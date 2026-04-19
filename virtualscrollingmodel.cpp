#include "virtualscrollingmodel.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QElapsedTimer>
#include <QApplication>
#include <QDateTime>
#include <QMutexLocker>

VirtualScrollingModel::VirtualScrollingModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_database(nullptr)
    , m_loadTimer(new QTimer(this))
    , m_prefetchTimer(new QTimer(this))
    , m_firstVisibleRow(0)
    , m_lastVisibleRow(0)
    , m_prefetchSize(100)  // 预取100行（可见区域前后各50行）
    , m_totalRecords(0)
    , m_loading(false)
    , m_loadedStartRow(-1)
    , m_loadedEndRow(-1)
{
    // 设置表头
    m_headers << "项目ID" << "项目名称" << "项目经理" << "开始日期"
              << "结束日期" << "预算(万元)" << "状态" << "描述";
    
    // 配置加载定时器（防抖）
    m_loadTimer->setSingleShot(true);
    m_loadTimer->setInterval(100);  // 100ms防抖
    
    // 配置预取定时器
    m_prefetchTimer->setSingleShot(true);
    m_prefetchTimer->setInterval(200);
    
    connect(m_loadTimer, &QTimer::timeout, this, &VirtualScrollingModel::loadVisibleData);
    connect(m_prefetchTimer, &QTimer::timeout, this, &VirtualScrollingModel::onPrefetchTimer);
    
    // 设置缓存大小
    m_rowCache.setMaxCost(MAX_CACHE_SIZE);
}

VirtualScrollingModel::~VirtualScrollingModel()
{
}

int VirtualScrollingModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    // 返回总记录数，而不是实际加载的行数
    // 这样视图可以正确显示滚动条
    return static_cast<int>(qMin(m_totalRecords, static_cast<qint64>(INT_MAX)));
}

int VirtualScrollingModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_headers.size();
}

QVariant VirtualScrollingModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_totalRecords)) {
        return QVariant();
    }
    
    int row = index.row();
    
    if (role == Qt::DisplayRole) {
        // 首先尝试从当前加载的行中获取
        if (m_loadedRows.contains(row)) {
            const QStringList &rowData = m_loadedRows[row];
            if (index.column() < rowData.size()) {
                return rowData.at(index.column());
            }
        }
        
        // 尝试从缓存中获取
        QMutexLocker locker(&m_cacheMutex);
        CachedRow *cachedRow = m_rowCache.object(row);
        if (cachedRow) {
            // 检查缓存是否过期
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - cachedRow->timestamp < CACHE_EXPIRE_MS) {
                const QStringList &rowData = cachedRow->data;
                if (index.column() < rowData.size()) {
                    return rowData.at(index.column());
                }
            }
        }
        
        // 如果不在缓存中，触发异步加载
        QMetaObject::invokeMethod(const_cast<VirtualScrollingModel*>(this), 
                                  "loadVisibleData", Qt::QueuedConnection);
        
        return QString("加载中...");  // 临时占位符
    }
    
    return QVariant();
}

QVariant VirtualScrollingModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.size()) {
            return m_headers.at(section);
        }
    }
    return QVariant();
}

Qt::ItemFlags VirtualScrollingModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void VirtualScrollingModel::setDatabase(QSqlDatabase *database)
{
    m_database = database;
    
    // 快速获取总记录数
    if (m_database && m_database->isOpen()) {
        QSqlQuery quickQuery("SELECT COUNT(*) FROM projects", *m_database);
        if (quickQuery.exec() && quickQuery.next()) {
            m_totalRecords = quickQuery.value(0).toLongLong();
            emit totalRecordsChanged(m_totalRecords);
        }
    }
    
    // 延迟刷新，避免阻塞
    QTimer::singleShot(100, this, &VirtualScrollingModel::refreshData);
}

void VirtualScrollingModel::setVisibleRange(int firstVisibleRow, int lastVisibleRow)
{
    if (m_firstVisibleRow != firstVisibleRow || m_lastVisibleRow != lastVisibleRow) {
        m_firstVisibleRow = firstVisibleRow;
        m_lastVisibleRow = lastVisibleRow;
        
        // 触发加载（使用防抖）
        m_loadTimer->start();
    }
}

void VirtualScrollingModel::setPrefetchSize(int rows)
{
    m_prefetchSize = qMax(50, qMin(500, rows));  // 限制在50-500行之间
}

void VirtualScrollingModel::refreshData()
{
    if (m_loading) {
        return;
    }
    
    m_loading = true;
    emit dataLoadingStarted();
    
    // 清除缓存和已加载数据
    {
        QMutexLocker locker(&m_cacheMutex);
        m_rowCache.clear();
    }
    m_loadedRows.clear();
    m_loadedStartRow = -1;
    m_loadedEndRow = -1;
    
    // 重新计算总记录数
    calculateTotalRecords();
    
    // 加载可见数据
    loadVisibleData();
}

void VirtualScrollingModel::setSearchFilter(const QString &filter, const QString &filterType)
{
    if (m_searchFilter != filter || m_filterType != filterType) {
        m_searchFilter = filter;
        m_filterType = filterType;
        
        // 清除缓存
        {
            QMutexLocker locker(&m_cacheMutex);
            m_rowCache.clear();
        }
        m_loadedRows.clear();
        
        refreshData();
    }
}

void VirtualScrollingModel::clearSearchFilter()
{
    if (!m_searchFilter.isEmpty()) {
        m_searchFilter.clear();
        m_filterType = "all";
        
        refreshData();
    }
}

void VirtualScrollingModel::loadVisibleData()
{
    if (!m_database || !m_database->isOpen() || m_totalRecords == 0) {
        m_loading = false;
        emit dataLoadingFinished();
        return;
    }
    
    if (m_firstVisibleRow < 0 || m_lastVisibleRow < m_firstVisibleRow) {
        return;
    }
    
    // 计算需要加载的范围（包含预取缓冲区）
    int bufferSize = m_prefetchSize / 2;  // 前后各一半
    int loadStart = qMax(0, m_firstVisibleRow - bufferSize);
    int loadEnd = qMin(static_cast<int>(m_totalRecords - 1), m_lastVisibleRow + bufferSize);
    int loadCount = loadEnd - loadStart + 1;
    
    // 如果这些行已经加载，跳过
    if (loadStart >= m_loadedStartRow && loadEnd <= m_loadedEndRow) {
        m_loading = false;
        emit dataLoadingFinished();
        return;
    }
    
    QElapsedTimer timer;
    timer.start();
    
    // 加载数据
    QSqlQuery query(*m_database);
    query.prepare(buildQuery(loadStart, loadCount, false));
    query.setForwardOnly(true);
    
    // 绑定搜索参数
    if (!m_searchFilter.isEmpty()) {
        QString searchPattern = "%" + m_searchFilter + "%";
        if (m_filterType == "all") {
            query.bindValue(0, searchPattern);
            query.bindValue(1, searchPattern);
            query.bindValue(2, searchPattern);
            query.bindValue(3, searchPattern);
        } else if (m_filterType == "name") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "manager") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "status") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "id") {
            query.bindValue(0, searchPattern);
        }
    }
    
    if (query.exec()) {
        // 清空超出范围的数据
        QHash<int, QStringList> newLoadedRows;
        
        int currentRow = loadStart;
        while (query.next() && currentRow <= loadEnd) {
            QStringList rowData;
            rowData.reserve(m_headers.size());
            for (int i = 0; i < m_headers.size(); ++i) {
                rowData << query.value(i).toString();
            }
            
            newLoadedRows[currentRow] = rowData;
            
            // 同时加入缓存
            CachedRow *cachedRow = new CachedRow;
            cachedRow->data = rowData;
            cachedRow->timestamp = QDateTime::currentMSecsSinceEpoch();
            
            {
                QMutexLocker locker(&m_cacheMutex);
                m_rowCache.insert(currentRow, cachedRow);
            }
            
            currentRow++;
        }
        
        m_loadedRows = newLoadedRows;
        m_loadedStartRow = loadStart;
        m_loadedEndRow = currentRow - 1;
        
        // 通知视图更新
        if (!newLoadedRows.isEmpty()) {
            beginResetModel();
            endResetModel();
        }
        
        qint64 elapsed = timer.elapsed();
        qDebug() << QString("加载 %1 行数据，耗时: %2ms").arg(newLoadedRows.size()).arg(elapsed);
        
        // 调度预取
        schedulePrefetch(m_lastVisibleRow);
    } else {
        qDebug() << "查询执行失败:" << query.lastError().text();
    }
    
    m_loading = false;
    emit dataLoadingFinished();
}

void VirtualScrollingModel::calculateTotalRecords()
{
    if (!m_database || !m_database->isOpen()) {
        return;
    }
    
    // 使用缓存的总记录数（如果没有搜索条件）
    if (m_searchFilter.isEmpty() && m_totalRecords > 0) {
        return;
    }
    
    QSqlQuery query(*m_database);
    query.prepare(buildQuery(0, 0, true));
    query.setForwardOnly(true);
    
    // 绑定搜索参数
    if (!m_searchFilter.isEmpty()) {
        QString searchPattern = "%" + m_searchFilter + "%";
        if (m_filterType == "all") {
            query.bindValue(0, searchPattern);
            query.bindValue(1, searchPattern);
            query.bindValue(2, searchPattern);
            query.bindValue(3, searchPattern);
        } else if (m_filterType == "name") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "manager") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "status") {
            query.bindValue(0, searchPattern);
        } else if (m_filterType == "id") {
            query.bindValue(0, searchPattern);
        }
    }
    
    if (query.exec() && query.next()) {
        m_totalRecords = query.value(0).toLongLong();
        emit totalRecordsChanged(m_totalRecords);
        qDebug() << "总记录数:" << m_totalRecords;
    }
}

QString VirtualScrollingModel::buildQuery(int startRow, int count, bool countOnly) const
{
    QString sql;
    
    if (countOnly) {
        sql = "SELECT COUNT(*) FROM projects";
    } else {
        sql = "SELECT project_id, project_name, manager, start_date, end_date, budget, status, description FROM projects";
    }
    
    // 添加搜索条件
    if (!m_searchFilter.isEmpty()) {
        QStringList conditions;
        
        if (m_filterType == "all" || m_filterType == "name") {
            conditions << "project_name LIKE ?";
        }
        if (m_filterType == "all" || m_filterType == "manager") {
            conditions << "manager LIKE ?";
        }
        if (m_filterType == "all" || m_filterType == "status") {
            conditions << "status LIKE ?";
        }
        if (m_filterType == "all" || m_filterType == "id") {
            conditions << "project_id LIKE ?";
        }
        
        if (!conditions.isEmpty()) {
            sql += " WHERE " + conditions.join(" OR ");
        }
    }
    
    if (!countOnly) {
        // 使用复合索引优化排序
        sql += " ORDER BY created_at DESC, project_id DESC";
        sql += QString(" LIMIT %1 OFFSET %2").arg(count).arg(startRow);
    }
    
    return sql;
}

void VirtualScrollingModel::schedulePrefetch(int currentRow)
{
    // 如果接近已加载范围的边界，预取更多数据
    int threshold = m_prefetchSize / 4;  // 距离边界25%时触发预取
    
    if (currentRow >= m_loadedEndRow - threshold) {
        // 需要向后预取
        m_prefetchTimer->start();
    } else if (currentRow <= m_loadedStartRow + threshold && m_loadedStartRow > 0) {
        // 需要向前预取
        m_prefetchTimer->start();
    }
}

void VirtualScrollingModel::onPrefetchTimer()
{
    // 在后台预取数据，不阻塞UI
    QTimer::singleShot(0, this, [this]() {
        if (m_lastVisibleRow >= m_loadedEndRow - m_prefetchSize / 4) {
            // 向后预取
            int prefetchStart = m_loadedEndRow + 1;
            int prefetchCount = m_prefetchSize;
            
            if (prefetchStart < static_cast<int>(m_totalRecords)) {
                loadRowsFromDatabase(prefetchStart, prefetchCount);
            }
        } else if (m_firstVisibleRow <= m_loadedStartRow + m_prefetchSize / 4 && m_loadedStartRow > 0) {
            // 向前预取
            int prefetchStart = qMax(0, m_loadedStartRow - m_prefetchSize);
            int prefetchCount = m_loadedStartRow - prefetchStart;
            
            if (prefetchCount > 0) {
                loadRowsFromDatabase(prefetchStart, prefetchCount);
            }
        }
    });
}

void VirtualScrollingModel::loadRowsFromDatabase(int startRow, int count)
{
    if (!m_database || !m_database->isOpen() || count <= 0) {
        return;
    }
    
    // 检查是否已经在加载范围或缓存中
    if (startRow >= m_loadedStartRow && startRow + count - 1 <= m_loadedEndRow) {
        return;
    }
    
    QSqlQuery query(*m_database);
    query.prepare(buildQuery(startRow, count, false));
    query.setForwardOnly(true);
    
    // 绑定搜索参数（如果存在）
    if (!m_searchFilter.isEmpty()) {
        QString searchPattern = "%" + m_searchFilter + "%";
        if (m_filterType == "all") {
            query.bindValue(0, searchPattern);
            query.bindValue(1, searchPattern);
            query.bindValue(2, searchPattern);
            query.bindValue(3, searchPattern);
        } else {
            query.bindValue(0, searchPattern);
        }
    }
    
    if (query.exec()) {
        int currentRow = startRow;
        while (query.next() && currentRow < startRow + count) {
            QStringList rowData;
            rowData.reserve(m_headers.size());
            for (int i = 0; i < m_headers.size(); ++i) {
                rowData << query.value(i).toString();
            }
            
            // 只缓存，不立即加载到视图（因为不在可见范围）
            CachedRow *cachedRow = new CachedRow;
            cachedRow->data = rowData;
            cachedRow->timestamp = QDateTime::currentMSecsSinceEpoch();
            
            {
                QMutexLocker locker(&m_cacheMutex);
                m_rowCache.insert(currentRow, cachedRow);
            }
            
            currentRow++;
        }
    }
}

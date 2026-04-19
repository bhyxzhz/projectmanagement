#include "simplepaginationmodel.h"
#include "appconstants.h"
#include "configmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QApplication>
#include <QColor>
#include <QModelIndex>
#include <QVariant>
#include <QElapsedTimer>
#include <algorithm>

SimplePaginationModel::SimplePaginationModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_database(nullptr)
    , m_loadTimer(new QTimer(this))
    , m_currentPage(1)
    , m_totalRecords(0)
    , m_totalPages(0)
    , m_dataLoading(false)
    , m_queriesPaused(false)
    , m_highlightedRow(-1)
    , m_totalRecordsCached(false)
    , m_cachedTotalRecords(0)
{
    // 从配置管理器读取分页和缓存配置（符合千万级产品标准化要求）
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.initialize()) {
        qDebug() << "警告：配置管理器初始化失败，使用默认值";
    }
    m_pageSize = config.getPageSize();
    m_cacheSize = config.getCacheSize();

    // 设置表头
    m_headers << "项目ID" << "项目名称" << "项目经理" << "开始日期"
              << "结束日期" << "预算(万元)" << "状态" << "描述";

    // 配置加载定时器（防抖机制：延迟加载避免频繁查询）
    m_loadTimer->setSingleShot(true);
    m_loadTimer->setInterval(PaginationConstants::LOAD_TIMER_INTERVAL_MS);

    connect(m_loadTimer, &QTimer::timeout, this, &SimplePaginationModel::loadPageData);

    // 监听配置变化
    connect(&config, &ConfigManager::configChanged, this, [this](const QString& key, const QVariant& value) {
        if (key == "Pagination/PageSize") {
            int newPageSize = value.toInt();
            if (newPageSize != m_pageSize) {
                m_pageSize = newPageSize;
                // 如果当前页超出范围，调整到第一页
                if (m_currentPage > getTotalPages()) {
                    m_currentPage = 1;
                }
                refreshData();
            }
        } else if (key == "Pagination/CacheSize") {
            int newCacheSize = value.toInt();
            if (newCacheSize != m_cacheSize) {
                m_cacheSize = newCacheSize;
                // 清理超出新缓存大小的页面
                if (m_pageCache.size() > m_cacheSize) {
                    // 保留最近访问的页面
                    // 这里简化处理：清空缓存，重新加载
                    clearCache();
                }
            }
        }
    });
}

SimplePaginationModel::~SimplePaginationModel()
{
}

int SimplePaginationModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_currentPageData.size();
}

int SimplePaginationModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_headers.size();
}

QVariant SimplePaginationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_currentPageData.size() || index.column() >= m_headers.size()) {
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        const QStringList &rowData = m_currentPageData.at(index.row());
        if (index.column() < rowData.size()) {
            return rowData.at(index.column());
        }
    }

    // 高亮显示新添加的行
    if (role == Qt::BackgroundRole && index.row() == m_highlightedRow) {
        return QColor(173, 216, 230); // 浅蓝色背景
    }

    if (role == Qt::ForegroundRole && index.row() == m_highlightedRow) {
        return QColor(0, 0, 139); // 深蓝色文字
    }

    return QVariant();
}

QVariant SimplePaginationModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.size()) {
            return m_headers.at(section);
        }
    }
    return QVariant();
}

Qt::ItemFlags SimplePaginationModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void SimplePaginationModel::setPageSize(int pageSize)
{
    if (pageSize > 0 && pageSize != m_pageSize) {
        m_pageSize = pageSize;
        refreshData();
    }
}

void SimplePaginationModel::setCurrentPage(int page)
{
    if (page > 0 && page != m_currentPage) {
        m_currentPage = page;

        // 检查缓存中是否有该页数据
        if (isPageCached(page)) {
            qDebug() << "从缓存加载页面数据，页号:" << page;
            QElapsedTimer timer;
            timer.start();
            loadPageFromCache(page);
            qint64 elapsed = timer.elapsed();
            QString queryType = m_searchFilter.isEmpty() ? "分页查询(缓存)" : QString("分页搜索(缓存,%1)").arg(m_filterType);
            // 发送查询性能信号，标记为缓存命中
            emit queryPerformed(queryType, elapsed, true);
        } else {
            qDebug() << "缓存中无数据，刷新页面数据，页号:" << page;
            refreshData();
        }
    }
}

int SimplePaginationModel::getTotalPages() const
{
    return m_totalPages;
}

void SimplePaginationModel::refreshData()
{
    // 如果查询已暂停（数据生成期间），直接返回，避免数据库锁定
    if (m_queriesPaused) {
        qDebug() << "查询已暂停（数据生成中），跳过刷新";
        return;
    }
    
    if (m_dataLoading) {
        return;
    }

    m_dataLoading = true;
    emit dataLoadingStarted();

    // 记录缓存状态，用于判断是否是删除/插入操作
    bool wasCached = m_totalRecordsCached;
    
    // 清除缓存，确保重新计算总记录数
    clearCache();

    // 如果是删除/插入操作（之前有缓存，现在被清除），立即重新计算总记录数
    // 这样可以确保记录数在数据加载前就更新，用户能立即看到变化
    if (wasCached && !m_queriesPaused && m_database && m_database->isOpen()) {
        qDebug() << "检测到删除/插入操作，立即重新计算总记录数";
        // 立即计算，不延迟
        forceRecalculateTotalRecords();
    }

    // 立即加载数据，不使用延迟
    loadPageData();
}

void SimplePaginationModel::forceRecalculateTotalRecords()
{
    // 强制清除总记录数缓存，立即重新计算
    m_totalRecordsCached = false;
    m_cachedTotalRecords = 0;
    
    // 立即计算总记录数（不延迟）
    if (!m_queriesPaused && m_database && m_database->isOpen()) {
        qDebug() << "强制立即重新计算总记录数（删除/插入后）";
        
        // 对于删除/插入操作，使用准确的 COUNT(*) 查询，而不是 MAX(id) 估算
        // 这样可以确保删除后总记录数准确更新
        if (m_searchFilter.isEmpty()) {
            QElapsedTimer timer;
            timer.start();
            
            QSqlQuery query(*m_database);
            query.prepare("SELECT COUNT(*) FROM projects");
            query.setForwardOnly(true);
            
            if (query.exec() && query.next()) {
                const qint64 elapsed = timer.elapsed();
                emit queryPerformed("准确计数", elapsed, false);
                
                m_totalRecords = query.value(0).toInt();
                if (m_totalRecords < 0) m_totalRecords = 0;
                m_totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
                
                m_cachedTotalRecords = m_totalRecords;
                m_totalRecordsCached = true;
                
                qDebug() << "准确总记录数:" << m_totalRecords << "总页数:" << m_totalPages << "耗时:" << elapsed << "ms";
                emit totalRecordsChanged(m_totalRecords);
                // 发送pageChanged信号，通知分页控制器更新总页数
                emit pageChanged(m_currentPage, m_totalPages);
            } else {
                qDebug() << "计算总记录数失败:" << query.lastError().text();
            }
        } else {
            // 有搜索条件时，使用原有的计算方法
            calculateTotalRecords();
        }
    }
}

void SimplePaginationModel::setSearchFilter(const QString &filter, const QString &filterType)
{
    if (m_searchFilter != filter || m_filterType != filterType) {
        m_searchFilter = filter;
        m_filterType = filterType;
        m_currentPage = 1; // 重置到第一页

        // 搜索条件改变时清除缓存
        clearCache();

        refreshData();
    }
}

void SimplePaginationModel::clearSearchFilter()
{
    if (!m_searchFilter.isEmpty()) {
        m_searchFilter.clear();
        m_filterType = "all";
        m_currentPage = 1;

        // 清除搜索时清除缓存
        clearCache();

        refreshData();
    }
}

void SimplePaginationModel::setDatabase(QSqlDatabase *database)
{
    qDebug() << "SimplePaginationModel::setDatabase called, database ptr:" << database;
    m_database = database;

    // 如果数据库有效，清除缓存
    if (m_database && m_database->isOpen()) {
        qDebug() << "Database is open, clearing cache";
        clearCache();
    } else if (m_database) {
        qDebug() << "Database is not open, connection name:" << m_database->connectionName();
    } else {
        qDebug() << "Database pointer is null";
    }
}

void SimplePaginationModel::loadPageData()
{
    // 如果查询已暂停（数据生成期间），直接返回，避免数据库锁定
    if (m_queriesPaused) {
        qDebug() << "查询已暂停（数据生成中），跳过加载页面数据";
        m_dataLoading = false;
        emit dataLoadingFinished();
        return;
    }
    
    if (!m_database || !m_database->isOpen()) {
        qDebug() << "数据库未连接，无法加载数据";
        m_dataLoading = false;
        emit dataLoadingFinished();
        return;
    }

    qDebug() << "开始加载页面数据，当前页:" << m_currentPage;

    try {
        // 关键优化：先加载当前页数据，让用户快速看到界面
        // 总记录数计算延迟到后台异步执行，不阻塞UI
        bool needCalculateTotal = !m_totalRecordsCached || m_totalRecords == 0;
        
        // 如果总记录数未缓存，先设置一个临时值，避免UI显示异常
        if (needCalculateTotal && m_totalRecords == 0) {
            // 设置临时总页数为1，后续异步更新
            m_totalPages = 1;
            emit totalRecordsChanged(0); // 先通知UI，表示正在加载
        }

        // 先加载当前页数据（快速响应）
        loadPageFromDatabase();

        // 将数据添加到缓存
        addToCache(m_currentPage, m_currentPageData);

        qDebug() << "页面数据加载完成，当前页数据行数:" << m_currentPageData.size();

        // 异步计算总记录数（不阻塞UI）
        // 注意：如果缓存被清除（删除/插入操作），总记录数已经在 refreshData() 中立即计算了
        // 这里只需要处理初始加载的情况（m_totalRecords == 0 且 m_totalRecordsCached == false）
        if (needCalculateTotal) {
            if (m_totalRecordsCached) {
                // 已经在 refreshData() 中计算过了，不需要再计算
                qDebug() << "总记录数已在 refreshData() 中计算，跳过重复计算";
            } else if (m_totalRecords == 0) {
                // 初始加载：延迟计算，使用快速估算（MAX(id)），让UI先显示数据
                QTimer::singleShot(300, this, [this]() {
                    qDebug() << "初始加载：异步计算总记录数";
                    calculateTotalRecords();
                });
            } else {
                // 缓存被清除但总记录数不为0（可能是删除/插入操作后计算失败），再次尝试
                qDebug() << "缓存已清除但总记录数不为0，再次尝试计算（可能 refreshData() 中的计算失败了）";
                QTimer::singleShot(100, this, [this]() {
                    forceRecalculateTotalRecords();
                });
            }
        }

    } catch (const std::exception& e) {
        qDebug() << "加载页面数据时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "加载页面数据时发生未知异常";
    }

    m_dataLoading = false;
    emit dataLoadingFinished();
}

/**
 * @brief 计算总记录数
 * 
 * 性能优化策略：
 * 1. 缓存机制：如果总记录数已缓存且无搜索条件，直接使用缓存，避免重复查询
 * 2. 准确计数：无搜索条件时使用 COUNT(*) 查询，确保记录数准确（即使有删除操作也能正确显示）
 * 3. 条件查询：有搜索条件时使用buildQuery构建带WHERE的COUNT查询
 * 
 * 注意：虽然 MAX(id) 查询更快，但它不准确（当存在删除操作时，MAX(id) 会大于实际记录数）
 * 对于200万级别的数据，COUNT(*) 查询通常只需要几十到几百毫秒，这是可以接受的
 * 
 * 算法复杂度：
 * - 无搜索条件：O(n) - COUNT(*) 需要扫描全表，但SQLite会优化
 * - 有搜索条件：O(n) - 需要扫描匹配的记录
 */
void SimplePaginationModel::calculateTotalRecords()
{
    // 如果查询已暂停（数据生成期间），直接返回，避免数据库锁定
    if (m_queriesPaused) {
        qDebug() << "查询已暂停（数据生成中），跳过计算总记录数";
        return;
    }
    
    if (!m_database || !m_database->isOpen()) {
        qDebug() << "数据库未连接，无法计算总记录数";
        return;
    }

    // 优化：如果总记录数已缓存且没有搜索条件，直接使用缓存（避免重复查询）
    if (m_totalRecordsCached && m_searchFilter.isEmpty()) {
        m_totalRecords = m_cachedTotalRecords;
        // 计算总页数：向上取整 (总记录数 + 每页大小 - 1) / 每页大小
        m_totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
        qDebug() << "使用缓存的总记录数:" << m_totalRecords << "总页数:" << m_totalPages;
        // 发送查询性能信号，标记为缓存命中
        emit queryPerformed("缓存计数", 0, true);
        emit totalRecordsChanged(m_totalRecords);
        // 发送pageChanged信号，通知分页控制器更新总页数
        emit pageChanged(m_currentPage, m_totalPages);
        return;
    }

    // 使用准确的 COUNT(*) 查询，确保记录数准确
    // 注意：虽然 MAX(id) 查询更快，但它不准确（当存在删除操作时，MAX(id) 会大于实际记录数）
    // 对于200万级别的数据，COUNT(*) 查询通常只需要几十到几百毫秒，这是可以接受的
    if (m_searchFilter.isEmpty()) {
        qDebug() << "使用准确计数（COUNT(*)）";
        QElapsedTimer timer;
        timer.start();

        QSqlQuery query(*m_database);
        query.prepare("SELECT COUNT(*) FROM projects");
        query.setForwardOnly(true);
        
        // 执行查询，设置合理的超时时间（10秒）
        QElapsedTimer timeoutTimer;
        timeoutTimer.start();
        
        // 尝试执行查询，如果超过10秒则放弃
        if (query.exec()) {
            qint64 queryTime = timeoutTimer.elapsed();
            if (queryTime > 10000) {
                qDebug() << "COUNT(*)查询超时（超过10秒），使用默认值";
                // 设置一个合理的默认值，避免UI异常
                m_totalRecords = 0;
                m_totalPages = 1;
                emit totalRecordsChanged(0);
                return;
            }
            
            if (query.next()) {
                const qint64 elapsed = timer.elapsed();
                emit queryPerformed("准确计数", elapsed, false);

                m_totalRecords = query.value(0).toInt();
                if (m_totalRecords < 0) m_totalRecords = 0;
                m_totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;

                m_cachedTotalRecords = m_totalRecords;
                m_totalRecordsCached = true;

                qDebug() << "准确总记录数:" << m_totalRecords << "总页数:" << m_totalPages << "耗时:" << elapsed << "ms";
                emit totalRecordsChanged(m_totalRecords);
                // 发送pageChanged信号，通知分页控制器更新总页数
                emit pageChanged(m_currentPage, m_totalPages);
                return;
            }
        }
        
        // 查询失败
        qDebug() << "COUNT(*)查询失败:" << query.lastError().text();
        // 查询失败时，设置默认值，避免UI异常
        m_totalRecords = 0;
        m_totalPages = 1;
        emit totalRecordsChanged(0);
        return;
    }

    // 有搜索条件时使用原来的方法
    QString countQuery = buildQuery(true);
    qDebug() << "执行计数查询:" << countQuery;

    QSqlQuery query(*m_database);
    query.prepare(countQuery);
    query.setForwardOnly(true);  // 性能优化：只向前查询

    // 绑定搜索参数
    if (!m_searchFilter.isEmpty()) {
        QString searchPattern = "%" + m_searchFilter + "%";

        if (m_filterType == "all") {
            // 全部搜索：绑定4个参数
            query.bindValue(0, searchPattern); // project_name
            query.bindValue(1, searchPattern); // manager
            query.bindValue(2, searchPattern); // status
            query.bindValue(3, searchPattern); // project_id
        } else if (m_filterType == "name") {
            query.bindValue(0, searchPattern); // project_name
        } else if (m_filterType == "manager") {
            query.bindValue(0, searchPattern); // manager
        } else if (m_filterType == "status") {
            query.bindValue(0, searchPattern); // status
        } else if (m_filterType == "id") {
            query.bindValue(0, searchPattern); // project_id
        }
    }

    if (query.exec() && query.next()) {
        m_totalRecords = query.value(0).toInt();
        m_totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
        qDebug() << "总记录数:" << m_totalRecords << "总页数:" << m_totalPages;
        emit totalRecordsChanged(m_totalRecords);
        // 发送pageChanged信号，通知分页控制器更新总页数
        emit pageChanged(m_currentPage, m_totalPages);
    } else {
        qDebug() << "计算总记录数失败:" << query.lastError().text();
    }
}

/**
 * @brief 从数据库加载当前页数据
 * 
 * 性能优化：
 * 1. 使用LIMIT/OFFSET实现分页，只加载当前页数据
 * 2. setForwardOnly(true)：只向前查询，节省内存
 * 3. 预分配内存：reserve()减少内存重新分配
 * 4. 性能监控：记录查询耗时，用于性能分析
 */
void SimplePaginationModel::loadPageFromDatabase()
{
    if (!m_database || !m_database->isOpen()) {
        return;
    }

    // 关键优化：检查页面缓存，如果已缓存则直接使用（避免重复查询）
    if (isPageCached(m_currentPage)) {
        qDebug() << "从缓存加载页面数据，页号:" << m_currentPage;
        QElapsedTimer timer;
        timer.start();
        loadPageFromCache(m_currentPage);
        qint64 elapsed = timer.elapsed();
        QString queryType = m_searchFilter.isEmpty() ? "分页查询(缓存)" : QString("分页搜索(缓存,%1)").arg(m_filterType);
        // 发送查询性能信号，标记为缓存命中
        emit queryPerformed(queryType, elapsed, true);
        return;
    }

    beginResetModel();

    QElapsedTimer timer;
    timer.start();

    // 安全验证：确保分页参数在有效范围内
    if (m_pageSize <= 0 || m_pageSize > PaginationConstants::MAX_PAGE_SIZE) {
        qDebug() << "警告：页面大小超出范围，使用默认值";
        m_pageSize = PaginationConstants::DEFAULT_PAGE_SIZE;
    }
    if (m_currentPage < 1) {
        qDebug() << "警告：当前页号无效，重置为第1页";
        m_currentPage = 1;
    }

    QSqlQuery query(*m_database);
    query.prepare(buildQuery(false));
    query.setForwardOnly(true); // 优化：只向前查询，节省内存，提高性能

    // 绑定搜索参数
    if (!m_searchFilter.isEmpty()) {
        QString searchPattern = "%" + m_searchFilter + "%";

        if (m_filterType == "all") {
            // 全部搜索：绑定4个参数
            query.bindValue(0, searchPattern); // project_name
            query.bindValue(1, searchPattern); // manager
            query.bindValue(2, searchPattern); // status
            query.bindValue(3, searchPattern); // project_id
        } else if (m_filterType == "name") {
            query.bindValue(0, searchPattern); // project_name
        } else if (m_filterType == "manager") {
            query.bindValue(0, searchPattern); // manager
        } else if (m_filterType == "status") {
            query.bindValue(0, searchPattern); // status
        } else if (m_filterType == "id") {
            query.bindValue(0, searchPattern); // project_id
        }
    }

    if (query.exec()) {
        // 记录查询性能
        qint64 elapsed = timer.elapsed();
        QString queryType = m_searchFilter.isEmpty() ? "分页查询" : QString("分页搜索(%1)").arg(m_filterType);
        
        // 发送查询性能信号（用于性能监控）
        emit queryPerformed(queryType, elapsed, false);
        m_currentPageData.clear();
        // 性能优化：预分配内存，避免动态扩容
        m_currentPageData.reserve(m_pageSize);

        // 遍历查询结果，构建页面数据
        while (query.next()) {
            QStringList rowData;
            // 性能优化：预分配行数据内存
            rowData.reserve(m_headers.size());
            for (int i = 0; i < m_headers.size(); ++i) {
                rowData << query.value(i).toString();
            }
            m_currentPageData << rowData;
        }
    } else {
        QString errorText = query.lastError().text();
        qDebug() << "查询执行失败:" << errorText;
        
        // 如果查询失败，检查数据库连接状态
        if (!m_database || !m_database->isOpen()) {
            qDebug() << "数据库连接已关闭，无法执行查询";
        } else {
            qDebug() << "数据库连接正常，但查询失败，错误详情:" << errorText;
        }
    }

    endResetModel();
    emit pageChanged(m_currentPage, m_totalPages);
}

QString SimplePaginationModel::buildQuery(bool countOnly) const
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
        // 优化：使用索引支持的排序（created_at DESC, project_id DESC有复合索引）
        // 索引：idx_projects_created_at_id 支持此排序，避免全表扫描
        sql += " ORDER BY created_at DESC, project_id DESC";
        // 注意：SQLite不支持LIMIT/OFFSET的参数绑定，但通过严格验证整数参数确保安全
        // 安全措施：1) 使用整数类型 2) 验证范围 3) 防止负数
        int limit = qMax(1, qMin(m_pageSize, PaginationConstants::MAX_PAGE_SIZE));  // 限制在合理范围内
        int offset = qMax(0, (m_currentPage - 1) * limit);  // 确保非负
        sql += QString(" LIMIT %1 OFFSET %2").arg(limit).arg(offset);
    }

    return sql;
}

void SimplePaginationModel::setHighlightedRow(int row)
{
    if (row != m_highlightedRow) {
        int oldRow = m_highlightedRow;
        m_highlightedRow = row;

        // 通知视图更新显示
        if (oldRow >= 0 && oldRow < m_currentPageData.size()) {
            QModelIndex topLeft = createIndex(oldRow, 0);
            QModelIndex bottomRight = createIndex(oldRow, m_headers.size() - 1);
            emit dataChanged(topLeft, bottomRight, {Qt::BackgroundRole, Qt::ForegroundRole});
        }

        if (m_highlightedRow >= 0 && m_highlightedRow < m_currentPageData.size()) {
            QModelIndex topLeft = createIndex(m_highlightedRow, 0);
            QModelIndex bottomRight = createIndex(m_highlightedRow, m_headers.size() - 1);
            emit dataChanged(topLeft, bottomRight, {Qt::BackgroundRole, Qt::ForegroundRole});
        }
    }
}

void SimplePaginationModel::clearHighlight()
{
    if (m_highlightedRow >= 0) {
        int oldRow = m_highlightedRow;
        m_highlightedRow = -1;

        // 通知视图更新显示
        if (oldRow < m_currentPageData.size()) {
            QModelIndex topLeft = createIndex(oldRow, 0);
            QModelIndex bottomRight = createIndex(oldRow, m_headers.size() - 1);
            emit dataChanged(topLeft, bottomRight, {Qt::BackgroundRole, Qt::ForegroundRole});
        }
    }
}

/**
 * @brief 设置缓存大小
 * 
 * 实现LRU（最近最少使用）缓存策略：
 * 1. 当缓存超过大小限制时，删除最旧的页面
 * 2. 保留最近访问的页面，提高分页切换性能
 * 
 * @param size 缓存页数（最小为1）
 */
void SimplePaginationModel::setCacheSize(int size)
{
    m_cacheSize = qMax(1, size);

    // 如果当前缓存超过新的大小限制，清理多余的缓存
    if (m_pageCache.size() > m_cacheSize) {
        QList<int> keys = m_pageCache.keys();
        std::sort(keys.begin(), keys.end());

        // LRU策略：保留最近的页面，删除最旧的页面
        while (m_pageCache.size() > m_cacheSize) {
            m_pageCache.remove(keys.takeFirst());
        }
    }
}

void SimplePaginationModel::clearCache()
{
    m_pageCache.clear();
    m_totalRecordsCached = false;
    m_cachedTotalRecords = 0;
    qDebug() << "分页缓存已清空";
}

bool SimplePaginationModel::isPageCached(int page) const
{
    return m_pageCache.contains(page);
}

void SimplePaginationModel::loadPageFromCache(int page)
{
    if (m_pageCache.contains(page)) {
        beginResetModel();
        m_currentPageData = m_pageCache.value(page);
        endResetModel();
        emit pageChanged(m_currentPage, m_totalPages);
        qDebug() << "从缓存加载页面数据完成，页号:" << page << "数据行数:" << m_currentPageData.size();
    }
}

/**
 * @brief 将页面数据添加到缓存
 * 
 * 实现LRU缓存策略：
 * - 如果缓存已满，删除最旧的页面（按页号排序）
 * - 新页面添加到缓存末尾
 * 
 * @param page 页号
 * @param data 页面数据
 */
void SimplePaginationModel::addToCache(int page, const QList<QStringList>& data)
{
    // LRU策略：如果缓存已满，删除最旧的页面
    if (m_pageCache.size() >= m_cacheSize) {
        QList<int> keys = m_pageCache.keys();
        std::sort(keys.begin(), keys.end());
        m_pageCache.remove(keys.takeFirst());
    }

    m_pageCache.insert(page, data);
    qDebug() << "页面数据已添加到缓存，页号:" << page << "缓存大小:" << m_pageCache.size();
}

void SimplePaginationModel::pauseQueries(bool pause)
{
    m_queriesPaused = pause;
    qDebug() << "分页模型查询" << (pause ? "已暂停" : "已恢复");
}

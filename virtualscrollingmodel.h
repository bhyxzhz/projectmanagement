#ifndef VIRTUALSCROLLINGMODEL_H
#define VIRTUALSCROLLINGMODEL_H

#include <QAbstractTableModel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCache>
#include <QMutex>
#include <QTimer>
#include <QThread>
#include <QQueue>
#include <QHash>

/**
 * @brief 高性能虚拟滚动模型 - 支持千万级数据
 * 
 * 特点：
 * 1. 只加载可见行和前后缓冲区
 * 2. 智能预取机制
 * 3. 查询缓存和去重
 * 4. 异步加载
 * 5. 内存占用极小（约1-2MB用于缓存）
 */
class VirtualScrollingModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit VirtualScrollingModel(QObject *parent = nullptr);
    ~VirtualScrollingModel();

    // 重写QAbstractTableModel的虚函数
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // 虚拟滚动相关
    void setDatabase(QSqlDatabase *database);
    void setVisibleRange(int firstVisibleRow, int lastVisibleRow);
    void setPrefetchSize(int rows);  // 设置预取缓冲区大小（默认50行）
    
    // 数据操作
    void refreshData();
    void setSearchFilter(const QString &filter, const QString &filterType = "all");
    void clearSearchFilter();
    
    // 获取统计信息
    int getTotalRecords() const { return m_totalRecords; }
    bool isLoading() const { return m_loading; }

signals:
    void dataLoadingStarted();
    void dataLoadingFinished();
    void totalRecordsChanged(qint64 totalRecords);
    void loadProgress(int current, int total);

private slots:
    void loadVisibleData();
    void onPrefetchTimer();

private:
    struct CachedRow {
        QStringList data;
        qint64 timestamp;  // 缓存时间戳
    };

    void calculateTotalRecords();
    void loadRowsFromDatabase(int startRow, int count);
    QString buildQuery(int startRow, int count, bool countOnly = false) const;
    
    // 查询优化
    CachedRow* getCachedRow(int row) const;
    void cacheRow(int row, const QStringList &data);
    void clearExpiredCache();
    
    // 预取机制
    void schedulePrefetch(int startRow);
    void prefetchNext();

    QSqlDatabase *m_database;
    QTimer *m_loadTimer;
    QTimer *m_prefetchTimer;
    mutable QMutex m_cacheMutex;
    
    // 虚拟滚动参数
    int m_firstVisibleRow;
    int m_lastVisibleRow;
    int m_prefetchSize;  // 预取缓冲区大小（行数）
    
    // 总记录数（使用qint64支持千万级）
    qint64 m_totalRecords;
    
    // 搜索过滤
    QString m_searchFilter;
    QString m_filterType;
    
    // 加载状态
    bool m_loading;
    
    // 表头
    QStringList m_headers;
    
    // 缓存机制 - 使用QCache自动管理内存
    // Key: 行号, Value: 行数据
    mutable QCache<int, CachedRow> m_rowCache;
    static const int MAX_CACHE_SIZE = 200;  // 最多缓存200行
    static const int CACHE_EXPIRE_MS = 30000;  // 缓存30秒过期
    
    // 查询去重 - 避免重复查询（已移除，使用缓存机制替代）
    // mutable QHash<QueryKey, QSet<int>> m_pendingQueries;
    
    // 当前可见行的范围（在总记录中的位置）
    int m_loadedStartRow;
    int m_loadedEndRow;
    QHash<int, QStringList> m_loadedRows;  // 当前加载的行数据
};

#endif // VIRTUALSCROLLINGMODEL_H

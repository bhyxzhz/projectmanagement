#ifndef SIMPLEPAGINATIONMODEL_H
#define SIMPLEPAGINATIONMODEL_H

#include <QAbstractTableModel>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QTimer>
#include <QColor>
#include <QModelIndex>
#include <QVariant>

class SimplePaginationModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit SimplePaginationModel(QObject *parent = nullptr);
    ~SimplePaginationModel();

    // 重写QAbstractTableModel的虚函数
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // 分页相关方法
    void setPageSize(int pageSize);
    int getPageSize() const { return m_pageSize; }
    void setCurrentPage(int page);
    int getCurrentPage() const { return m_currentPage; }
    int getTotalPages() const;
    int getTotalRecords() const { return m_totalRecords; }

    // 数据操作
    void refreshData();
    void forceRecalculateTotalRecords();  // 强制立即重新计算总记录数（用于删除/插入后即时更新）
    void setSearchFilter(const QString &filter, const QString &filterType = "all");
    void clearSearchFilter();
    void setHighlightedRow(int row);
    void clearHighlight();

    // 性能优化
    void setDatabase(QSqlDatabase *database);
    void setCacheSize(int size);
    void clearCache();
    bool isPageCached(int page) const;
    void loadPageFromCache(int page);
    void addToCache(int page, const QList<QStringList>& data);
    
    // 数据生成期间暂停查询（避免数据库锁定）
    void pauseQueries(bool pause = true);
    bool isQueriesPaused() const { return m_queriesPaused; }

signals:
    void dataLoadingStarted();
    void dataLoadingFinished();
    void pageChanged(int currentPage, int totalPages);
    void totalRecordsChanged(int totalRecords);
    void queryPerformed(const QString& queryType, qint64 elapsedMs, bool fromCache = false);  // 查询性能信号

private slots:
    void loadPageData();

private:
    void calculateTotalRecords();
    void loadPageFromDatabase();
    QString buildQuery(bool countOnly = false) const;

    QSqlDatabase *m_database;
    QTimer *m_loadTimer;

    // 分页参数
    int m_pageSize;
    int m_currentPage;
    int m_totalRecords;
    int m_totalPages;

    // 搜索过滤
    QString m_searchFilter;
    QString m_filterType;

    // 性能优化
    bool m_dataLoading;
    bool m_queriesPaused;  // 是否暂停查询（数据生成期间避免锁定）

    // 当前页数据
    QList<QStringList> m_currentPageData;

    // 表头
    QStringList m_headers;

    // 高亮行
    int m_highlightedRow;

    // 缓存机制
    QHash<int, QList<QStringList>> m_pageCache;  // 页面数据缓存
    int m_cacheSize;  // 缓存大小
    bool m_totalRecordsCached;  // 总记录数是否已缓存
    int m_cachedTotalRecords;  // 缓存的总记录数
};

#endif // SIMPLEPAGINATIONMODEL_H

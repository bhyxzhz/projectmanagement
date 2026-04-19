#ifndef QUERYQUEUE_H
#define QUERYQUEUE_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

/**
 * @brief 查询队列管理器 - 优化数据库查询性能
 * 
 * 功能：
 * 1. 查询去重 - 相同的查询只执行一次
 * 2. 查询队列 - 有序执行查询请求
 * 3. 结果缓存 - 缓存查询结果
 * 4. 批量优化 - 合并相似查询
 */
class QueryQueue : public QThread
{
    Q_OBJECT

public:
    struct QueryRequest {
        int id;
        QString sql;
        QVariantList bindValues;
        qint64 timestamp;
        bool isCountQuery;
        
        bool operator==(const QueryRequest &other) const {
            return sql == other.sql && bindValues == other.bindValues;
        }
    };
    
    struct QueryResult {
        int requestId;
        bool success;
        QList<QStringList> rows;
        qint64 count;
        QString error;
        qint64 elapsedMs;
    };

    explicit QueryQueue(QObject *parent = nullptr);
    ~QueryQueue();

    // 提交查询请求
    int submitQuery(const QString &sql, const QVariantList &bindValues = {}, bool isCountQuery = false);
    
    // 设置数据库连接
    void setDatabase(QSqlDatabase *database);
    
    // 控制队列
    void startQueue();
    void stopQueue();
    void clearQueue();
    
    // 缓存管理
    void clearCache();
    void setCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }

signals:
    void queryCompleted(int requestId, const QueryResult &result);
    void queryFailed(int requestId, const QString &error);

protected:
    void run() override;

private:
    QString hashQuery(const QueryRequest &request) const;
    QueryResult executeQuery(const QueryRequest &request);
    
    QSqlDatabase *m_database;
    QQueue<QueryRequest> m_queue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    
    bool m_running;
    bool m_cacheEnabled;
    
    // 查询去重
    QHash<QString, QSet<int>> m_pendingQueries;  // Key: query hash, Value: request IDs
    
    // 结果缓存
    struct CachedResult {
        QList<QStringList> rows;
        qint64 count;
        qint64 timestamp;
    };
    QHash<QString, CachedResult> m_resultCache;
    // 缓存参数使用全局常量（在appconstants.h中定义）
    
    int m_nextRequestId;
};

#endif // QUERYQUEUE_H

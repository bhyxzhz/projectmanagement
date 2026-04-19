#include "queryqueue.h"
#include "appconstants.h"
#include "configmanager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QTimer>
#include <QDateTime>
#include <QMutexLocker>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>

QueryQueue::QueryQueue(QObject *parent)
    : QThread(parent)
    , m_database(nullptr)
    , m_running(false)
    , m_cacheEnabled(true)
    , m_nextRequestId(1)
{
}

QueryQueue::~QueryQueue()
{
    stopQueue();
}

int QueryQueue::submitQuery(const QString &sql, const QVariantList &bindValues, bool isCountQuery)
{
    QueryRequest request;
    request.id = m_nextRequestId++;
    request.sql = sql;
    request.bindValues = bindValues;
    request.timestamp = QDateTime::currentMSecsSinceEpoch();
    request.isCountQuery = isCountQuery;
    
    QString hash = hashQuery(request);
    
    QMutexLocker locker(&m_queueMutex);
    
    // 检查缓存
    if (m_cacheEnabled && m_resultCache.contains(hash)) {
        const CachedResult &cached = m_resultCache[hash];
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        
        // 从配置管理器获取缓存过期时间（符合千万级产品标准化要求）
        ConfigManager& config = ConfigManager::getInstance();
        int cacheExpireMs = config.getCacheExpireMs();
        if (now - cached.timestamp < cacheExpireMs) {
            // 从缓存返回
            QueryResult result;
            result.requestId = request.id;
            result.success = true;
            result.rows = cached.rows;
            result.count = cached.count;
            result.elapsedMs = 0;
            result.error = "";
            
            // 延迟发射信号，避免在锁内发射
            QTimer::singleShot(0, this, [this, result]() {
                emit queryCompleted(result.requestId, result);
            });
            
            return request.id;
        }
    }
    
    // 检查是否已有相同查询在执行
    if (m_pendingQueries.contains(hash)) {
        m_pendingQueries[hash].insert(request.id);
        // 等待已有查询完成
        return request.id;
    }
    
    // 添加到队列
    m_pendingQueries[hash].insert(request.id);
    m_queue.enqueue(request);
    m_queueCondition.wakeOne();
    
    return request.id;
}

void QueryQueue::setDatabase(QSqlDatabase *database)
{
    m_database = database;
}

void QueryQueue::startQueue()
{
    if (m_running) {
        return;
    }
    
    m_running = true;
    start();
}

void QueryQueue::stopQueue()
{
    if (!m_running) {
        return;
    }
    
    m_running = false;
    m_queueCondition.wakeAll();
    wait(3000);  // 等待3秒
}

void QueryQueue::clearQueue()
{
    QMutexLocker locker(&m_queueMutex);
    m_queue.clear();
    m_pendingQueries.clear();
}

void QueryQueue::clearCache()
{
    QMutexLocker locker(&m_queueMutex);
    m_resultCache.clear();
}

/**
 * @brief 生成查询的哈希值
 * 
 * 用于查询去重和缓存：
 * - 相同SQL和参数的查询生成相同哈希
 * - 用于识别重复查询，避免重复执行
 * - 作为缓存键，快速查找缓存结果
 * 
 * @param request 查询请求
 * @return MD5哈希值（十六进制字符串）
 */
QString QueryQueue::hashQuery(const QueryRequest &request) const
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(request.sql.toUtf8());
    
    // 包含绑定参数，确保相同SQL+参数生成相同哈希
    for (const QVariant &value : request.bindValues) {
        hash.addData(value.toString().toUtf8());
    }
    
    return hash.result().toHex();
}

QueryQueue::QueryResult QueryQueue::executeQuery(const QueryRequest &request)
{
    QueryResult result;
    result.requestId = request.id;
    result.success = false;
    
    if (!m_database || !m_database->isOpen()) {
        result.error = "数据库未连接";
        return result;
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QSqlQuery query(*m_database);
    query.prepare(request.sql);
    query.setForwardOnly(true);  // 优化性能
    
    // 绑定参数
    for (int i = 0; i < request.bindValues.size(); ++i) {
        query.bindValue(i, request.bindValues[i]);
    }
    
    if (query.exec()) {
        result.success = true;
        
        if (request.isCountQuery) {
            if (query.next()) {
                result.count = query.value(0).toLongLong();
            }
        } else {
            while (query.next()) {
                QStringList row;
                int columnCount = query.record().count();
                for (int i = 0; i < columnCount; ++i) {
                    row << query.value(i).toString();
                }
                result.rows << row;
            }
        }
        
        result.elapsedMs = timer.elapsed();
    } else {
        result.error = query.lastError().text();
        result.elapsedMs = timer.elapsed();
    }
    
    return result;
}

void QueryQueue::run()
{
    while (m_running) {
        QueryRequest request;
        QString hash;
        
        {
            QMutexLocker locker(&m_queueMutex);
            
            if (m_queue.isEmpty()) {
                m_queueCondition.wait(&m_queueMutex, 1000);  // 等待1秒或唤醒
                continue;
            }
            
            request = m_queue.dequeue();
            hash = hashQuery(request);
        }
        
        // 执行查询
        QueryResult result = executeQuery(request);
        
        // 缓存结果
        if (result.success && m_cacheEnabled) {
            QMutexLocker locker(&m_queueMutex);
            
            // 限制缓存大小（LRU策略：删除最旧的缓存）
            // 从配置管理器获取最大缓存大小
            int maxCacheSize = DatabaseConstants::MAX_CACHE_SIZE;  // 最大缓存条目数保持常量
            if (m_resultCache.size() >= maxCacheSize) {
                // 删除最旧的缓存
                qint64 oldestTime = QDateTime::currentMSecsSinceEpoch();
                QString oldestKey;
                
                for (auto it = m_resultCache.begin(); it != m_resultCache.end(); ++it) {
                    if (it.value().timestamp < oldestTime) {
                        oldestTime = it.value().timestamp;
                        oldestKey = it.key();
                    }
                }
                
                if (!oldestKey.isEmpty()) {
                    m_resultCache.remove(oldestKey);
                }
            }
            
            CachedResult cached;
            cached.rows = result.rows;
            cached.count = result.count;
            cached.timestamp = QDateTime::currentMSecsSinceEpoch();
            m_resultCache[hash] = cached;
        }
        
        // 通知所有等待相同查询的请求
        QSet<int> waitingIds;
        {
            QMutexLocker locker(&m_queueMutex);
            waitingIds = m_pendingQueries.take(hash);
        }
        
        // 在锁外发射信号
        for (int waitingId : waitingIds) {
            if (waitingId == request.id) {
                emit queryCompleted(request.id, result);
            } else {
                // 为其他等待的请求发送缓存结果
                QueryResult cachedResult = result;
                cachedResult.requestId = waitingId;
                emit queryCompleted(waitingId, cachedResult);
            }
        }
    }
}

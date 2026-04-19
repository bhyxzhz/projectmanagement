#ifndef PERFORMANCEMONITOR_H
#define PERFORMANCEMONITOR_H

#include <QDialog>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QProgressBar>
#include <QTextEdit>

/**
 * @brief 性能监控面板
 * 
 * 功能：
 * 1. 实时监控查询性能
 * 2. 缓存命中率统计
 * 3. 内存使用监控
 * 4. 数据库操作统计
 */
class PerformanceMonitor : public QDialog
{
    Q_OBJECT

public:
    explicit PerformanceMonitor(QWidget *parent = nullptr);
    ~PerformanceMonitor();

    // 性能指标更新
    void recordQuery(const QString& queryType, qint64 elapsedMs, bool fromCache = false);
    void updateMemoryUsage(qint64 memoryBytes);
    void updateDatabaseStats(int totalRecords, qint64 dbSizeBytes);
    void recordOperation(const QString& operation, qint64 elapsedMs);

public slots:
    void refreshStats();
    void clearStats();
    void exportStats();

private slots:
    void onRefreshTimer();

private:
    void setupUI();
    void updateUI();
    
    // UI组件
    QLabel *m_queryCountLabel;
    QLabel *m_cacheHitRateLabel;
    QLabel *m_avgQueryTimeLabel;
    QLabel *m_memoryUsageLabel;
    QLabel *m_databaseSizeLabel;
    QLabel *m_totalRecordsLabel;
    
    QTableWidget *m_queryStatsTable;
    QTableWidget *m_operationStatsTable;
    QTextEdit *m_recentQueriesLog;
    
    QPushButton *m_refreshButton;
    QPushButton *m_clearButton;
    QPushButton *m_exportButton;
    QPushButton *m_closeButton;
    
    QTimer *m_refreshTimer;
    
    // 统计数据
    struct QueryStat {
        QString type;
        int count;
        qint64 totalTime;
        qint64 minTime;
        qint64 maxTime;
        int cacheHits;
    };
    
    QHash<QString, QueryStat> m_queryStats;
    QHash<QString, qint64> m_operationStats;
    
    qint64 m_totalQueries;
    qint64 m_totalCacheHits;
    qint64 m_totalQueryTime;
    qint64 m_currentMemoryUsage;
    qint64 m_databaseSize;
    int m_totalRecords;
    
    // 最近查询日志（使用全局常量定义最大条目数）
    QStringList m_recentQueries;
};

#endif // PERFORMANCEMONITOR_H

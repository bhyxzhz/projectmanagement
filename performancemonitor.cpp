#include "performancemonitor.h"
#include "appconstants.h"
#include <QHeaderView>
#include <QDateTime>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QStandardPaths>
#include <QGridLayout>
#include <QStringConverter>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#include <QTextStream>
#include <QRegExp>
#endif

PerformanceMonitor::PerformanceMonitor(QWidget *parent)
    : QDialog(parent)
    , m_totalQueries(0)
    , m_totalCacheHits(0)
    , m_totalQueryTime(0)
    , m_currentMemoryUsage(0)
    , m_databaseSize(0)
    , m_totalRecords(0)
{
    setWindowTitle("性能监控面板");
    setMinimumSize(800, 600);
    resize(1000, 700);
    
    setupUI();
    
    // 设置自动刷新定时器（按指定间隔刷新性能数据）
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &PerformanceMonitor::onRefreshTimer);
    m_refreshTimer->start(PerformanceConstants::REFRESH_INTERVAL_MS);
}

PerformanceMonitor::~PerformanceMonitor()
{
}

void PerformanceMonitor::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // 统计信息区域
    QGroupBox *statsGroup = new QGroupBox("实时统计", this);
    QGridLayout *statsLayout = new QGridLayout(statsGroup);
    
    m_queryCountLabel = new QLabel("总查询次数: 0");
    m_cacheHitRateLabel = new QLabel("缓存命中率: 0%");
    m_avgQueryTimeLabel = new QLabel("平均查询时间: 0ms");
    m_memoryUsageLabel = new QLabel("内存使用: 0 MB");
    m_databaseSizeLabel = new QLabel("数据库大小: 0 MB");
    m_totalRecordsLabel = new QLabel("总记录数: 0");
    
    statsLayout->addWidget(m_queryCountLabel, 0, 0);
    statsLayout->addWidget(m_cacheHitRateLabel, 0, 1);
    statsLayout->addWidget(m_avgQueryTimeLabel, 0, 2);
    statsLayout->addWidget(m_memoryUsageLabel, 1, 0);
    statsLayout->addWidget(m_databaseSizeLabel, 1, 1);
    statsLayout->addWidget(m_totalRecordsLabel, 1, 2);
    
    mainLayout->addWidget(statsGroup);
    
    // 查询统计表格
    QGroupBox *queryStatsGroup = new QGroupBox("查询性能统计", this);
    QVBoxLayout *queryStatsLayout = new QVBoxLayout(queryStatsGroup);
    
    m_queryStatsTable = new QTableWidget(this);
    m_queryStatsTable->setColumnCount(7);  // 修复：应该是7列，包括"缓存命中"
    m_queryStatsTable->setHorizontalHeaderLabels({
        "查询类型", "执行次数", "总耗时(ms)", "平均耗时(ms)", "最小耗时(ms)", "最大耗时(ms)", "缓存命中"
    });
    m_queryStatsTable->horizontalHeader()->setStretchLastSection(true);
    m_queryStatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queryStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    queryStatsLayout->addWidget(m_queryStatsTable);
    
    mainLayout->addWidget(queryStatsGroup);
    
    // 操作统计表格
    QGroupBox *operationStatsGroup = new QGroupBox("操作性能统计", this);
    QVBoxLayout *operationStatsLayout = new QVBoxLayout(operationStatsGroup);
    
    m_operationStatsTable = new QTableWidget(this);
    m_operationStatsTable->setColumnCount(2);
    m_operationStatsTable->setHorizontalHeaderLabels({"操作类型", "平均耗时(ms)"});
    m_operationStatsTable->horizontalHeader()->setStretchLastSection(true);
    m_operationStatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_operationStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    operationStatsLayout->addWidget(m_operationStatsTable);
    
    mainLayout->addWidget(operationStatsGroup);
    
    // 最近查询日志
    QGroupBox *logGroup = new QGroupBox("最近查询日志", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    
    m_recentQueriesLog = new QTextEdit(this);
    m_recentQueriesLog->setReadOnly(true);
    m_recentQueriesLog->setMaximumHeight(150);
    m_recentQueriesLog->setFont(QFont("Consolas", 9));
    logLayout->addWidget(m_recentQueriesLog);
    
    mainLayout->addWidget(logGroup);
    
    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    m_refreshButton = new QPushButton("刷新", this);
    m_clearButton = new QPushButton("清空统计", this);
    m_exportButton = new QPushButton("导出统计", this);
    m_closeButton = new QPushButton("关闭", this);
    
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_clearButton);
    buttonLayout->addWidget(m_exportButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // 连接信号槽
    connect(m_refreshButton, &QPushButton::clicked, this, &PerformanceMonitor::refreshStats);
    connect(m_clearButton, &QPushButton::clicked, this, &PerformanceMonitor::clearStats);
    connect(m_exportButton, &QPushButton::clicked, this, &PerformanceMonitor::exportStats);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    
    updateUI();
}

void PerformanceMonitor::recordQuery(const QString& queryType, qint64 elapsedMs, bool fromCache)
{
    QueryStat& stat = m_queryStats[queryType];
    stat.type = queryType;
    stat.count++;
    stat.totalTime += elapsedMs;
    
    if (stat.minTime == 0 || elapsedMs < stat.minTime) {
        stat.minTime = elapsedMs;
    }
    if (elapsedMs > stat.maxTime) {
        stat.maxTime = elapsedMs;
    }
    
    if (fromCache) {
        stat.cacheHits++;
        m_totalCacheHits++;
    }
    
    m_totalQueries++;
    m_totalQueryTime += elapsedMs;
    
    // 记录到日志
    QString logEntry = QString("[%1] %2: %3ms %4")
                       .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                       .arg(queryType)
                       .arg(elapsedMs)
                       .arg(fromCache ? "(缓存)" : "");
    m_recentQueries.prepend(logEntry);
    
    // 限制日志条目数（FIFO策略：保留最近的N条日志）
    if (m_recentQueries.size() > PerformanceConstants::MAX_LOG_ENTRIES) {
        m_recentQueries.removeLast();
    }
    
    // 更新UI（如果可见）
    if (isVisible()) {
        updateUI();
    }
}

void PerformanceMonitor::updateMemoryUsage(qint64 memoryBytes)
{
    m_currentMemoryUsage = memoryBytes;
    if (isVisible()) {
        updateUI();
    }
}

void PerformanceMonitor::updateDatabaseStats(int totalRecords, qint64 dbSizeBytes)
{
    m_totalRecords = totalRecords;
    m_databaseSize = dbSizeBytes;
    if (isVisible()) {
        updateUI();
    }
}

void PerformanceMonitor::recordOperation(const QString& operation, qint64 elapsedMs)
{
    // 计算移动平均
    if (m_operationStats.contains(operation)) {
        qint64 currentAvg = m_operationStats[operation];
        m_operationStats[operation] = (currentAvg + elapsedMs) / 2;  // 简单移动平均
    } else {
        m_operationStats[operation] = elapsedMs;
    }
    
    if (isVisible()) {
        updateUI();
    }
}

void PerformanceMonitor::refreshStats()
{
    updateUI();
}

void PerformanceMonitor::clearStats()
{
    int ret = QMessageBox::question(this, "确认", "确定要清空所有统计数据吗？",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        m_queryStats.clear();
        m_operationStats.clear();
        m_recentQueries.clear();
        m_totalQueries = 0;
        m_totalCacheHits = 0;
        m_totalQueryTime = 0;
        updateUI();
    }
}

void PerformanceMonitor::exportStats()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "导出性能统计",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/性能统计.txt",
        "文本文件 (*.txt);;所有文件 (*.*)"
    );
    
    if (fileName.isEmpty()) {
        return;
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法创建文件: " + file.errorString());
        return;
    }
    
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    out << "性能统计报告\n";
    out << "生成时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "========================================\n\n";
    
    out << "总体统计:\n";
    out << "  总查询次数: " << m_totalQueries << "\n";
    out << "  缓存命中次数: " << m_totalCacheHits << "\n";
    out << "  缓存命中率: " << QString::number(m_totalQueries > 0 ? (m_totalCacheHits * 100.0 / m_totalQueries) : 0, 'f', 2) << "%\n";
    out << "  平均查询时间: " << (m_totalQueries > 0 ? (m_totalQueryTime / m_totalQueries) : 0) << "ms\n";
    out << "  内存使用: " << (m_currentMemoryUsage / 1024.0 / 1024.0) << " MB\n";
    out << "  数据库大小: " << (m_databaseSize / 1024.0 / 1024.0) << " MB\n";
    out << "  总记录数: " << m_totalRecords << "\n\n";
    
    out << "查询类型统计:\n";
    for (auto it = m_queryStats.begin(); it != m_queryStats.end(); ++it) {
        const QueryStat& stat = it.value();
        out << "  " << stat.type << ":\n";
        out << "    执行次数: " << stat.count << "\n";
        out << "    总耗时: " << stat.totalTime << "ms\n";
        out << "    平均耗时: " << (stat.count > 0 ? (stat.totalTime / stat.count) : 0) << "ms\n";
        out << "    最小耗时: " << stat.minTime << "ms\n";
        out << "    最大耗时: " << stat.maxTime << "ms\n";
        out << "    缓存命中: " << stat.cacheHits << " (" 
            << (stat.count > 0 ? (stat.cacheHits * 100.0 / stat.count) : 0) << "%)\n\n";
    }
    
    out << "操作性能统计:\n";
    for (auto it = m_operationStats.begin(); it != m_operationStats.end(); ++it) {
        out << "  " << it.key() << ": " << it.value() << "ms\n";
    }
    
    file.close();
    QMessageBox::information(this, "成功", "性能统计已导出到: " + fileName);
}

void PerformanceMonitor::onRefreshTimer()
{
    // 更新内存使用（如果可见）
    if (isVisible()) {
        // 获取当前进程的内存使用
        #ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            updateMemoryUsage(pmc.WorkingSetSize);
        }
        #elif defined(Q_OS_LINUX)
        QFile file("/proc/self/status");
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.startsWith("VmRSS:")) {
                    QStringList parts = line.split(QRegExp("\\s+"));
                    if (parts.size() >= 2) {
                        bool ok;
                        qint64 memoryKB = parts[1].toLongLong(&ok);
                        if (ok) {
                            updateMemoryUsage(memoryKB * 1024); // 转换为字节
                        }
                    }
                    break;
                }
            }
        }
        #elif defined(Q_OS_MAC)
        // macOS 内存监控需要特殊处理
        #endif
        updateUI();
    }
}

void PerformanceMonitor::updateUI()
{
    // 更新统计标签
    m_queryCountLabel->setText(QString("总查询次数: %1").arg(m_totalQueries));
    
    double cacheHitRate = m_totalQueries > 0 ? (m_totalCacheHits * 100.0 / m_totalQueries) : 0;
    m_cacheHitRateLabel->setText(QString("缓存命中率: %1%").arg(cacheHitRate, 0, 'f', 1));
    
    qint64 avgQueryTime = m_totalQueries > 0 ? (m_totalQueryTime / m_totalQueries) : 0;
    m_avgQueryTimeLabel->setText(QString("平均查询时间: %1ms").arg(avgQueryTime));
    
    m_memoryUsageLabel->setText(QString("内存使用: %1 MB")
                                 .arg(m_currentMemoryUsage / 1024.0 / 1024.0, 0, 'f', 2));
    
    m_databaseSizeLabel->setText(QString("数据库大小: %1 MB")
                                 .arg(m_databaseSize / 1024.0 / 1024.0, 0, 'f', 2));
    
    m_totalRecordsLabel->setText(QString("总记录数: %1").arg(m_totalRecords));
    
    // 更新查询统计表格
    m_queryStatsTable->setRowCount(m_queryStats.size());
    int row = 0;
    for (auto it = m_queryStats.begin(); it != m_queryStats.end(); ++it, ++row) {
        const QueryStat& stat = it.value();
        m_queryStatsTable->setItem(row, 0, new QTableWidgetItem(stat.type));
        m_queryStatsTable->setItem(row, 1, new QTableWidgetItem(QString::number(stat.count)));
        m_queryStatsTable->setItem(row, 2, new QTableWidgetItem(QString::number(stat.totalTime)));
        
        qint64 avgTime = stat.count > 0 ? (stat.totalTime / stat.count) : 0;
        m_queryStatsTable->setItem(row, 3, new QTableWidgetItem(QString::number(avgTime)));
        m_queryStatsTable->setItem(row, 4, new QTableWidgetItem(QString::number(stat.minTime)));
        m_queryStatsTable->setItem(row, 5, new QTableWidgetItem(QString::number(stat.maxTime)));
        m_queryStatsTable->setItem(row, 6, new QTableWidgetItem(
            QString("%1 (%2%)").arg(stat.cacheHits)
                               .arg(stat.count > 0 ? (stat.cacheHits * 100.0 / stat.count) : 0, 0, 'f', 1)));
    }
    
    // 更新操作统计表格
    m_operationStatsTable->setRowCount(m_operationStats.size());
    row = 0;
    for (auto it = m_operationStats.begin(); it != m_operationStats.end(); ++it, ++row) {
        m_operationStatsTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_operationStatsTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
    }
    
    // 更新日志
    m_recentQueriesLog->clear();
    m_recentQueriesLog->setPlainText(m_recentQueries.join("\n"));
}



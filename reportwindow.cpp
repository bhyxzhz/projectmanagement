#include "reportwindow.h"
#include "reportservice.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QColor>
#include <QBrush>
#include <QPen>
#include <QFont>
#include <QFontMetrics>
#include <QDebug>
#include <QPrinter>
#include <QPrintDialog>
#include <QPageSize>
#include <QToolBar>
#include <QGroupBox>
#include <QDateEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QSet>
#include <QList>
#include <algorithm>
#include <limits>
#include <cmath>
#include <QTimer>
#include <QApplication>
#include <QStatusBar>
#include <QThread>
#include <QProgressBar>
#include "configmanager.h"
#include "databasemanager.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QScrollArea>

ReportWindow::ReportWindow(ReportService *reportService, QWidget *parent)
    : QMainWindow(parent)
    , m_reportService(reportService)
    , m_dataLoadThread(nullptr)
    , m_dataWorker(nullptr)
{
    setWindowTitle("项目报表");
    setMinimumSize(1000, 700);
    resize(1200, 800);
    
    setupUI();
    
    // 记录打开报表窗口的操作（异常保护）
    try {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "打开项目报表窗口", QString(), "成功");
    } catch (...) {
        qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
    }
    
    // 初始化后台数据加载线程（异常处理）
    try {
        m_dataLoadThread = new QThread(this);
        m_dataWorker = new ReportDataWorker(reportService); // reportService可以为null，Worker会自己创建数据库连接
        m_dataWorker->moveToThread(m_dataLoadThread);
        
        // 连接信号槽
        connect(m_dataWorker, &ReportDataWorker::overviewDataLoaded,
                this, &ReportWindow::onOverviewDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::statusDataLoaded,
                this, &ReportWindow::onStatusDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::managerDataLoaded,
                this, &ReportWindow::onManagerDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::budgetDataLoaded,
                this, &ReportWindow::onBudgetDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::trendDataLoaded,
                this, &ReportWindow::onTrendDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::chartsDataLoaded,
                this, &ReportWindow::onChartsDataLoaded, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::progressUpdated,
                this, &ReportWindow::onDataLoadProgress, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::loadFinished,
                this, &ReportWindow::onDataLoadFinished, Qt::QueuedConnection);
        connect(m_dataWorker, &ReportDataWorker::loadError,
                this, &ReportWindow::onDataLoadError, Qt::QueuedConnection);
        
        // 启动线程
        m_dataLoadThread->start();
        
        qDebug() << "报表窗口：后台数据加载线程已启动";
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：初始化线程失败:" << e.what();
        // 如果线程初始化失败，设置为nullptr，避免后续访问崩溃
        if (m_dataWorker) {
            delete m_dataWorker;
            m_dataWorker = nullptr;
        }
        if (m_dataLoadThread) {
            delete m_dataLoadThread;
            m_dataLoadThread = nullptr;
        }
    } catch (...) {
        qDebug() << "报表窗口：初始化线程时发生未知错误";
        if (m_dataWorker) {
            delete m_dataWorker;
            m_dataWorker = nullptr;
        }
        if (m_dataLoadThread) {
            delete m_dataLoadThread;
            m_dataLoadThread = nullptr;
        }
    }
    
    // 注意：不在构造函数中刷新数据，避免阻塞UI线程
    // 数据刷新将在窗口显示后由MainWindow调用
}

ReportWindow::~ReportWindow()
{
    // 停止数据加载线程（线程资源管理）
    try {
        if (m_dataWorker) {
            m_dataWorker->stopLoading();
        }
        
        if (m_dataLoadThread) {
            if (m_dataLoadThread->isRunning()) {
                m_dataLoadThread->quit();
                if (!m_dataLoadThread->wait(3000)) { // 等待最多3秒
                    qDebug() << "报表窗口：线程未在3秒内退出，强制终止";
                    m_dataLoadThread->terminate();
                    m_dataLoadThread->wait(1000); // 再等待1秒
                }
            }
        }
        
        // 注意：m_dataWorker 的父对象是 m_dataLoadThread，当线程被删除时会自动删除
        // 但为了安全，我们显式删除
        if (m_dataWorker) {
            // 先将对象移回主线程，避免在已停止的线程中删除
            m_dataWorker->moveToThread(QThread::currentThread());
            delete m_dataWorker;
            m_dataWorker = nullptr;
        }
        
        // 清理线程对象（如果线程已经停止）
        if (m_dataLoadThread) {
            if (!m_dataLoadThread->isRunning()) {
                delete m_dataLoadThread;
                m_dataLoadThread = nullptr;
            }
        }
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：析构函数中发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：析构函数中发生未知异常";
    }
}

void ReportWindow::setupUI()
{
    // 创建状态栏和进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // 不确定进度模式
    m_progressBar->setVisible(false);
    statusBar()->addPermanentWidget(m_progressBar);
    
    // 创建工具栏
    QToolBar *toolBar = addToolBar("报表工具");
    toolBar->setMovable(false);
    
    m_refreshButton = new QPushButton("刷新", this);
    m_refreshButton->setToolTip("刷新报表数据");
    connect(m_refreshButton, &QPushButton::clicked, this, &ReportWindow::refreshReport);
    toolBar->addWidget(m_refreshButton);
    
    toolBar->addSeparator();
    
    m_exportPDFButton = new QPushButton("导出PDF", this);
    m_exportPDFButton->setToolTip("导出报表为PDF格式");
    connect(m_exportPDFButton, &QPushButton::clicked, this, &ReportWindow::exportToPDF);
    toolBar->addWidget(m_exportPDFButton);
    
    m_exportExcelButton = new QPushButton("导出Excel", this);
    m_exportExcelButton->setToolTip("导出报表为Excel格式");
    connect(m_exportExcelButton, &QPushButton::clicked, this, &ReportWindow::exportToExcel);
    toolBar->addWidget(m_exportExcelButton);
    
    m_exportCSVButton = new QPushButton("导出CSV", this);
    m_exportCSVButton->setToolTip("导出报表为CSV格式");
    connect(m_exportCSVButton, &QPushButton::clicked, this, &ReportWindow::exportToCSV);
    toolBar->addWidget(m_exportCSVButton);
    
    // 创建标签页
    m_tabWidget = new QTabWidget(this);
    setCentralWidget(m_tabWidget);
    
    createOverviewTab();
    createStatisticsTab();
    createTrendTab();
    createChartsTab();
}

void ReportWindow::createOverviewTab()
{
    m_overviewTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(m_overviewTab);
    
    // 关键指标区域
    QGroupBox *metricsGroup = new QGroupBox("关键指标", this);
    QHBoxLayout *metricsLayout = new QHBoxLayout(metricsGroup);
    
    m_totalProjectsLabel = new QLabel("项目总数: 0", this);
    m_totalProjectsLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 10px;");
    metricsLayout->addWidget(m_totalProjectsLabel);
    
    m_totalBudgetLabel = new QLabel("预算总额: ¥0.00", this);
    m_totalBudgetLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 10px;");
    metricsLayout->addWidget(m_totalBudgetLabel);
    
    m_averageBudgetLabel = new QLabel("平均预算: ¥0.00", this);
    m_averageBudgetLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 10px;");
    metricsLayout->addWidget(m_averageBudgetLabel);
    
    m_completionRateLabel = new QLabel("完成率: 0%", this);
    m_completionRateLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 10px;");
    metricsLayout->addWidget(m_completionRateLabel);
    
    layout->addWidget(metricsGroup);
    
    // 概览表格
    m_overviewTable = new QTableWidget(this);
    m_overviewTable->setColumnCount(2);
    m_overviewTable->setHorizontalHeaderLabels(QStringList() << "指标" << "数值");
    m_overviewTable->horizontalHeader()->setStretchLastSection(true);
    m_overviewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_overviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    layout->addWidget(m_overviewTable);
    
    m_tabWidget->addTab(m_overviewTab, "概览");
}

void ReportWindow::createStatisticsTab()
{
    m_statisticsTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(m_statisticsTab);
    
    // 状态统计表格
    QGroupBox *statusGroup = new QGroupBox("项目状态统计", this);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    m_statusTable = new QTableWidget(this);
    m_statusTable->setColumnCount(3);
    m_statusTable->setHorizontalHeaderLabels(QStringList() << "状态" << "数量" << "占比");
    m_statusTable->horizontalHeader()->setStretchLastSection(true);
    m_statusTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statusLayout->addWidget(m_statusTable);
    layout->addWidget(statusGroup);
    
    // 经理统计表格
    QGroupBox *managerGroup = new QGroupBox("项目经理统计", this);
    QVBoxLayout *managerLayout = new QVBoxLayout(managerGroup);
    m_managerTable = new QTableWidget(this);
    m_managerTable->setColumnCount(3);
    m_managerTable->setHorizontalHeaderLabels(QStringList() << "经理" << "项目数" << "预算总额");
    m_managerTable->horizontalHeader()->setStretchLastSection(true);
    m_managerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    managerLayout->addWidget(m_managerTable);
    layout->addWidget(managerGroup);
    
    // 预算统计表格
    QGroupBox *budgetGroup = new QGroupBox("预算统计", this);
    QVBoxLayout *budgetLayout = new QVBoxLayout(budgetGroup);
    m_budgetTable = new QTableWidget(this);
    m_budgetTable->setColumnCount(2);
    m_budgetTable->setHorizontalHeaderLabels(QStringList() << "统计项" << "数值");
    m_budgetTable->horizontalHeader()->setStretchLastSection(true);
    m_budgetTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    budgetLayout->addWidget(m_budgetTable);
    layout->addWidget(budgetGroup);
    
    m_tabWidget->addTab(m_statisticsTab, "统计分析");
}

void ReportWindow::createTrendTab()
{
    m_trendTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(m_trendTab);
    
    // 日期范围选择
    QGroupBox *dateGroup = new QGroupBox("时间范围", this);
    QHBoxLayout *dateLayout = new QHBoxLayout(dateGroup);
    
    dateLayout->addWidget(new QLabel("开始日期:", this));
    m_startDateEdit = new QDateEdit(this);
    m_startDateEdit->setDate(QDate::currentDate().addMonths(-6));
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDisplayFormat("yyyy-MM-dd");
    connect(m_startDateEdit, &QDateEdit::dateChanged, this, &ReportWindow::onDateRangeChanged);
    dateLayout->addWidget(m_startDateEdit);
    
    dateLayout->addWidget(new QLabel("结束日期:", this));
    m_endDateEdit = new QDateEdit(this);
    m_endDateEdit->setDate(QDate::currentDate());
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDisplayFormat("yyyy-MM-dd");
    connect(m_endDateEdit, &QDateEdit::dateChanged, this, &ReportWindow::onDateRangeChanged);
    dateLayout->addWidget(m_endDateEdit);
    
    dateLayout->addWidget(new QLabel("分组方式:", this));
    m_groupByCombo = new QComboBox(this);
    m_groupByCombo->addItems(QStringList() << "日" << "周" << "月" << "年");
    m_groupByCombo->setCurrentText("月");
    connect(m_groupByCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ReportWindow::onDateRangeChanged);
    dateLayout->addWidget(m_groupByCombo);
    
    dateLayout->addStretch();
    layout->addWidget(dateGroup);
    
    // 趋势表格
    m_trendTable = new QTableWidget(this);
    m_trendTable->setColumnCount(4);
    m_trendTable->setHorizontalHeaderLabels(QStringList() << "时间" << "项目数" << "预算总额" << "平均预算");
    m_trendTable->horizontalHeader()->setStretchLastSection(true);
    m_trendTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_trendTable);
    
    m_tabWidget->addTab(m_trendTab, "趋势分析");
}

void ReportWindow::createChartsTab()
{
    m_chartsTab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(m_chartsTab);
    
    // 状态分布图表
    QGroupBox *statusChartGroup = new QGroupBox("项目状态分布", this);
    QVBoxLayout *statusChartLayout = new QVBoxLayout(statusChartGroup);
    m_statusChartLabel = new QLabel(this);
    m_statusChartLabel->setMinimumHeight(300);
    m_statusChartLabel->setAlignment(Qt::AlignCenter);
    m_statusChartLabel->setStyleSheet("border: 1px solid gray; background-color: white;");
    statusChartLayout->addWidget(m_statusChartLabel);
    layout->addWidget(statusChartGroup);
    
    // 经理分布图表（水平柱状图，支持滚动）
    QGroupBox *managerChartGroup = new QGroupBox("项目经理分布", this);
    QVBoxLayout *managerChartLayout = new QVBoxLayout(managerChartGroup);
    
    // 创建滚动区域
    m_managerChartScrollArea = new QScrollArea(this);
    m_managerChartScrollArea->setWidgetResizable(true);
    m_managerChartScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_managerChartScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_managerChartScrollArea->setMinimumHeight(300);
    m_managerChartScrollArea->setStyleSheet("border: 1px solid gray;");
    
    // 创建图表标签（将作为滚动区域的内容）
    m_managerChartLabel = new QLabel(this);
    m_managerChartLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_managerChartLabel->setStyleSheet("background-color: white;");
    m_managerChartLabel->setMinimumWidth(600); // 设置最小宽度
    
    m_managerChartScrollArea->setWidget(m_managerChartLabel);
    managerChartLayout->addWidget(m_managerChartScrollArea);
    layout->addWidget(managerChartGroup);
    
    // 导出图表按钮
    m_exportChartButton = new QPushButton("导出图表", this);
    connect(m_exportChartButton, &QPushButton::clicked, this, &ReportWindow::exportChart);
    layout->addWidget(m_exportChartButton);
    
    m_tabWidget->addTab(m_chartsTab, "图表展示");
}

void ReportWindow::refreshReport()
{
    if (!m_dataWorker) {
        QMessageBox::warning(this, "错误", "报表数据加载器未初始化");
        // 记录错误日志
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "刷新报表数据", QString(), "失败：数据加载器未初始化");
        return;
    }
    
    if (!m_dataLoadThread || !m_dataLoadThread->isRunning()) {
        QMessageBox::warning(this, "错误", "报表数据加载线程未运行");
        // 记录错误日志
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "刷新报表数据", QString(), "失败：数据加载线程未运行");
        return;
    }
    
    // 记录刷新操作日志（异常保护）
    try {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "刷新报表数据", QString(), "成功");
    } catch (...) {
        qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
    }
    
    // 显示加载提示（空指针检查）
    if (m_progressBar) {
        m_progressBar->setVisible(true);
    }
    if (statusBar()) {
        statusBar()->showMessage("正在加载报表数据...", 0);
    }
    if (m_refreshButton) {
        m_refreshButton->setEnabled(false); // 禁用刷新按钮，避免重复点击
    }
    
    // 在后台线程中异步加载数据（异常保护）
    try {
        if (m_dataWorker) {
            QMetaObject::invokeMethod(m_dataWorker, "loadOverviewData", Qt::QueuedConnection);
            QMetaObject::invokeMethod(m_dataWorker, "loadStatisticsData", Qt::QueuedConnection);
            
            // 加载趋势数据（使用缓存的日期范围）
            QDate startDate = m_startDateEdit ? m_startDateEdit->date() : QDate::currentDate().addMonths(-6);
            QDate endDate = m_endDateEdit ? m_endDateEdit->date() : QDate::currentDate();
            QString groupBy = m_groupByCombo ? m_groupByCombo->currentText() : "月";
            QString groupByKey = (groupBy == "日") ? "day" : (groupBy == "周") ? "week" : (groupBy == "月") ? "month" : "year";
            QMetaObject::invokeMethod(m_dataWorker, "loadTrendData", Qt::QueuedConnection,
                                      Q_ARG(QDate, startDate),
                                      Q_ARG(QDate, endDate),
                                      Q_ARG(QString, groupByKey));
            
            QMetaObject::invokeMethod(m_dataWorker, "loadChartsData", Qt::QueuedConnection);
        }
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：调用后台加载方法时发生异常:" << e.what();
        if (m_progressBar) {
            m_progressBar->setVisible(false);
        }
        if (m_refreshButton) {
            m_refreshButton->setEnabled(true);
        }
        QMessageBox::warning(this, "错误", QString("启动数据加载失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "报表窗口：调用后台加载方法时发生未知异常";
        if (m_progressBar) {
            m_progressBar->setVisible(false);
        }
        if (m_refreshButton) {
            m_refreshButton->setEnabled(true);
        }
        QMessageBox::warning(this, "错误", "启动数据加载时发生未知错误");
    }
}

void ReportWindow::updateOverviewReport()
{
    if (!m_reportService) return;
    
    QMap<QString, QVariant> report = m_reportService->getOverviewReport();
    
    // 更新关键指标
    int totalProjects = report["totalProjects"].toInt();
    double totalBudget = report["totalBudget"].toDouble();
    double averageBudget = report["averageBudget"].toDouble();
    double completionRate = report["completionRate"].toDouble();
    
    m_totalProjectsLabel->setText(QString("项目总数: %1").arg(totalProjects));
    m_totalBudgetLabel->setText(QString("预算总额: ¥%1").arg(totalBudget, 0, 'f', 2));
    m_averageBudgetLabel->setText(QString("平均预算: ¥%1").arg(averageBudget, 0, 'f', 2));
    m_completionRateLabel->setText(QString("完成率: %1%").arg(completionRate, 0, 'f', 1));
    
    // 更新概览表格
    m_overviewTable->setRowCount(0);
    
    QStringList labels = {
        "项目总数", "预算总额", "平均预算", "完成率", 
        "延期项目数", "进行中项目", "已完成项目"
    };
    
    QList<QVariant> values = {
        totalProjects,
        QString("¥%1").arg(totalBudget, 0, 'f', 2),
        QString("¥%1").arg(averageBudget, 0, 'f', 2),
        QString("%1%").arg(completionRate, 0, 'f', 1),
        m_reportService->getDelayedProjectsCount(),
        0, // 进行中项目数（需要从状态统计获取）
        0  // 已完成项目数（需要从状态统计获取）
    };
    
    QMap<QString, int> statusStats = m_reportService->getStatusStatistics();
    for (auto it = statusStats.begin(); it != statusStats.end(); ++it) {
        if (it.key().contains("进行") || it.key().contains("进行中")) {
            values[5] = it.value();
        }
        if (it.key().contains("完成") || it.key().contains("已完成")) {
            values[6] = it.value();
        }
    }
    
    for (int i = 0; i < labels.size(); ++i) {
        int row = m_overviewTable->rowCount();
        m_overviewTable->insertRow(row);
        m_overviewTable->setItem(row, 0, new QTableWidgetItem(labels[i]));
        m_overviewTable->setItem(row, 1, new QTableWidgetItem(values[i].toString()));
    }
    
    m_overviewTable->resizeColumnsToContents();
}

void ReportWindow::updateStatisticsReport()
{
    if (!m_reportService) return;
    
    // 更新状态统计
    QMap<QString, int> statusStats = m_reportService->getStatusStatistics();
    QMap<QString, double> statusPercentages = m_reportService->getStatusPercentage();
    
    m_statusTable->setRowCount(statusStats.size());
    int row = 0;
    for (auto it = statusStats.begin(); it != statusStats.end(); ++it, ++row) {
        m_statusTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_statusTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
        double percentage = statusPercentages.value(it.key(), 0.0);
        m_statusTable->setItem(row, 2, new QTableWidgetItem(QString("%1%").arg(percentage, 0, 'f', 1)));
    }
    m_statusTable->resizeColumnsToContents();
    
    // 更新经理统计
    QMap<QString, int> managerStats = m_reportService->getManagerStatistics();
    QMap<QString, double> managerBudgets = m_reportService->getManagerBudgetStatistics();
    
    m_managerTable->setRowCount(managerStats.size());
    row = 0;
    for (auto it = managerStats.begin(); it != managerStats.end(); ++it, ++row) {
        m_managerTable->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_managerTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
        double budget = managerBudgets.value(it.key(), 0.0);
        m_managerTable->setItem(row, 2, new QTableWidgetItem(QString("¥%1").arg(budget, 0, 'f', 2)));
    }
    m_managerTable->resizeColumnsToContents();
    
    // 更新预算统计
    QMap<QString, double> budgetStats = m_reportService->getBudgetStatistics();
    m_budgetTable->setRowCount(budgetStats.size());
    
    QStringList budgetLabels = {"最小值", "最大值", "平均值", "中位数"};
    QStringList budgetKeys = {"min", "max", "avg", "median"};
    
    for (int i = 0; i < budgetLabels.size(); ++i) {
        m_budgetTable->setItem(i, 0, new QTableWidgetItem(budgetLabels[i]));
        double value = budgetStats.value(budgetKeys[i], 0.0);
        m_budgetTable->setItem(i, 1, new QTableWidgetItem(QString("¥%1").arg(value, 0, 'f', 2)));
    }
    m_budgetTable->resizeColumnsToContents();
}

void ReportWindow::updateTrendReport()
{
    if (!m_reportService) return;
    
    QDate startDate = m_startDateEdit->date();
    QDate endDate = m_endDateEdit->date();
    
    if (startDate > endDate) {
        QMessageBox::warning(this, "错误", "开始日期不能晚于结束日期");
        return;
    }
    
    QString groupBy = m_groupByCombo->currentText();
    QString groupByKey;
    if (groupBy == "日") groupByKey = "day";
    else if (groupBy == "周") groupByKey = "week";
    else if (groupBy == "月") groupByKey = "month";
    else groupByKey = "year";
    
    QMap<QString, int> projectsByDate = m_reportService->getProjectsByCreateDate(startDate, endDate, groupByKey);
    QMap<QString, double> budgetTrend = m_reportService->getBudgetTrend(startDate, endDate, groupByKey);
    
    // 合并数据
    QSet<QString> allDates;
    for (auto it = projectsByDate.begin(); it != projectsByDate.end(); ++it) {
        allDates.insert(it.key());
    }
    for (auto it = budgetTrend.begin(); it != budgetTrend.end(); ++it) {
        allDates.insert(it.key());
    }
    
    QList<QString> sortedDates = allDates.values();
    std::sort(sortedDates.begin(), sortedDates.end());
    
    m_trendTable->setRowCount(sortedDates.size());
    
    for (int i = 0; i < sortedDates.size(); ++i) {
        QString date = sortedDates[i];
        int count = projectsByDate.value(date, 0);
        double budget = budgetTrend.value(date, 0.0);
        double avgBudget = count > 0 ? budget / count : 0.0;
        
        m_trendTable->setItem(i, 0, new QTableWidgetItem(date));
        m_trendTable->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
        m_trendTable->setItem(i, 2, new QTableWidgetItem(QString("¥%1").arg(budget, 0, 'f', 2)));
        m_trendTable->setItem(i, 3, new QTableWidgetItem(QString("¥%1").arg(avgBudget, 0, 'f', 2)));
    }
    
    m_trendTable->resizeColumnsToContents();
}

void ReportWindow::updateCharts()
{
    if (!m_reportService) return;
    
    // 绘制状态分布饼图
    QMap<QString, int> statusStats = m_reportService->getStatusStatistics();
    QPixmap statusPixmap(m_statusChartLabel->size());
    statusPixmap.fill(Qt::white);
    QPainter statusPainter(&statusPixmap);
    drawPieChart(statusPainter, statusPixmap.rect(), statusStats, "项目状态分布");
    m_statusChartLabel->setPixmap(statusPixmap);
    
    // 绘制经理分布柱状图（水平方向，支持滚动）
    QMap<QString, int> managerStats = m_reportService->getManagerStatistics();
    // 根据项目经理数量动态计算图表高度
    int barCount = managerStats.size();
    int barHeight = 30;  // 每个柱子的高度
    int spacing = 10;    // 柱子之间的间距
    int chartHeight = barCount * (barHeight + spacing) + 100; // 总高度：柱子高度 + 标题和边距
    int chartWidth = 600; // 固定宽度
    
    QSize managerSize(chartWidth, chartHeight);
    QPixmap managerPixmap(managerSize);
    managerPixmap.fill(Qt::white);
    QPainter managerPainter(&managerPixmap);
    if (managerPainter.isActive()) {
        drawBarChart(managerPainter, managerPixmap.rect(), managerStats, "项目经理分布");
        m_managerChartLabel->setPixmap(managerPixmap);
        // 设置标签的固定大小，使其可以滚动
        m_managerChartLabel->setFixedSize(chartWidth, chartHeight);
    }
}

void ReportWindow::drawBarChart(QPainter &painter, const QRect &rect, const QMap<QString, int> &data, const QString &title)
{
    if (data.isEmpty()) {
        painter.drawText(rect, Qt::AlignCenter, "暂无数据");
        return;
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制标题
    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRect titleRect = QRect(rect.x(), rect.y(), rect.width(), 30);
    painter.drawText(titleRect, Qt::AlignCenter, title);
    
    // 计算绘图区域（水平柱状图：左侧留空间给标签，右侧留空间给数值）
    int leftMargin = 120;  // 左侧留空间给项目经理名称
    int rightMargin = 60;  // 右侧留空间给数值标签
    int topMargin = 50;
    int bottomMargin = 20;
    QRect chartRect = rect.adjusted(leftMargin, topMargin, -rightMargin, -bottomMargin);
    
    // 找出最大值
    int maxValue = 0;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.value() > maxValue) {
            maxValue = it.value();
        }
    }
    
    if (maxValue == 0) {
        painter.drawText(chartRect, Qt::AlignCenter, "暂无数据");
        return;
    }
    
    // 绘制坐标轴（水平柱状图：X轴在底部，Y轴在左侧）
    painter.setPen(QPen(Qt::black, 2));
    painter.drawLine(chartRect.left(), chartRect.bottom(), chartRect.right(), chartRect.bottom()); // X轴
    painter.drawLine(chartRect.left(), chartRect.top(), chartRect.left(), chartRect.bottom()); // Y轴
    
    // 绘制水平柱状图
    int barCount = data.size();
    int barHeight = 30;  // 每个柱子的高度
    int spacing = 10;    // 柱子之间的间距
    int y = chartRect.top() + spacing;
    
    QList<QColor> colors = {Qt::blue, Qt::green, Qt::red, Qt::yellow, Qt::cyan, Qt::magenta};
    int colorIndex = 0;
    
    QFont labelFont("Arial", 9);
    QFontMetrics labelMetrics(labelFont);
    painter.setFont(labelFont);
    
    for (auto it = data.begin(); it != data.end(); ++it) {
        int value = it.value();
        int barWidth = (value * chartRect.width()) / maxValue;
        
        // 绘制柱子（水平方向）
        QRect barRect(chartRect.left(), y, barWidth, barHeight);
        painter.setBrush(QBrush(colors[colorIndex % colors.size()]));
        painter.setPen(QPen(Qt::black, 1));
        painter.drawRect(barRect);
        
        // 绘制项目经理名称标签（在柱子左侧）
        QString managerName = it.key();
        QRect nameRect(rect.left() + 10, y, leftMargin - 20, barHeight);
        painter.setPen(QPen(Qt::black));
        painter.drawText(nameRect, Qt::AlignRight | Qt::AlignVCenter, managerName);
        
        // 绘制数值标签（在柱子右侧）
        QString valueText = QString::number(value);
        QRect valueRect(barRect.right() + 5, y, rightMargin - 10, barHeight);
        painter.setPen(QPen(Qt::black));
        painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, valueText);
        
        y += barHeight + spacing;
        colorIndex++;
    }
}

void ReportWindow::drawPieChart(QPainter &painter, const QRect &rect, const QMap<QString, int> &data, const QString &title)
{
    if (data.isEmpty()) {
        painter.drawText(rect, Qt::AlignCenter, "暂无数据");
        return;
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制标题
    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRect titleRect = QRect(rect.x(), rect.y(), rect.width(), 30);
    painter.drawText(titleRect, Qt::AlignCenter, title);
    
    // 计算总和
    int total = 0;
    for (auto it = data.begin(); it != data.end(); ++it) {
        total += it.value();
    }
    
    if (total == 0) {
        painter.drawText(rect.adjusted(0, 30, 0, 0), Qt::AlignCenter, "暂无数据");
        return;
    }
    
    // 计算饼图区域 - 靠左显示，右侧留出空间给图例
    int leftMargin = 50;  // 左侧边距
    int rightMargin = 300; // 右侧预留空间给图例（根据图例项数量调整）
    int topMargin = 50;
    int bottomMargin = 50;
    
    // 饼图靠左绘制
    int availableWidth = rect.width() - leftMargin - rightMargin;
    int availableHeight = rect.height() - topMargin - bottomMargin;
    int pieSize = qMin(availableWidth, availableHeight);
    
    // 饼图靠左对齐
    QRect pieRect(leftMargin, topMargin, pieSize, pieSize);
    
    // 绘制饼图
    double startAngle = 0;
    QList<QColor> colors = {Qt::blue, Qt::green, Qt::red, Qt::yellow, Qt::cyan, Qt::magenta, Qt::gray};
    int colorIndex = 0;
    
    // 图例位置：在饼图右侧，有足够空间
    int legendX = pieRect.right() + 30; // 饼图右侧30像素处开始
    int legendY = pieRect.top() + 20;    // 从饼图顶部稍微下移开始
    
    for (auto it = data.begin(); it != data.end(); ++it) {
        double angle = (it.value() * 360.0 * 16) / total;
        
        // 绘制扇形
        QColor color = colors[colorIndex % colors.size()];
        painter.setBrush(QBrush(color));
        painter.setPen(QPen(Qt::white, 2));
        painter.drawPie(pieRect, startAngle, angle);
        
        // 绘制图例
        QRect legendRect(legendX, legendY, 15, 15);
        painter.setBrush(QBrush(color));
        painter.setPen(QPen(Qt::black));
        painter.drawRect(legendRect);
        
        double percentage = (it.value() * 100.0) / total;
        QString legendText = QString("%1 (%2%)").arg(it.key()).arg(percentage, 0, 'f', 1);
        painter.setPen(QPen(Qt::black));
        QFont legendFont("Arial", 10); // 稍微增大字体，确保可读性
        legendFont.setBold(false);
        painter.setFont(legendFont);
        // 确保图例文本有足够空间显示（至少250像素宽度）
        QRect textRect = legendRect.adjusted(20, 0, 250, 0);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, legendText);
        
        startAngle += angle;
        legendY += 25;
        colorIndex++;
    }
}

void ReportWindow::drawLineChart(QPainter &painter, const QRect &rect, const QMap<QString, double> &data, const QString &title)
{
    if (data.isEmpty()) {
        painter.drawText(rect, Qt::AlignCenter, "暂无数据");
        return;
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制标题
    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRect titleRect = QRect(rect.x(), rect.y(), rect.width(), 30);
    painter.drawText(titleRect, Qt::AlignCenter, title);
    
    // 计算绘图区域
    QRect chartRect = rect.adjusted(50, 50, -20, -50);
    
    // 找出最大值和最小值
    double maxValue = 0;
    double minValue = std::numeric_limits<double>::max();
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.value() > maxValue) maxValue = it.value();
        if (it.value() < minValue) minValue = it.value();
    }
    
    if (maxValue == minValue) {
        painter.drawText(chartRect, Qt::AlignCenter, "暂无数据");
        return;
    }
    
    // 绘制坐标轴
    painter.setPen(QPen(Qt::black, 2));
    painter.drawLine(chartRect.left(), chartRect.bottom(), chartRect.right(), chartRect.bottom());
    painter.drawLine(chartRect.left(), chartRect.top(), chartRect.left(), chartRect.bottom());
    
    // 绘制折线
    QList<QString> sortedKeys = data.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    
    QList<QPointF> points;
    int xStep = chartRect.width() / (sortedKeys.size() + 1);
    int x = chartRect.left() + xStep;
    
    for (const QString &key : sortedKeys) {
        double value = data[key];
        int y = chartRect.bottom() - ((value - minValue) * chartRect.height() / (maxValue - minValue));
        points.append(QPointF(x, y));
        x += xStep;
    }
    
    // 绘制折线
    painter.setPen(QPen(Qt::blue, 2));
    for (int i = 0; i < points.size() - 1; ++i) {
        painter.drawLine(points[i], points[i + 1]);
    }
    
    // 绘制数据点
    painter.setBrush(QBrush(Qt::blue));
    for (const QPointF &point : points) {
        painter.drawEllipse(point, 4, 4);
    }
}

void ReportWindow::onDateRangeChanged()
{
    if (!m_dataWorker || !m_dataLoadThread || !m_dataLoadThread->isRunning()) {
        return;
    }
    
    QDate startDate = m_startDateEdit->date();
    QDate endDate = m_endDateEdit->date();
    
    if (startDate > endDate) {
        QMessageBox::warning(this, "错误", "开始日期不能晚于结束日期");
        // 记录错误日志（异常保护）
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", "修改趋势分析日期范围", QString(), "失败：开始日期晚于结束日期");
        } catch (...) {
            qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
        }
        return;
    }
    
    QString groupBy = m_groupByCombo->currentText();
    QString groupByKey = (groupBy == "日") ? "day" : (groupBy == "周") ? "week" : (groupBy == "月") ? "month" : "year";
    
    // 记录修改日期范围操作日志
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    QString dateRange = QString("%1 至 %2 (%3)").arg(startDate.toString("yyyy-MM-dd"))
                                                  .arg(endDate.toString("yyyy-MM-dd"))
                                                  .arg(groupBy);
    dbManager.logOperation("报表操作", QString("修改趋势分析日期范围: %1").arg(dateRange), QString(), "成功");
    
    // 在后台线程中加载趋势数据
    statusBar()->showMessage("正在加载趋势数据...", 0);
    QMetaObject::invokeMethod(m_dataWorker, "loadTrendData", Qt::QueuedConnection,
                              Q_ARG(QDate, startDate),
                              Q_ARG(QDate, endDate),
                              Q_ARG(QString, groupByKey));
}

void ReportWindow::exportToPDF()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出PDF", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/项目报表.pdf",
        "PDF文件 (*.pdf)");
    
    if (fileName.isEmpty()) {
        // 用户取消操作，不记录日志
        return;
    }
    
    // 记录导出PDF操作日志（异常保护）
    try {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", QString("导出PDF报表到: %1").arg(fileName), QString(), "成功");
    } catch (...) {
        qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
    }
    
    exportToPDFFile(fileName);
}

void ReportWindow::exportToPDFFile(const QString &filePath)
{
    // 空指针检查
    if (!m_overviewTable || !m_statusTable || !m_managerTable || !m_budgetTable) {
        QMessageBox::critical(this, "错误", "报表数据未加载，无法导出PDF");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出PDF报表到: %1").arg(filePath), QString(), "失败：数据未加载");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
        return;
    }
    
    try {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(filePath);
        printer.setPageSize(QPageSize::A4);
        
        QTextDocument document;
        QString html = "<html><head><meta charset='UTF-8'>";
    html += "<style>";
    html += "body { font-family: 'Microsoft YaHei', Arial, sans-serif; margin: 20px; }";
    html += "h1 { color: #2c3e50; border-bottom: 3px solid #3498db; padding-bottom: 10px; }";
    html += "h2 { color: #34495e; margin-top: 30px; margin-bottom: 15px; }";
    html += "table { width: 100%; border-collapse: collapse; margin-bottom: 30px; }";
    html += "th { background-color: #3498db; color: white; padding: 10px; text-align: left; font-weight: bold; }";
    html += "td { padding: 8px; border: 1px solid #ddd; }";
    html += "tr:nth-child(even) { background-color: #f2f2f2; }";
    html += "tr:hover { background-color: #e8f4f8; }";
    html += ".page-break { page-break-after: always; }";
    html += "</style></head><body>";
    
    html += "<h1>项目报表</h1>";
    html += "<p style='color: #7f8c8d;'>生成时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>";
    
        // 添加概览数据
        html += "<h2>一、概览</h2>";
        html += "<table border='1' cellpadding='5'>";
        html += "<tr><th>指标</th><th>数值</th></tr>";
        
        if (m_overviewTable) {
            for (int i = 0; i < m_overviewTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_overviewTable->item(i, 0);
                QTableWidgetItem *item1 = m_overviewTable->item(i, 1);
                if (item0 && item1) {
                    html += "<tr>";
                    html += "<td>" + item0->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item1->text().toHtmlEscaped() + "</td>";
                    html += "</tr>";
                }
            }
        }
        html += "</table>";
        
        // 添加项目状态统计
        html += "<h2>二、项目状态统计</h2>";
        html += "<table border='1' cellpadding='5'>";
        html += "<tr><th>状态</th><th>数量</th><th>占比</th></tr>";
        
        if (m_statusTable) {
            for (int i = 0; i < m_statusTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_statusTable->item(i, 0);
                QTableWidgetItem *item1 = m_statusTable->item(i, 1);
                QTableWidgetItem *item2 = m_statusTable->item(i, 2);
                if (item0 && item1 && item2) {
                    html += "<tr>";
                    html += "<td>" + item0->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item1->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item2->text().toHtmlEscaped() + "</td>";
                    html += "</tr>";
                }
            }
        }
        html += "</table>";
        
        // 添加项目经理统计
        html += "<h2>三、项目经理统计</h2>";
        html += "<table border='1' cellpadding='5'>";
        html += "<tr><th>经理</th><th>项目数</th><th>预算总额</th></tr>";
        
        if (m_managerTable) {
            for (int i = 0; i < m_managerTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_managerTable->item(i, 0);
                QTableWidgetItem *item1 = m_managerTable->item(i, 1);
                QTableWidgetItem *item2 = m_managerTable->item(i, 2);
                if (item0 && item1 && item2) {
                    html += "<tr>";
                    html += "<td>" + item0->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item1->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item2->text().toHtmlEscaped() + "</td>";
                    html += "</tr>";
                }
            }
        }
        html += "</table>";
        
        // 添加预算统计
        html += "<h2>四、预算统计</h2>";
        html += "<table border='1' cellpadding='5'>";
        html += "<tr><th>统计项</th><th>数值</th></tr>";
        
        if (m_budgetTable) {
            for (int i = 0; i < m_budgetTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_budgetTable->item(i, 0);
                QTableWidgetItem *item1 = m_budgetTable->item(i, 1);
                if (item0 && item1) {
                    html += "<tr>";
                    html += "<td>" + item0->text().toHtmlEscaped() + "</td>";
                    html += "<td>" + item1->text().toHtmlEscaped() + "</td>";
                    html += "</tr>";
                }
            }
        }
        html += "</table>";
    
    // 添加项目状态分布图表
    QMap<QString, int> statusStatsForChart;
    QMap<QString, double> statusPercentagesForChart;
    
    // 获取状态统计数据（用于图表和图例）
    if (m_statusTable && m_statusTable->rowCount() > 0) {
        for (int i = 0; i < m_statusTable->rowCount(); ++i) {
            QTableWidgetItem *item0 = m_statusTable->item(i, 0); // 状态
            QTableWidgetItem *item1 = m_statusTable->item(i, 1); // 数量
            QTableWidgetItem *item2 = m_statusTable->item(i, 2); // 占比
            if (item0 && item1 && item2) {
                QString status = item0->text();
                int count = item1->text().toInt();
                QString percentageStr = item2->text();
                percentageStr.remove('%');
                double percentage = percentageStr.toDouble();
                
                statusStatsForChart[status] = count;
                statusPercentagesForChart[status] = percentage;
            }
        }
    }
    
    // 如果表格数据为空，尝试从服务获取
    if (statusStatsForChart.isEmpty() && m_reportService) {
        statusStatsForChart = m_reportService->getStatusStatistics();
        statusPercentagesForChart = m_reportService->getStatusPercentage();
    }
    
    if (!statusStatsForChart.isEmpty()) {
        html += "<h2>五、项目状态分布图</h2>";
        
        // 生成图表（确保有足够大的尺寸来显示图例）
        QPixmap statusChart;
        if (m_statusChartLabel) {
            statusChart = m_statusChartLabel->pixmap();
        }
        
        // 如果图表为空或太小，重新生成一个更大的图表
        // 确保宽度足够：饼图(400) + 间距(30) + 图例(250) = 至少680，加上边距需要至少800
        if (statusChart.isNull() || statusChart.width() < 900) {
            // 生成足够大的图表：饼图靠左，右侧有足够空间显示完整图例
            // 宽度：左侧边距(50) + 饼图(450) + 间距(30) + 图例(300) + 右侧边距(20) = 850
            // 高度：顶部边距(50) + 饼图(450) + 底部边距(50) = 550
            QSize chartSize(900, 550);
            statusChart = QPixmap(chartSize);
            statusChart.fill(Qt::white);
            QPainter painter(&statusChart);
            painter.setRenderHint(QPainter::Antialiasing);
            drawPieChart(painter, statusChart.rect(), statusStatsForChart, "项目状态分布");
        }
        
        if (!statusChart.isNull()) {
            // 将图表转换为base64编码，嵌入HTML
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            
            // 转换为QImage并保存为PNG格式
            QImage chartImage = statusChart.toImage();
            if (chartImage.save(&buffer, "PNG")) {
                QString base64Image = imageData.toBase64();
                // 图表靠左对齐，确保右侧图例完整显示
                html += "<div style='text-align: left; margin: 20px 0; overflow-x: auto;'>";
                html += "<img src='data:image/png;base64," + base64Image + "' style='max-width: 100%; height: auto; border: 1px solid #ddd; display: block;' />";
                html += "</div>";
            }
        }
        
        // 添加图例数据表格（确保即使图片中的图例看不到，也能在PDF中看到）
        html += "<h3 style='color: #34495e; margin-top: 20px; margin-bottom: 10px;'>图例说明</h3>";
        html += "<table border='1' cellpadding='8' style='width: 100%; margin-bottom: 30px;'>";
        html += "<tr><th style='background-color: #3498db; color: white;'>状态</th><th style='background-color: #3498db; color: white;'>数量</th><th style='background-color: #3498db; color: white;'>占比</th></tr>";
        
        // 按占比从大到小排序显示
        QList<QPair<QString, double>> sortedStatus;
        for (auto it = statusPercentagesForChart.begin(); it != statusPercentagesForChart.end(); ++it) {
            sortedStatus.append(qMakePair(it.key(), it.value()));
        }
        std::sort(sortedStatus.begin(), sortedStatus.end(), 
                  [](const QPair<QString, double> &a, const QPair<QString, double> &b) {
                      return a.second > b.second;
                  });
        
        for (const auto &pair : sortedStatus) {
            QString status = pair.first;
            int count = statusStatsForChart.value(status, 0);
            double percentage = pair.second;
            
            html += "<tr>";
            html += "<td><strong>" + status + "</strong></td>";
            html += "<td>" + QString::number(count) + "</td>";
            html += "<td>" + QString("%1%").arg(percentage, 0, 'f', 1) + "</td>";
            html += "</tr>";
        }
        html += "</table>";
    }
    
        html += "</body></html>";
        
        document.setHtml(html);
        document.print(&printer);
        
        // 日志已在exportToPDF()中记录，这里不再重复记录
        QMessageBox::information(this, "成功", "PDF报表已导出到: " + filePath);
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：导出PDF时发生异常:" << e.what();
        QMessageBox::critical(this, "错误", QString("导出PDF失败: %1").arg(e.what()));
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出PDF报表到: %1").arg(filePath), QString(), QString("失败：%1").arg(e.what()));
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    } catch (...) {
        qDebug() << "报表窗口：导出PDF时发生未知异常";
        QMessageBox::critical(this, "错误", "导出PDF时发生未知错误");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出PDF报表到: %1").arg(filePath), QString(), "失败：未知错误");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    }
}

void ReportWindow::exportToExcel()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出Excel", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/项目报表.csv",
        "CSV文件 (*.csv)");
    
    if (fileName.isEmpty()) {
        // 用户取消操作，不记录日志
        return;
    }
    
    // 空指针检查
    if (!m_overviewTable || !m_statusTable || !m_managerTable || !m_budgetTable || !m_trendTable) {
        QMessageBox::critical(this, "错误", "报表数据未加载，无法导出Excel");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出Excel报表到: %1").arg(fileName), QString(), "失败：数据未加载");
        } catch (...) {
            qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
        }
        return;
    }
    
    try {
        // 由于Excel格式较复杂，这里导出为CSV格式（Excel可以打开）
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "错误", "无法创建文件: " + fileName);
            // 记录失败日志
            try {
                DatabaseManager& dbManager = DatabaseManager::getInstance();
                dbManager.logOperation("报表操作", QString("导出Excel报表到: %1").arg(fileName), QString(), "失败：无法创建文件");
            } catch (...) {
                qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
            }
            return;
        }
        
        // 记录导出Excel操作日志（异常保护）
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出Excel报表到: %1").arg(fileName), QString(), "成功");
        } catch (...) {
            qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
        }
        
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        
        // 写入BOM头（Excel识别UTF-8）
        out << "\xEF\xBB\xBF";
        
        // ========== 一、概览 ==========
        out << "项目报表\n";
        out << "生成时间," << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n\n";
        out << "一、概览\n";
        
        // 导出概览表格表头
        if (m_overviewTable->columnCount() >= 2) {
            QTableWidgetItem *header0 = m_overviewTable->horizontalHeaderItem(0);
            QTableWidgetItem *header1 = m_overviewTable->horizontalHeaderItem(1);
            if (header0 && header1) {
                out << header0->text() << "," << header1->text() << "\n";
            } else {
                out << "指标,数值\n";
            }
        }
        
        // 导出概览表格数据
        for (int i = 0; i < m_overviewTable->rowCount(); ++i) {
            QTableWidgetItem *item0 = m_overviewTable->item(i, 0);
            QTableWidgetItem *item1 = m_overviewTable->item(i, 1);
            if (item0 && item1) {
                out << "\"" << item0->text() << "\",";
                out << "\"" << item1->text() << "\"\n";
            }
        }
        
        out << "\n";
        
        // ========== 二、统计分析 ==========
        out << "二、统计分析\n\n";
        
        // 2.1 项目状态统计
        out << "2.1 项目状态统计\n";
        if (m_statusTable && m_statusTable->rowCount() > 0) {
            // 导出表头
            if (m_statusTable->columnCount() >= 3) {
                QTableWidgetItem *header0 = m_statusTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_statusTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_statusTable->horizontalHeaderItem(2);
                if (header0 && header1 && header2) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" << header2->text() << "\"\n";
                } else {
                    out << "\"状态\",\"数量\",\"占比\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_statusTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_statusTable->item(i, 0);
                QTableWidgetItem *item1 = m_statusTable->item(i, 1);
                QTableWidgetItem *item2 = m_statusTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 2.2 项目经理统计
        out << "2.2 项目经理统计\n";
        if (m_managerTable && m_managerTable->rowCount() > 0) {
            // 导出表头
            if (m_managerTable->columnCount() >= 3) {
                QTableWidgetItem *header0 = m_managerTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_managerTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_managerTable->horizontalHeaderItem(2);
                if (header0 && header1 && header2) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" << header2->text() << "\"\n";
                } else {
                    out << "\"经理\",\"项目数\",\"预算总额\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_managerTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_managerTable->item(i, 0);
                QTableWidgetItem *item1 = m_managerTable->item(i, 1);
                QTableWidgetItem *item2 = m_managerTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 2.3 预算统计
        out << "2.3 预算统计\n";
        if (m_budgetTable && m_budgetTable->rowCount() > 0) {
            // 导出表头
            if (m_budgetTable->columnCount() >= 2) {
                QTableWidgetItem *header0 = m_budgetTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_budgetTable->horizontalHeaderItem(1);
                if (header0 && header1) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\"\n";
                } else {
                    out << "\"统计项\",\"数值\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_budgetTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_budgetTable->item(i, 0);
                QTableWidgetItem *item1 = m_budgetTable->item(i, 1);
                if (item0 && item1) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // ========== 三、趋势分析 ==========
        out << "三、趋势分析\n";
        if (m_trendTable && m_trendTable->rowCount() > 0) {
            // 导出表头
            if (m_trendTable->columnCount() >= 4) {
                QTableWidgetItem *header0 = m_trendTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_trendTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_trendTable->horizontalHeaderItem(2);
                QTableWidgetItem *header3 = m_trendTable->horizontalHeaderItem(3);
                if (header0 && header1 && header2 && header3) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" 
                        << header2->text() << "\",\"" << header3->text() << "\"\n";
                } else {
                    out << "\"时间\",\"项目数\",\"预算总额\",\"平均预算\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_trendTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_trendTable->item(i, 0);
                QTableWidgetItem *item1 = m_trendTable->item(i, 1);
                QTableWidgetItem *item2 = m_trendTable->item(i, 2);
                QTableWidgetItem *item3 = m_trendTable->item(i, 3);
                if (item0 && item1 && item2 && item3) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\",";
                    out << "\"" << item3->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // ========== 四、图表展示数据 ==========
        out << "四、图表展示数据\n\n";
        
        // 4.1 项目状态分布数据（从状态表格获取）
        out << "4.1 项目状态分布数据\n";
        if (m_statusTable && m_statusTable->rowCount() > 0) {
            out << "\"状态\",\"数量\",\"占比\"\n";
            for (int i = 0; i < m_statusTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_statusTable->item(i, 0);
                QTableWidgetItem *item1 = m_statusTable->item(i, 1);
                QTableWidgetItem *item2 = m_statusTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 4.2 项目经理分布数据（从经理表格获取）
        out << "4.2 项目经理分布数据\n";
        if (m_managerTable && m_managerTable->rowCount() > 0) {
            out << "\"经理\",\"项目数\"\n";
            for (int i = 0; i < m_managerTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_managerTable->item(i, 0);
                QTableWidgetItem *item1 = m_managerTable->item(i, 1);
                if (item0 && item1) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\"\n";
                }
            }
        }
        
        file.close();
        QMessageBox::information(this, "成功", "报表已导出到: " + fileName);
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：导出Excel时发生异常:" << e.what();
        QMessageBox::critical(this, "错误", QString("导出Excel失败: %1").arg(e.what()));
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出Excel报表到: %1").arg(fileName), QString(), QString("失败：%1").arg(e.what()));
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    } catch (...) {
        qDebug() << "报表窗口：导出Excel时发生未知异常";
        QMessageBox::critical(this, "错误", "导出Excel时发生未知错误");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出Excel报表到: %1").arg(fileName), QString(), "失败：未知错误");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    }
}

void ReportWindow::exportToCSV()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出CSV", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/项目报表.csv",
        "CSV文件 (*.csv)");
    
    if (fileName.isEmpty()) {
        // 用户取消操作，不记录日志
        return;
    }
    
    // 空指针检查
    if (!m_overviewTable || !m_statusTable || !m_managerTable || !m_budgetTable || !m_trendTable) {
        QMessageBox::critical(this, "错误", "报表数据未加载，无法导出CSV");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(fileName), QString(), "失败：数据未加载");
        } catch (...) {
            qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
        }
        return;
    }
    
    try {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "错误", "无法创建文件: " + fileName);
            // 记录失败日志
            try {
                DatabaseManager& dbManager = DatabaseManager::getInstance();
                dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(fileName), QString(), "失败：无法创建文件");
            } catch (...) {
                qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
            }
            return;
        }
        
        // 记录导出CSV操作日志（异常保护）
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(fileName), QString(), "成功");
        } catch (...) {
            qDebug() << "报表窗口：记录日志时发生异常（可忽略）";
        }
        
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "\xEF\xBB\xBF"; // UTF-8 BOM
        
        // ========== 一、概览 ==========
        out << "项目报表\n";
        out << "生成时间," << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n\n";
        out << "一、概览\n";
        
        // 导出概览表格表头
        if (m_overviewTable->columnCount() >= 2) {
            QTableWidgetItem *header0 = m_overviewTable->horizontalHeaderItem(0);
            QTableWidgetItem *header1 = m_overviewTable->horizontalHeaderItem(1);
            if (header0 && header1) {
                out << "\"" << header0->text() << "\",\"" << header1->text() << "\"\n";
            } else {
                out << "\"指标\",\"数值\"\n";
            }
        }
        
        // 导出概览表格数据
        for (int i = 0; i < m_overviewTable->rowCount(); ++i) {
            QTableWidgetItem *item0 = m_overviewTable->item(i, 0);
            QTableWidgetItem *item1 = m_overviewTable->item(i, 1);
            if (item0 && item1) {
                out << "\"" << item0->text() << "\",";
                out << "\"" << item1->text() << "\"\n";
            }
        }
        
        out << "\n";
        
        // ========== 二、统计分析 ==========
        out << "二、统计分析\n\n";
        
        // 2.1 项目状态统计
        out << "2.1 项目状态统计\n";
        if (m_statusTable && m_statusTable->rowCount() > 0) {
            // 导出表头
            if (m_statusTable->columnCount() >= 3) {
                QTableWidgetItem *header0 = m_statusTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_statusTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_statusTable->horizontalHeaderItem(2);
                if (header0 && header1 && header2) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" << header2->text() << "\"\n";
                } else {
                    out << "\"状态\",\"数量\",\"占比\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_statusTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_statusTable->item(i, 0);
                QTableWidgetItem *item1 = m_statusTable->item(i, 1);
                QTableWidgetItem *item2 = m_statusTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 2.2 项目经理统计
        out << "2.2 项目经理统计\n";
        if (m_managerTable && m_managerTable->rowCount() > 0) {
            // 导出表头
            if (m_managerTable->columnCount() >= 3) {
                QTableWidgetItem *header0 = m_managerTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_managerTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_managerTable->horizontalHeaderItem(2);
                if (header0 && header1 && header2) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" << header2->text() << "\"\n";
                } else {
                    out << "\"经理\",\"项目数\",\"预算总额\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_managerTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_managerTable->item(i, 0);
                QTableWidgetItem *item1 = m_managerTable->item(i, 1);
                QTableWidgetItem *item2 = m_managerTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 2.3 预算统计
        out << "2.3 预算统计\n";
        if (m_budgetTable && m_budgetTable->rowCount() > 0) {
            // 导出表头
            if (m_budgetTable->columnCount() >= 2) {
                QTableWidgetItem *header0 = m_budgetTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_budgetTable->horizontalHeaderItem(1);
                if (header0 && header1) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\"\n";
                } else {
                    out << "\"统计项\",\"数值\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_budgetTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_budgetTable->item(i, 0);
                QTableWidgetItem *item1 = m_budgetTable->item(i, 1);
                if (item0 && item1) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // ========== 三、趋势分析 ==========
        out << "三、趋势分析\n";
        if (m_trendTable && m_trendTable->rowCount() > 0) {
            // 导出表头
            if (m_trendTable->columnCount() >= 4) {
                QTableWidgetItem *header0 = m_trendTable->horizontalHeaderItem(0);
                QTableWidgetItem *header1 = m_trendTable->horizontalHeaderItem(1);
                QTableWidgetItem *header2 = m_trendTable->horizontalHeaderItem(2);
                QTableWidgetItem *header3 = m_trendTable->horizontalHeaderItem(3);
                if (header0 && header1 && header2 && header3) {
                    out << "\"" << header0->text() << "\",\"" << header1->text() << "\",\"" 
                        << header2->text() << "\",\"" << header3->text() << "\"\n";
                } else {
                    out << "\"时间\",\"项目数\",\"预算总额\",\"平均预算\"\n";
                }
            }
            
            // 导出数据
            for (int i = 0; i < m_trendTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_trendTable->item(i, 0);
                QTableWidgetItem *item1 = m_trendTable->item(i, 1);
                QTableWidgetItem *item2 = m_trendTable->item(i, 2);
                QTableWidgetItem *item3 = m_trendTable->item(i, 3);
                if (item0 && item1 && item2 && item3) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\",";
                    out << "\"" << item3->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // ========== 四、图表展示数据 ==========
        out << "四、图表展示数据\n\n";
        
        // 4.1 项目状态分布数据（从状态表格获取）
        out << "4.1 项目状态分布数据\n";
        if (m_statusTable && m_statusTable->rowCount() > 0) {
            out << "\"状态\",\"数量\",\"占比\"\n";
            for (int i = 0; i < m_statusTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_statusTable->item(i, 0);
                QTableWidgetItem *item1 = m_statusTable->item(i, 1);
                QTableWidgetItem *item2 = m_statusTable->item(i, 2);
                if (item0 && item1 && item2) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\",";
                    out << "\"" << item2->text() << "\"\n";
                }
            }
        }
        out << "\n";
        
        // 4.2 项目经理分布数据（从经理表格获取）
        out << "4.2 项目经理分布数据\n";
        if (m_managerTable && m_managerTable->rowCount() > 0) {
            out << "\"经理\",\"项目数\"\n";
            for (int i = 0; i < m_managerTable->rowCount(); ++i) {
                QTableWidgetItem *item0 = m_managerTable->item(i, 0);
                QTableWidgetItem *item1 = m_managerTable->item(i, 1);
                if (item0 && item1) {
                    out << "\"" << item0->text() << "\",";
                    out << "\"" << item1->text() << "\"\n";
                }
            }
        }
        
        file.close();
        QMessageBox::information(this, "成功", "CSV报表已导出到: " + fileName);
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：导出CSV时发生异常:" << e.what();
        QMessageBox::critical(this, "错误", QString("导出CSV失败: %1").arg(e.what()));
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(fileName), QString(), QString("失败：%1").arg(e.what()));
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    } catch (...) {
        qDebug() << "报表窗口：导出CSV时发生未知异常";
        QMessageBox::critical(this, "错误", "导出CSV时发生未知错误");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(fileName), QString(), "失败：未知错误");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    }
}

void ReportWindow::exportTableToCSV(QTableWidget *table, const QString &filePath)
{
    // 空指针检查
    if (!table) {
        QMessageBox::critical(this, "错误", "表格未初始化，无法导出CSV");
        return;
    }
    
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "错误", "无法创建文件: " + filePath);
            // 记录失败日志（如果是从exportToCSV调用的，这里会重复记录，但为了完整性还是记录）
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(filePath), QString(), "失败：无法创建文件");
            return;
        }
        
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "\xEF\xBB\xBF"; // UTF-8 BOM
        
        // 写入表头
        for (int col = 0; col < table->columnCount(); ++col) {
            if (col > 0) out << ",";
            QTableWidgetItem *headerItem = table->horizontalHeaderItem(col);
            if (headerItem) {
                out << "\"" << headerItem->text() << "\"";
            } else {
                out << "\"列" << (col + 1) << "\"";
            }
        }
        out << "\n";
        
        // 写入数据
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < table->columnCount(); ++col) {
                if (col > 0) out << ",";
                QTableWidgetItem *item = table->item(row, col);
                if (item) {
                    out << "\"" << item->text() << "\"";
                } else {
                    out << "\"\"";
                }
            }
            out << "\n";
        }
        
        file.close();
        QMessageBox::information(this, "成功", "CSV文件已导出到: " + filePath);
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：导出CSV时发生异常:" << e.what();
        QMessageBox::critical(this, "错误", QString("导出CSV失败: %1").arg(e.what()));
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(filePath), QString(), QString("失败：%1").arg(e.what()));
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    } catch (...) {
        qDebug() << "报表窗口：导出CSV时发生未知异常";
        QMessageBox::critical(this, "错误", "导出CSV时发生未知错误");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出CSV报表到: %1").arg(filePath), QString(), "失败：未知错误");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    }
}

void ReportWindow::exportChart()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出图表", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/图表.png",
        "图片文件 (*.png *.jpg)");
    
    if (fileName.isEmpty()) {
        // 用户取消操作，不记录日志
        return;
    }
    
    // 空指针检查
    if (!m_statusChartLabel) {
        QMessageBox::warning(this, "警告", "图表标签未初始化");
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "导出图表", QString(), "失败：图表标签未初始化");
        return;
    }
    
    try {
        QPixmap pixmap = m_statusChartLabel->pixmap();
        if (!pixmap.isNull()) {
            if (pixmap.save(fileName)) {
                // 记录导出图表操作日志
                DatabaseManager& dbManager = DatabaseManager::getInstance();
                dbManager.logOperation("报表操作", QString("导出图表到: %1").arg(fileName), QString(), "成功");
                QMessageBox::information(this, "成功", "图表已导出到: " + fileName);
            } else {
                // 记录失败日志
                DatabaseManager& dbManager = DatabaseManager::getInstance();
                dbManager.logOperation("报表操作", QString("导出图表到: %1").arg(fileName), QString(), "失败：保存文件失败");
                QMessageBox::critical(this, "错误", "保存图表文件失败");
            }
        } else {
            // 记录警告日志
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", "导出图表", QString(), "失败：没有可导出的图表");
            QMessageBox::warning(this, "警告", "没有可导出的图表");
        }
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：导出图表时发生异常:" << e.what();
        QMessageBox::critical(this, "错误", QString("导出图表失败: %1").arg(e.what()));
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出图表到: %1").arg(fileName), QString(), QString("失败：%1").arg(e.what()));
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    } catch (...) {
        qDebug() << "报表窗口：导出图表时发生未知异常";
        QMessageBox::critical(this, "错误", "导出图表时发生未知错误");
        try {
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", QString("导出图表到: %1").arg(fileName), QString(), "失败：未知错误");
        } catch (...) {
            qDebug() << "报表窗口：记录错误日志时发生异常";
        }
    }
}

// ========== 数据加载完成回调函数 ==========

void ReportWindow::onOverviewDataLoaded(int totalProjects, double totalBudget, double avgBudget,
                                        double completionRate, int delayedProjects,
                                        int inProgressProjects, int completedProjects)
{
    // 空指针检查
    if (!m_totalProjectsLabel || !m_totalBudgetLabel || !m_averageBudgetLabel || 
        !m_completionRateLabel || !m_overviewTable) {
        qDebug() << "报表窗口：UI组件未初始化，无法更新概览数据";
        return;
    }
    
    try {
        // 更新关键指标
        m_totalProjectsLabel->setText(QString("项目总数: %1").arg(totalProjects));
        m_totalBudgetLabel->setText(QString("预算总额: ¥%1").arg(totalBudget, 0, 'f', 2));
        m_averageBudgetLabel->setText(QString("平均预算: ¥%1").arg(avgBudget, 0, 'f', 2));
        m_completionRateLabel->setText(QString("完成率: %1%").arg(completionRate, 0, 'f', 1));
        
        // 更新概览表格
        m_overviewTable->setRowCount(0);
        
        QStringList labels = {
            "项目总数", "预算总额", "平均预算", "完成率", 
            "延期项目数", "进行中项目", "已完成项目"
        };
        
        QList<QVariant> values = {
            totalProjects,
            QString("¥%1").arg(totalBudget, 0, 'f', 2),
            QString("¥%1").arg(avgBudget, 0, 'f', 2),
            QString("%1%").arg(completionRate, 0, 'f', 1),
            delayedProjects,
            inProgressProjects,
            completedProjects
        };
        
        for (int i = 0; i < labels.size() && i < values.size(); ++i) {
            int row = m_overviewTable->rowCount();
            m_overviewTable->insertRow(row);
            m_overviewTable->setItem(row, 0, new QTableWidgetItem(labels[i]));
            m_overviewTable->setItem(row, 1, new QTableWidgetItem(values[i].toString()));
        }
        
        m_overviewTable->resizeColumnsToContents();
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：更新概览数据时发生异常:" << e.what();
        QMessageBox::warning(this, "错误", QString("更新概览数据失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "报表窗口：更新概览数据时发生未知异常";
        QMessageBox::warning(this, "错误", "更新概览数据时发生未知错误");
    }
}

void ReportWindow::onStatusDataLoaded(const QMap<QString, int> &statusStats,
                                     const QMap<QString, double> &statusPercentages)
{
    // 空指针检查
    if (!m_statusTable) {
        qDebug() << "报表窗口：状态表格未初始化";
        return;
    }
    
    try {
        m_statusTable->setRowCount(statusStats.size());
        int row = 0;
        for (auto it = statusStats.begin(); it != statusStats.end(); ++it, ++row) {
            m_statusTable->setItem(row, 0, new QTableWidgetItem(it.key()));
            m_statusTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
            double percentage = statusPercentages.value(it.key(), 0.0);
            m_statusTable->setItem(row, 2, new QTableWidgetItem(QString("%1%").arg(percentage, 0, 'f', 1)));
        }
        m_statusTable->resizeColumnsToContents();
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：更新状态数据时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：更新状态数据时发生未知异常";
    }
}

void ReportWindow::onManagerDataLoaded(const QMap<QString, int> &managerStats,
                                      const QMap<QString, double> &managerBudgets)
{
    // 空指针检查
    if (!m_managerTable) {
        qDebug() << "报表窗口：经理表格未初始化";
        return;
    }
    
    try {
        m_managerTable->setRowCount(managerStats.size());
        int row = 0;
        for (auto it = managerStats.begin(); it != managerStats.end(); ++it, ++row) {
            m_managerTable->setItem(row, 0, new QTableWidgetItem(it.key()));
            m_managerTable->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
            double budget = managerBudgets.value(it.key(), 0.0);
            m_managerTable->setItem(row, 2, new QTableWidgetItem(QString("¥%1").arg(budget, 0, 'f', 2)));
        }
        m_managerTable->resizeColumnsToContents();
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：更新经理数据时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：更新经理数据时发生未知异常";
    }
}

void ReportWindow::onBudgetDataLoaded(const QMap<QString, double> &budgetStats)
{
    // 空指针检查
    if (!m_budgetTable) {
        qDebug() << "报表窗口：预算表格未初始化";
        return;
    }
    
    try {
        QStringList budgetLabels = {"最小值", "最大值", "平均值", "中位数"};
        QStringList budgetKeys = {"min", "max", "avg", "median"};
        
        m_budgetTable->setRowCount(budgetLabels.size());
        
        for (int i = 0; i < budgetLabels.size() && i < budgetKeys.size(); ++i) {
            m_budgetTable->setItem(i, 0, new QTableWidgetItem(budgetLabels[i]));
            double value = budgetStats.value(budgetKeys[i], 0.0);
            m_budgetTable->setItem(i, 1, new QTableWidgetItem(QString("¥%1").arg(value, 0, 'f', 2)));
        }
        m_budgetTable->resizeColumnsToContents();
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：更新预算数据时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：更新预算数据时发生未知异常";
    }
}

void ReportWindow::onTrendDataLoaded(const QMap<QString, int> &projectsByDate,
                                    const QMap<QString, double> &budgetTrend)
{
    // 空指针检查
    if (!m_trendTable) {
        qDebug() << "报表窗口：趋势表格未初始化";
        return;
    }
    
    try {
        // 合并数据
        QSet<QString> allDates;
        for (auto it = projectsByDate.begin(); it != projectsByDate.end(); ++it) {
            allDates.insert(it.key());
        }
        for (auto it = budgetTrend.begin(); it != budgetTrend.end(); ++it) {
            allDates.insert(it.key());
        }
        
        QList<QString> sortedDates = allDates.values();
        std::sort(sortedDates.begin(), sortedDates.end());
        
        m_trendTable->setRowCount(sortedDates.size());
        
        for (int i = 0; i < sortedDates.size(); ++i) {
            QString date = sortedDates[i];
            int count = projectsByDate.value(date, 0);
            double budget = budgetTrend.value(date, 0.0);
            double avgBudget = count > 0 ? budget / count : 0.0;
            
            m_trendTable->setItem(i, 0, new QTableWidgetItem(date));
            m_trendTable->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
            m_trendTable->setItem(i, 2, new QTableWidgetItem(QString("¥%1").arg(budget, 0, 'f', 2)));
            m_trendTable->setItem(i, 3, new QTableWidgetItem(QString("¥%1").arg(avgBudget, 0, 'f', 2)));
        }
        
        m_trendTable->resizeColumnsToContents();
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：更新趋势数据时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：更新趋势数据时发生未知异常";
    }
}

void ReportWindow::onChartsDataLoaded(const QMap<QString, int> &statusStats,
                                      const QMap<QString, int> &managerStats)
{
    // 空指针检查
    if (!m_statusChartLabel || !m_managerChartLabel || !m_managerChartScrollArea) {
        qDebug() << "报表窗口：图表标签未初始化";
        return;
    }
    
    try {
        // 绘制状态分布饼图
        QSize statusSize = m_statusChartLabel->size();
        if (statusSize.width() <= 0 || statusSize.height() <= 0) {
            statusSize = QSize(900, 550); // 使用足够大的默认大小以包含图例
        }
        QPixmap statusPixmap(statusSize);
        statusPixmap.fill(Qt::white);
        QPainter statusPainter(&statusPixmap);
        if (statusPainter.isActive()) {
            drawPieChart(statusPainter, statusPixmap.rect(), statusStats, "项目状态分布");
            m_statusChartLabel->setPixmap(statusPixmap);
        }
        
        // 绘制经理分布柱状图（水平方向，支持滚动）
        // 根据项目经理数量动态计算图表高度
        int barCount = managerStats.size();
        int barHeight = 30;  // 每个柱子的高度
        int spacing = 10;    // 柱子之间的间距
        int chartHeight = barCount * (barHeight + spacing) + 100; // 总高度：柱子高度 + 标题和边距
        int chartWidth = 600; // 固定宽度
        
        QSize managerSize(chartWidth, chartHeight);
        QPixmap managerPixmap(managerSize);
        managerPixmap.fill(Qt::white);
        QPainter managerPainter(&managerPixmap);
        if (managerPainter.isActive()) {
            drawBarChart(managerPainter, managerPixmap.rect(), managerStats, "项目经理分布");
            m_managerChartLabel->setPixmap(managerPixmap);
            // 设置标签的固定大小，使其可以滚动
            m_managerChartLabel->setFixedSize(chartWidth, chartHeight);
        }
    } catch (const std::exception& e) {
        qDebug() << "报表窗口：生成图表时发生异常:" << e.what();
    } catch (...) {
        qDebug() << "报表窗口：生成图表时发生未知异常";
    }
}

void ReportWindow::onDataLoadProgress(const QString &message)
{
    // 空指针检查
    if (statusBar()) {
        statusBar()->showMessage(message, 0);
    }
}

void ReportWindow::onDataLoadFinished()
{
    // 空指针检查
    if (m_progressBar) {
        m_progressBar->setVisible(false);
    }
    if (m_refreshButton) {
        m_refreshButton->setEnabled(true);
    }
    if (statusBar()) {
        statusBar()->showMessage("报表数据加载完成", 3000);
    }
    
    // 数据加载完成不需要单独记录日志，因为refreshReport()已经记录了
}

void ReportWindow::onDataLoadError(const QString &error)
{
    // 空指针检查
    if (m_progressBar) {
        m_progressBar->setVisible(false);
    }
    if (m_refreshButton) {
        m_refreshButton->setEnabled(true);
    }
    if (statusBar()) {
        statusBar()->showMessage("报表数据加载失败", 3000);
    }
    
    // 记录数据加载错误日志（异常保护）
    try {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "刷新报表数据", QString(), QString("失败：%1").arg(error));
    } catch (...) {
        qDebug() << "报表窗口：记录错误日志时发生异常（可忽略）";
    }
    
    // 显示错误消息（异常保护）
    try {
        QMessageBox::critical(this, "错误", "加载报表数据时发生错误: " + error);
    } catch (...) {
        qDebug() << "报表窗口：显示错误消息时发生异常";
    }
}

// ========== ReportDataWorker 实现 ==========

ReportDataWorker::ReportDataWorker(ReportService *reportService, QObject *parent)
    : QObject(parent)
    , m_reportService(reportService)
    , m_shouldStop(false)
{
}

ReportDataWorker::~ReportDataWorker()
{
    stopLoading();
    
    // 注意：析构函数在对象所属的线程中执行
    // 如果对象已经移动到其他线程，这里可能无法正确清理连接
    // 连接清理应该在每个使用连接的函数结束时进行
}

// 获取工作线程的独立数据库连接（异常处理和资源管理）
QSqlDatabase ReportDataWorker::getWorkerDatabase()
{
    try {
        QString connName = QString("pm_report_%1_%2")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
            .arg(QDateTime::currentMSecsSinceEpoch());
        
        // 确保连接名唯一
        if (QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        
        ConfigManager& config = ConfigManager::getInstance();
        if (!config.initialize()) {
            qDebug() << "警告：配置管理器初始化失败";
        }
        const QString dbPath = config.getDatabasePath();
        
        if (dbPath.isEmpty()) {
            qDebug() << "工作线程：数据库路径为空";
            return QSqlDatabase();
        }
        
        QSqlDatabase workerDb = QSqlDatabase::addDatabase("QSQLITE", connName);
        workerDb.setDatabaseName(dbPath);
        
        if (!workerDb.open()) {
            qDebug() << "工作线程：无法打开数据库:" << workerDb.lastError().text();
            // 清理失败的连接
            if (QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return QSqlDatabase();
        }
        
        // 设置PRAGMA优化（异常保护）
        try {
            QSqlQuery pragmaQ(workerDb);
            pragmaQ.exec("PRAGMA journal_mode = WAL");
            pragmaQ.exec("PRAGMA synchronous = NORMAL");
            pragmaQ.exec("PRAGMA busy_timeout = 30000");
        } catch (...) {
            qDebug() << "工作线程：设置PRAGMA时发生异常（可忽略）";
        }
        
        return workerDb;
    } catch (const std::exception& e) {
        qDebug() << "ReportDataWorker：创建数据库连接时发生异常:" << e.what();
        return QSqlDatabase();
    } catch (...) {
        qDebug() << "ReportDataWorker：创建数据库连接时发生未知异常";
        return QSqlDatabase();
    }
}

void ReportDataWorker::stopLoading()
{
    m_shouldStop = true;
}

void ReportDataWorker::loadOverviewData()
{
    if (m_shouldStop) {
        return;
    }
    
    emit progressUpdated("正在加载概览数据...");
    
    QSqlDatabase workerDb;
    QString connName;
    
    try {
        // 创建独立的数据库连接（线程安全）
        workerDb = getWorkerDatabase();
        if (!workerDb.isOpen()) {
            emit loadError("无法打开数据库连接");
            return;
        }
        connName = workerDb.connectionName();
    
    // 查询项目总数
    QSqlQuery query(workerDb);
    query.prepare("SELECT COUNT(*) FROM projects");
    int totalProjects = 0;
    if (query.exec() && query.next()) {
        totalProjects = query.value(0).toInt();
    }
    if (m_shouldStop) return;
    
    // 查询预算总额
    query.prepare("SELECT SUM(budget) FROM projects");
    double totalBudget = 0.0;
    if (query.exec() && query.next()) {
        totalBudget = query.value(0).toDouble();
    }
    if (m_shouldStop) return;
    
    double avgBudget = totalProjects > 0 ? totalBudget / totalProjects : 0.0;
    
    // 查询状态统计
    query.prepare("SELECT status, COUNT(*) FROM projects GROUP BY status");
    QMap<QString, int> statusStats;
    if (query.exec()) {
        while (query.next()) {
            statusStats[query.value(0).toString()] = query.value(1).toInt();
        }
    }
    if (m_shouldStop) return;
    
    // 计算完成率
    int completed = 0;
    QStringList completedStatuses = {"已完成", "完成", "closed", "done"};
    for (const QString &status : completedStatuses) {
        if (statusStats.contains(status)) {
            completed += statusStats[status];
        }
    }
    double completionRate = totalProjects > 0 ? (completed * 100.0) / totalProjects : 0.0;
    
    // 查询延期项目
    QDate today = QDate::currentDate();
    query.prepare("SELECT COUNT(*) FROM projects WHERE DATE(end_date) < ? AND status NOT IN ('已完成', '完成', 'closed', 'done')");
    query.bindValue(0, today.toString("yyyy-MM-dd"));
    int delayedProjects = 0;
    if (query.exec() && query.next()) {
        delayedProjects = query.value(0).toInt();
    }
    if (m_shouldStop) return;
    
    int inProgressProjects = 0;
    int completedProjects = 0;
    for (auto it = statusStats.begin(); it != statusStats.end(); ++it) {
        if (it.key().contains("进行") || it.key().contains("进行中")) {
            inProgressProjects = it.value();
        }
        if (it.key().contains("完成") || it.key().contains("已完成")) {
            completedProjects = it.value();
        }
    }
    
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        
        if (!m_shouldStop) {
            emit overviewDataLoaded(totalProjects, totalBudget, avgBudget, completionRate,
                                   delayedProjects, inProgressProjects, completedProjects);
        }
    } catch (const std::exception& e) {
        qDebug() << "ReportDataWorker：加载概览数据时发生异常:" << e.what();
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError(QString("加载概览数据失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "ReportDataWorker：加载概览数据时发生未知异常";
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError("加载概览数据时发生未知错误");
    }
}

void ReportDataWorker::loadStatisticsData()
{
    if (m_shouldStop) {
        return;
    }
    
    QSqlDatabase workerDb;
    QString connName;
    
    try {
        // 创建独立的数据库连接（线程安全）
        workerDb = getWorkerDatabase();
        if (!workerDb.isOpen()) {
            emit loadError("无法打开数据库连接");
            return;
        }
        connName = workerDb.connectionName();
        
        QSqlQuery query(workerDb);
    
    // 加载状态统计
    emit progressUpdated("正在加载统计数据...");
    query.prepare("SELECT status, COUNT(*) FROM projects GROUP BY status");
    QMap<QString, int> statusStats;
    if (query.exec()) {
        while (query.next()) {
            statusStats[query.value(0).toString()] = query.value(1).toInt();
        }
    }
        if (m_shouldStop) {
            if (workerDb.isOpen()) {
                workerDb.close();
            }
            if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return;
        }
        
        // 计算状态百分比
        int total = 0;
        for (auto it = statusStats.begin(); it != statusStats.end(); ++it) {
            total += it.value();
        }
        QMap<QString, double> statusPercentages;
        if (total > 0) {
            for (auto it = statusStats.begin(); it != statusStats.end(); ++it) {
                statusPercentages[it.key()] = (it.value() * 100.0) / total;
            }
        }
        
        if (!m_shouldStop) {
            emit statusDataLoaded(statusStats, statusPercentages);
        }
        
        // 加载经理统计
        emit progressUpdated("正在加载经理数据...");
        query.prepare("SELECT manager, COUNT(*) FROM projects GROUP BY manager ORDER BY COUNT(*) DESC");
        QMap<QString, int> managerStats;
        if (query.exec()) {
            while (query.next()) {
                managerStats[query.value(0).toString()] = query.value(1).toInt();
            }
        }
        if (m_shouldStop) {
            if (workerDb.isOpen()) {
                workerDb.close();
            }
            if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return;
        }
        
        query.prepare("SELECT manager, SUM(budget) FROM projects GROUP BY manager ORDER BY SUM(budget) DESC");
        QMap<QString, double> managerBudgets;
        if (query.exec()) {
            while (query.next()) {
                managerBudgets[query.value(0).toString()] = query.value(1).toDouble();
            }
        }
        if (m_shouldStop) {
            if (workerDb.isOpen()) {
                workerDb.close();
            }
            if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return;
        }
        
        if (!m_shouldStop) {
            emit managerDataLoaded(managerStats, managerBudgets);
        }
        
        // 加载预算统计
        emit progressUpdated("正在加载预算数据...");
        query.prepare("SELECT MIN(budget), MAX(budget), AVG(budget) FROM projects");
        QMap<QString, double> budgetStats;
        if (query.exec() && query.next()) {
            budgetStats["min"] = query.value(0).toDouble();
            budgetStats["max"] = query.value(1).toDouble();
            budgetStats["avg"] = query.value(2).toDouble();
        }
        
        // 计算中位数
        query.prepare("SELECT budget FROM projects ORDER BY budget");
        QList<double> budgets;
        if (query.exec()) {
            while (query.next()) {
                budgets.append(query.value(0).toDouble());
            }
        }
        if (budgets.size() > 0) {
            std::sort(budgets.begin(), budgets.end());
            int size = budgets.size();
            if (size % 2 == 0) {
                budgetStats["median"] = (budgets[size/2 - 1] + budgets[size/2]) / 2.0;
            } else {
                budgetStats["median"] = budgets[size/2];
            }
        } else {
            budgetStats["median"] = 0.0;
        }
        
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        
        if (!m_shouldStop) {
            emit budgetDataLoaded(budgetStats);
        }
    } catch (const std::exception& e) {
        qDebug() << "ReportDataWorker：加载统计数据时发生异常:" << e.what();
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError(QString("加载统计数据失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "ReportDataWorker：加载统计数据时发生未知异常";
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError("加载统计数据时发生未知错误");
    }
}

void ReportDataWorker::loadTrendData(const QDate &startDate, const QDate &endDate, const QString &groupBy)
{
    if (m_shouldStop) {
        return;
    }
    
    QSqlDatabase workerDb;
    QString connName;
    
    try {
        // 创建独立的数据库连接（线程安全）
        workerDb = getWorkerDatabase();
        if (!workerDb.isOpen()) {
            emit loadError("无法打开数据库连接");
            return;
        }
        connName = workerDb.connectionName();
        
        emit progressUpdated("正在加载趋势数据...");
    
    QString dateFormat;
    if (groupBy == "day") {
        dateFormat = "DATE(created_at)";
    } else if (groupBy == "week") {
        dateFormat = "strftime('%Y-W%W', created_at)";
    } else if (groupBy == "month") {
        dateFormat = "strftime('%Y-%m', created_at)";
    } else if (groupBy == "year") {
        dateFormat = "strftime('%Y', created_at)";
    } else {
        dateFormat = "DATE(created_at)";
    }
    
        QSqlQuery query(workerDb);
        
        // 查询项目数量趋势
        query.prepare(QString("SELECT %1, COUNT(*) FROM projects WHERE DATE(created_at) BETWEEN ? AND ? GROUP BY %1 ORDER BY %1")
                      .arg(dateFormat));
        query.bindValue(0, startDate.toString("yyyy-MM-dd"));
        query.bindValue(1, endDate.toString("yyyy-MM-dd"));
        
        QMap<QString, int> projectsByDate;
        if (query.exec()) {
            while (query.next()) {
                projectsByDate[query.value(0).toString()] = query.value(1).toInt();
            }
        }
        if (m_shouldStop) {
            if (workerDb.isOpen()) {
                workerDb.close();
            }
            if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return;
        }
        
        // 查询预算趋势
        query.prepare(QString("SELECT %1, SUM(budget) FROM projects WHERE DATE(created_at) BETWEEN ? AND ? GROUP BY %1 ORDER BY %1")
                      .arg(dateFormat));
        query.bindValue(0, startDate.toString("yyyy-MM-dd"));
        query.bindValue(1, endDate.toString("yyyy-MM-dd"));
        
        QMap<QString, double> budgetTrend;
        if (query.exec()) {
            while (query.next()) {
                budgetTrend[query.value(0).toString()] = query.value(1).toDouble();
            }
        }
        
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        
        if (!m_shouldStop) {
            emit trendDataLoaded(projectsByDate, budgetTrend);
        }
    } catch (const std::exception& e) {
        qDebug() << "ReportDataWorker：加载趋势数据时发生异常:" << e.what();
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError(QString("加载趋势数据失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "ReportDataWorker：加载趋势数据时发生未知异常";
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError("加载趋势数据时发生未知错误");
    }
}

void ReportDataWorker::loadChartsData()
{
    if (m_shouldStop) {
        return;
    }
    
    QSqlDatabase workerDb;
    QString connName;
    
    try {
        // 创建独立的数据库连接（线程安全）
        workerDb = getWorkerDatabase();
        if (!workerDb.isOpen()) {
            emit loadError("无法打开数据库连接");
            return;
        }
        connName = workerDb.connectionName();
        
        emit progressUpdated("正在生成图表...");
        
        QSqlQuery query(workerDb);
        
        // 查询状态统计
        query.prepare("SELECT status, COUNT(*) FROM projects GROUP BY status");
        QMap<QString, int> statusStats;
        if (query.exec()) {
            while (query.next()) {
                statusStats[query.value(0).toString()] = query.value(1).toInt();
            }
        }
        if (m_shouldStop) {
            if (workerDb.isOpen()) {
                workerDb.close();
            }
            if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
                QSqlDatabase::removeDatabase(connName);
            }
            return;
        }
        
        // 查询经理统计
        query.prepare("SELECT manager, COUNT(*) FROM projects GROUP BY manager ORDER BY COUNT(*) DESC");
        QMap<QString, int> managerStats;
        if (query.exec()) {
            while (query.next()) {
                managerStats[query.value(0).toString()] = query.value(1).toInt();
            }
        }
        
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        
        if (!m_shouldStop) {
            emit chartsDataLoaded(statusStats, managerStats);
            emit loadFinished();
        }
    } catch (const std::exception& e) {
        qDebug() << "ReportDataWorker：加载图表数据时发生异常:" << e.what();
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError(QString("加载图表数据失败: %1").arg(e.what()));
    } catch (...) {
        qDebug() << "ReportDataWorker：加载图表数据时发生未知异常";
        // 清理连接
        if (workerDb.isOpen()) {
            workerDb.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        emit loadError("加载图表数据时发生未知错误");
    }
}

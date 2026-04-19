#ifndef REPORTWINDOW_H
#define REPORTWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDateEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QFileDialog>
#include <QPainter>
#include <QPixmap>
#include <QPrinter>
#include <QPrintDialog>
#include <QThread>
#include <QProgressBar>
#include <QScrollArea>
#include "reportservice.h"
#include "databasemanager.h"

// 前向声明
class ReportDataWorker;

/**
 * @brief 报表窗口
 * 
 * 功能：
 * 1. 显示数据统计报表
 * 2. 展示图表
 * 3. 导出报表
 */
class ReportWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ReportWindow(ReportService *reportService, QWidget *parent = nullptr);
    ~ReportWindow();

public slots:
    void refreshReport();

private slots:
    void exportToPDF();
    void exportToExcel();
    void exportToCSV();
    void exportChart();
    void onDateRangeChanged();
    
    // 数据加载完成回调
    void onOverviewDataLoaded(int totalProjects, double totalBudget, double avgBudget, 
                               double completionRate, int delayedProjects, 
                               int inProgressProjects, int completedProjects);
    void onStatusDataLoaded(const QMap<QString, int> &statusStats, 
                           const QMap<QString, double> &statusPercentages);
    void onManagerDataLoaded(const QMap<QString, int> &managerStats,
                            const QMap<QString, double> &managerBudgets);
    void onBudgetDataLoaded(const QMap<QString, double> &budgetStats);
    void onTrendDataLoaded(const QMap<QString, int> &projectsByDate,
                          const QMap<QString, double> &budgetTrend);
    void onChartsDataLoaded(const QMap<QString, int> &statusStats,
                           const QMap<QString, int> &managerStats);
    void onDataLoadProgress(const QString &message);
    void onDataLoadFinished();
    void onDataLoadError(const QString &error);

private:
    void setupUI();
    void createOverviewTab();
    void createStatisticsTab();
    void createTrendTab();
    void createChartsTab();
    
    void updateOverviewReport();
    void updateStatisticsReport();
    void updateTrendReport();
    void updateCharts();
    
    // 图表绘制
    void drawBarChart(QPainter &painter, const QRect &rect, const QMap<QString, int> &data, const QString &title);
    void drawPieChart(QPainter &painter, const QRect &rect, const QMap<QString, int> &data, const QString &title);
    void drawLineChart(QPainter &painter, const QRect &rect, const QMap<QString, double> &data, const QString &title);
    
    // 导出功能
    void exportTableToCSV(QTableWidget *table, const QString &filePath);
    void exportTableToExcel(QTableWidget *table, const QString &filePath);
    void exportToPDFFile(const QString &filePath);

    ReportService *m_reportService;
    QThread *m_dataLoadThread;
    ReportDataWorker *m_dataWorker;
    
    // UI组件
    QTabWidget *m_tabWidget;
    QProgressBar *m_progressBar;
    
    // 概览标签页
    QWidget *m_overviewTab;
    QTableWidget *m_overviewTable;
    QLabel *m_totalProjectsLabel;
    QLabel *m_totalBudgetLabel;
    QLabel *m_averageBudgetLabel;
    QLabel *m_completionRateLabel;
    
    // 统计标签页
    QWidget *m_statisticsTab;
    QTableWidget *m_statusTable;
    QTableWidget *m_managerTable;
    QTableWidget *m_budgetTable;
    
    // 趋势标签页
    QWidget *m_trendTab;
    QDateEdit *m_startDateEdit;
    QDateEdit *m_endDateEdit;
    QComboBox *m_groupByCombo;
    QTableWidget *m_trendTable;
    
    // 图表标签页
    QWidget *m_chartsTab;
    QLabel *m_statusChartLabel;
    QScrollArea *m_managerChartScrollArea;
    QLabel *m_managerChartLabel;
    QLabel *m_budgetTrendChartLabel;
    QPushButton *m_exportChartButton;
    
    // 工具栏按钮
    QPushButton *m_refreshButton;
    QPushButton *m_exportPDFButton;
    QPushButton *m_exportExcelButton;
    QPushButton *m_exportCSVButton;
    
    // 数据缓存
    QDate m_cachedStartDate;
    QDate m_cachedEndDate;
    QString m_cachedGroupBy;
};

// 报表数据加载工作线程
class ReportDataWorker : public QObject
{
    Q_OBJECT

public:
    explicit ReportDataWorker(ReportService *reportService, QObject *parent = nullptr);
    ~ReportDataWorker();

public slots:
    void loadOverviewData();
    void loadStatisticsData();
    void loadTrendData(const QDate &startDate, const QDate &endDate, const QString &groupBy);
    void loadChartsData();
    void stopLoading();

signals:
    void overviewDataLoaded(int totalProjects, double totalBudget, double avgBudget,
                            double completionRate, int delayedProjects,
                            int inProgressProjects, int completedProjects);
    void statusDataLoaded(const QMap<QString, int> &statusStats,
                         const QMap<QString, double> &statusPercentages);
    void managerDataLoaded(const QMap<QString, int> &managerStats,
                         const QMap<QString, double> &managerBudgets);
    void budgetDataLoaded(const QMap<QString, double> &budgetStats);
    void trendDataLoaded(const QMap<QString, int> &projectsByDate,
                        const QMap<QString, double> &budgetTrend);
    void chartsDataLoaded(const QMap<QString, int> &statusStats,
                         const QMap<QString, int> &managerStats);
    void progressUpdated(const QString &message);
    void loadFinished();
    void loadError(const QString &error);

private:
    ReportService *m_reportService;
    volatile bool m_shouldStop;
    
    // 获取工作线程的独立数据库连接
    QSqlDatabase getWorkerDatabase();
};

#endif // REPORTWINDOW_H

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QItemSelection>
#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QThread>
#include <QTimer>
#include <QCloseEvent>

QT_BEGIN_NAMESPACE
class QTableView;
class QToolBar;
class QAction;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QProgressBar;
class QThread;
class QTimer;
QT_END_NAMESPACE

class DatabaseManager;
class SimpleDialog;
class SimplePaginationModel;
class PaginationController;
class ProjectService;
class SearchService;
class ExportService;
class ReportService;
class ReportWindow;
class UIStateManager;

// 数据生成工作线程
class DataGenerationWorker : public QObject
{
    Q_OBJECT

public:
    explicit DataGenerationWorker(QObject *parent = nullptr);
    ~DataGenerationWorker();  // 显式析构函数，确保资源正确清理
    void stopGeneration();

public slots:
    void generateTestData(int totalRecords);

signals:
    void progressUpdated(int current, int total, double rate);
    void generationCompleted(int totalRecords, int totalTime);
    void generationFailed(const QString &error);

private:
    void generateBatchData(int startIndex, int batchSize, const QStringList &projectNames,
                           const QStringList &managerNames, const QStringList &statuses,
                           const QStringList &descriptions);
    void saveProgress(int currentCount, int totalCount);
    int loadProgress();

    volatile bool m_shouldStop;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onAddProject();
    void onDeleteProject();
    void onEditProject();
    void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void onSearch();
    void onClearSearch();
    void onRefreshData();
    void onGenerateTestData();
    void onDataGenerationProgress(int current, int total, double rate);
    void onDataGenerationCompleted(int totalRecords, int totalTime);
    void onDataGenerationFailed(const QString &error);
    void onDataLoadingFinished();  // 数据加载完成后的处理
    void onExportCSV();  // 导出CSV功能
    void onTotalRecordsChanged(int totalRecords);  // 总记录数变化时更新状态栏
    void onShowPerformanceMonitor();  // 显示性能监控面板
    void onShowSystemLog();  // 显示系统日志窗口
    void onShowUserManagement();  // 显示用户管理窗口
    void onShowReport();  // 显示报表窗口
    void onLogout();  // 退出系统，返回登录窗口
    void onClearAllData();  // 新增

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void setupModel();
    void setupViews();
    void createToolbar();
    void createSearchBar();
    void createStatusBar();
    void loadDataFromDatabase();
    QDate getDateFromDialog(const QString &title, const QString &label, const QDate &defaultDate);
    void updateStatusBar();
    void adjustColumnWidths();
    void highlightNewRecord();

    QStandardItemModel *model;
    SimplePaginationModel *paginationModel;
    QTableView *tableView;
    PaginationController *paginationController;
    QLineEdit *searchLineEdit;
    QComboBox *searchComboBox;
    QPushButton *searchButton;
    QPushButton *clearButton;
    QLabel *statusLabel;
    QProgressBar *progressBar;
    DatabaseManager *dbManager;
    
    // 服务层
    ProjectService *projectService;
    SearchService *searchService;
    ExportService *exportService;
    ReportService *reportService;
    
    // UI状态管理
    class UIStateManager *uiStateManager;

    // 数据生成相关
    QThread *dataGenerationThread;
    DataGenerationWorker *dataGenerationWorker;
    QTimer *progressUpdateTimer;
    
    // 性能监控
    class PerformanceMonitor *performanceMonitor;
    class SystemLogWindow *systemLogWindow;
    class UserManagementWindow *userManagementWindow;
    class ReportWindow *reportWindow;
    
    // 退出系统标志
    bool m_logoutRequested;  // 是否请求退出系统（返回登录窗口）
    
signals:
    // 退出系统信号（当用户点击退出系统按钮时发出）
    void logoutRequested();
    
public:
    // 检查是否请求退出系统
    bool isLogoutRequested() const { return m_logoutRequested; }
};

#endif // MAINWINDOW_H






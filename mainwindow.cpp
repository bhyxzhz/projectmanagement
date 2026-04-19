#include "mainwindow.h"
#include "databasemanager.h"
#include "simpledialog.h"
#include "simplepaginationmodel.h"
#include "paginationcontroller.h"
#include "performancemonitor.h"
#include "systemlogwindow.h"
#include "usermanagementwindow.h"
#include "projectservice.h"
#include "searchservice.h"
#include "exportservice.h"
#include "reportservice.h"
#include "reportwindow.h"
#include "uistatemanager.h"
#include "appconstants.h"
#include "configmanager.h"
#include "permissionmanager.h"
#include <QCoreApplication>
#include <QTableView>
#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QHeaderView>
#include <QDialog>
#include <QCalendarWidget>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QStatusBar>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QResizeEvent>
#include <QSqlQuery>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QFileDialog>
#include <QStandardPaths>
#include <QEventLoop>
#include <QStringConverter>
#include <QMenuBar>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), model(new QStandardItemModel(this)), tableView(new QTableView(this))
    , m_logoutRequested(false)
{
    // 设置窗口属性
    resize(UIConstants::DEFAULT_WINDOW_WIDTH, UIConstants::DEFAULT_WINDOW_HEIGHT);
    
    // 获取数据库管理器（已在main.cpp中初始化）
    dbManager = &DatabaseManager::getInstance();

    // 初始化服务层（依赖注入，实现依赖倒置原则）
    // 服务层依赖IDatabaseAccessor接口，而不是DatabaseManager具体类
    projectService = new ProjectService(dbManager, this);  // DatabaseManager实现了IDatabaseAccessor接口
    searchService = new SearchService(dbManager, this);    // DatabaseManager实现了IDatabaseAccessor接口
    exportService = new ExportService(this);
    reportService = new ReportService(dbManager, this);    // DatabaseManager实现了IDatabaseAccessor接口
    
    // 初始化UI状态管理器
    uiStateManager = new UIStateManager(this);
    connect(uiStateManager, &UIStateManager::selectionChanged, this, [](int /*row*/) {
        // 状态管理器已处理选择变化
    });
    
    // 连接服务信号
    connect(projectService, &ProjectService::operationCompleted,
            this, [this](const QString &/*operation*/, bool success, const QString &message) {
                if (success) {
                    QMessageBox::information(this, "成功", message);
                } else {
                    QMessageBox::critical(this, "错误", message);
                }
            },
            Qt::QueuedConnection);

    // 初始化分页模型
    paginationModel = new SimplePaginationModel(this);
    paginationController = new PaginationController(this);

    // 初始化数据生成工作线程
    dataGenerationThread = new QThread(this);
    dataGenerationWorker = new DataGenerationWorker(); // 不设置父对象，因为要移动到其他线程
    dataGenerationWorker->moveToThread(dataGenerationThread);

    // 连接工作线程信号
    connect(dataGenerationWorker, &DataGenerationWorker::progressUpdated,
            this, &MainWindow::onDataGenerationProgress);
    connect(dataGenerationWorker, &DataGenerationWorker::generationCompleted,
            this, &MainWindow::onDataGenerationCompleted);
    connect(dataGenerationWorker, &DataGenerationWorker::generationFailed,
            this, &MainWindow::onDataGenerationFailed);

    // 启动工作线程
    dataGenerationThread->start();
    
    // 初始化性能监控面板
    performanceMonitor = new PerformanceMonitor(this);
    systemLogWindow = new SystemLogWindow(this);
    userManagementWindow = new UserManagementWindow(this);
    reportWindow = nullptr;  // 延迟创建，按需创建
    
    // 将性能监控面板注册为属性，方便其他类访问
    this->setProperty("performanceMonitor", QVariant::fromValue<QObject*>(performanceMonitor));

    // 初始化模型和视图
    setupModel();
    setupViews();
    createToolbar();
    createSearchBar();
    createStatusBar();

    // 关键优化：延迟数据加载，让界面先显示出来（避免启动卡顿）
    // 数据库初始化已在getInstance()中完成，但数据加载延迟执行
    // 这样可以先显示空界面，然后在后台加载数据
    // 延迟150ms（数据库初始化50ms + 额外缓冲100ms），确保数据库已初始化后再加载数据
    QTimer::singleShot(150, this, &MainWindow::loadDataFromDatabase);
}

/**
 * @brief MainWindow析构函数
 * 
 * 安全优化：确保线程资源正确清理，避免资源泄漏
 * 1. 优雅停止：先请求停止，等待线程自然退出
 * 2. 强制终止：仅在必要时使用terminate（最后手段）
 * 3. 资源清理：确保所有对象正确释放
 */
MainWindow::~MainWindow()
{
    // 停止数据生成（优雅停止）
    if (dataGenerationWorker) {
        dataGenerationWorker->stopGeneration();
    }

    // 正确清理工作线程（避免资源泄漏）
    if (dataGenerationThread && dataGenerationThread->isRunning()) {
        // 步骤1：请求线程退出（优雅停止）
        dataGenerationThread->quit();
        
        // 步骤2：等待线程结束（最多等待指定时间）
        if (!dataGenerationThread->wait(ThreadConstants::THREAD_WAIT_TIMEOUT_MS)) {
            // 步骤3：如果线程没有在指定时间内结束，记录警告
            qDebug() << "警告：工作线程未在指定时间内退出，尝试强制终止";
            
            // 步骤4：强制终止（最后手段，可能导致资源泄漏，但总比程序挂起好）
            dataGenerationThread->terminate();
            
            // 步骤5：再次等待，确保线程真正终止
            if (!dataGenerationThread->wait(ThreadConstants::THREAD_FORCE_TERMINATE_WAIT_MS)) {
                qDebug() << "严重警告：强制终止线程后仍无法等待其退出";
            }
        }
    }

    // 删除工作线程对象（先删除worker，再删除thread，避免悬空指针）
    if (dataGenerationWorker) {
        // 断开所有信号槽连接，避免析构时信号发射到已销毁的对象
        dataGenerationWorker->disconnect();
        dataGenerationWorker->deleteLater();
        dataGenerationWorker = nullptr;
    }

    // 删除线程对象
    if (dataGenerationThread) {
        dataGenerationThread->disconnect();
        dataGenerationThread->deleteLater();
        dataGenerationThread = nullptr;
    }
    
    // 处理Qt事件循环，确保deleteLater的对象被正确删除
    QCoreApplication::processEvents();
}

/**
 * @brief 窗口关闭事件处理
 * 
 * 安全优化：确保在窗口关闭时正确清理线程资源
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 如果是退出系统（返回登录窗口），快速关闭，不等待线程
    if (m_logoutRequested) {
        qDebug() << "退出系统：快速关闭窗口，不等待线程";
        // 停止数据生成（优雅停止）
        if (dataGenerationWorker) {
            dataGenerationWorker->stopGeneration();
        }
        // 请求线程退出，但不等待
        if (dataGenerationThread && dataGenerationThread->isRunning()) {
            dataGenerationThread->quit();
        }
        // 接受关闭事件，立即关闭
        event->accept();
        return;
    }

    // 正常关闭：停止数据生成（优雅停止）
    if (dataGenerationWorker) {
        dataGenerationWorker->stopGeneration();
    }

    // 等待工作线程结束（与析构函数相同的清理逻辑）
    if (dataGenerationThread && dataGenerationThread->isRunning()) {
        dataGenerationThread->quit();
        if (!dataGenerationThread->wait(ThreadConstants::THREAD_FORCE_TERMINATE_WAIT_MS)) {
            // 如果指定时间内没有结束，强制终止（最后手段）
            qDebug() << "窗口关闭：强制终止工作线程";
            dataGenerationThread->terminate();
            dataGenerationThread->wait();
        }
    }

    // 处理Qt事件循环，确保资源清理完成
    QCoreApplication::processEvents();

    // 接受关闭事件
    event->accept();
}

void MainWindow::setupModel()
{
    // 设置表头
    QStringList headers;
    headers << "项目ID" << "项目名称" << "项目经理" << "开始日期"
            << "结束日期" << "预算(万元)" << "状态" << "描述";
    model->setHorizontalHeaderLabels(headers);
}

void MainWindow::setupViews()
{
    // 设置表格视图属性
    if (uiStateManager->isPaginationMode()) {
        tableView->setModel(paginationModel);
    } else {
        tableView->setModel(model);
    }

    tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 显示行号，不隐藏左侧编号
    tableView->verticalHeader()->setVisible(true);
    tableView->verticalHeader()->setDefaultSectionSize(UIConstants::COLUMN_HEADER_HEIGHT);

    // 设置列宽 - 让表格填充满整个窗口
    tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);  // 项目ID - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);          // 项目名称 - 拉伸填充
    tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents); // 项目经理 - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents); // 开始日期 - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents); // 结束日期 - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents); // 预算 - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents); // 状态 - 固定宽度
    tableView->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);          // 描述 - 拉伸填充

    // 设置表格样式
    tableView->setAlternatingRowColors(true);  // 交替行颜色
    tableView->setGridStyle(Qt::SolidLine);    // 网格线样式
    tableView->setShowGrid(true);              // 显示网格线
    tableView->setSortingEnabled(true);        // 启用排序

    // 连接选择变化信号
    connect(tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    // 连接分页控制器信号
    if (uiStateManager->isPaginationMode()) {
        connect(paginationController, &PaginationController::pageChanged,
                paginationModel, &SimplePaginationModel::setCurrentPage);
        connect(paginationController, &PaginationController::pageSizeChanged,
                paginationModel, &SimplePaginationModel::setPageSize);
        connect(paginationController, &PaginationController::refreshRequested,
                paginationModel, &SimplePaginationModel::refreshData);

        connect(paginationModel, &SimplePaginationModel::pageChanged,
                paginationController, &PaginationController::setCurrentPage);
        connect(paginationModel, &SimplePaginationModel::totalRecordsChanged,
                paginationController, &PaginationController::setTotalRecords);
        // 连接总记录数变化信号，更新状态栏
        connect(paginationModel, &SimplePaginationModel::totalRecordsChanged,
                this, &MainWindow::onTotalRecordsChanged);
        
        // 连接查询性能信号
        connect(paginationModel, &SimplePaginationModel::queryPerformed,
                this, [this](const QString& queryType, qint64 elapsedMs, bool fromCache) {
                    if (performanceMonitor) {
                        performanceMonitor->recordQuery(queryType, elapsedMs, fromCache);
                    }
                });
        // 连接pageChanged信号来更新总页数
        connect(paginationModel, &SimplePaginationModel::pageChanged,
                [this](int /*currentPage*/, int totalPages) {
                    paginationController->setTotalPages(totalPages);
                });
        connect(paginationModel, &SimplePaginationModel::dataLoadingStarted,
                [this]() { paginationController->setLoading(true); });
        connect(paginationModel, &SimplePaginationModel::dataLoadingFinished,
                [this]() {
                    paginationController->setLoading(false);
                    onDataLoadingFinished();  // 数据加载完成后处理选中逻辑
                });
    }

    // 设置中心部件 - 让表格填充满整个窗口
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);  // 移除边距
    layout->addWidget(tableView);

    // 添加分页控制器
    if (uiStateManager->isPaginationMode()) {
        layout->addWidget(paginationController);
    }

    centralWidget->setLayout(layout);
    setCentralWidget(centralWidget);
}

void MainWindow::createToolbar()
{
    QToolBar *toolBar = addToolBar("操作");

    QAction *addAction = new QAction(QIcon(":/new/prefix1/images/add001.ico"), "添加项目", this);
    QAction *deleteAction = new QAction(QIcon(":/new/prefix1/images/delete001.ico"), "删除项目", this);
    QAction *editAction = new QAction(QIcon(":/new/prefix1/images/edit001.ico"), "编辑项目", this);
    QAction *refreshAction = new QAction("刷新系统数据", this);
    QAction *generateTestDataAction = new QAction("生成测试数据", this);
    QAction *clearDataAction = new QAction("清空所有数据", this);  // 新增
    QAction *exportCSVAction = new QAction("导出数据(CSV)", this);

    // 设置工具按钮样式
    addAction->setShortcut(QKeySequence::New);
    deleteAction->setShortcut(QKeySequence::Delete);
    editAction->setShortcut(Qt::Key_Return);
    refreshAction->setShortcut(QKeySequence::Refresh);
    exportCSVAction->setShortcut(QKeySequence::SaveAs);

    // 根据用户权限控制删除按钮的显示
    PermissionManager& permManager = PermissionManager::getInstance();
    if (!permManager.canDeleteProject()) {
        deleteAction->setVisible(false);
        deleteAction->setEnabled(false);
    }

    toolBar->addAction(addAction);
    toolBar->addAction(deleteAction);
    toolBar->addAction(editAction);
    toolBar->addSeparator();
    
    QAction *performanceMonitorAction = new QAction("性能监控", this);
    toolBar->addAction(performanceMonitorAction);
    
    QAction *logoutAction = new QAction("退出系统", this);
    toolBar->addAction(logoutAction);

    // =========================
    // 菜单栏：系统菜单（集中系统级操作）
    // =========================
    QMenu *systemMenu = menuBar()->addMenu("系统");
    systemMenu->addAction(refreshAction);
    systemMenu->addAction(generateTestDataAction);
    systemMenu->addAction(clearDataAction);  // 新增
    systemMenu->addAction(exportCSVAction);
    
    // 系统日志菜单项（只有管理员可见，防止普通用户知道管理员密码后删除日志）
    if (permManager.canViewSystemLog()) {
        QAction *systemLogAction = new QAction("系统日志", this);
        systemMenu->addSeparator();
        systemMenu->addAction(systemLogAction);
        connect(systemLogAction, &QAction::triggered, this, &MainWindow::onShowSystemLog);
        
        // 用户管理菜单项（只有管理员可见）
        QAction *userManagementAction = new QAction("用户管理", this);
        systemMenu->addAction(userManagementAction);
        connect(userManagementAction, &QAction::triggered, this, &MainWindow::onShowUserManagement);
        
        // 报表菜单项
        QAction *reportAction = new QAction("项目报表", this);
        systemMenu->addAction(reportAction);
        connect(reportAction, &QAction::triggered, this, &MainWindow::onShowReport);
    }

    // 连接信号槽
    connect(addAction, &QAction::triggered, this, &MainWindow::onAddProject);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteProject);
    connect(editAction, &QAction::triggered, this, &MainWindow::onEditProject);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::onRefreshData);
    connect(generateTestDataAction, &QAction::triggered, this, &MainWindow::onGenerateTestData);
    connect(clearDataAction, &QAction::triggered, this, &MainWindow::onClearAllData);  // 新增
    connect(exportCSVAction, &QAction::triggered, this, &MainWindow::onExportCSV);
    connect(performanceMonitorAction, &QAction::triggered, this, &MainWindow::onShowPerformanceMonitor);
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);
}

void MainWindow::createSearchBar()
{
    QWidget *searchWidget = new QWidget();
    QHBoxLayout *searchLayout = new QHBoxLayout(searchWidget);

    searchLineEdit = new QLineEdit();
    searchLineEdit->setPlaceholderText("输入搜索关键词...");
    searchLineEdit->setMaximumWidth(200);

    searchComboBox = new QComboBox();
    searchComboBox->addItems({"全部", "项目名称", "项目经理", "状态", "项目ID"});
    searchComboBox->setMaximumWidth(100);

    searchButton = new QPushButton("搜索");
    clearButton = new QPushButton("清除");

    searchLayout->addWidget(new QLabel("搜索:"));
    searchLayout->addWidget(searchLineEdit);
    searchLayout->addWidget(searchComboBox);
    searchLayout->addWidget(searchButton);
    searchLayout->addWidget(clearButton);
    searchLayout->addStretch();

    // 添加到工具栏
    QToolBar *searchToolBar = addToolBar("搜索");
    searchToolBar->addWidget(searchWidget);

    // 连接信号槽
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearSearch);
    connect(searchLineEdit, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
}

void MainWindow::createStatusBar()
{
    statusLabel = new QLabel("就绪");
    progressBar = new QProgressBar();
    progressBar->setVisible(false);
    progressBar->setMaximumWidth(200);

    statusBar()->addWidget(statusLabel);
    statusBar()->addPermanentWidget(progressBar);
}

void MainWindow::loadDataFromDatabase()
{
    if (!dbManager->isConnected()) {
        QMessageBox::critical(this, "错误", "数据库连接失败: " + dbManager->getLastError());
        return;
    }

    // 大数据量默认启用分页模式：避免任何全量加载/计数导致UI卡死
    uiStateManager->setPaginationMode(true);

    // 设置进度条为不确定模式，避免阻塞UI线程
    progressBar->setVisible(true);
    progressBar->setRange(0, 0); // 不确定进度模式
    statusLabel->setText("正在初始化数据模型...");

    // 使用异步方式更新进度，避免阻塞UI
    // 关键优化：移除QApplication::processEvents()调用，避免阻塞
    QTimer::singleShot(0, this, [this]() {
        statusLabel->setText("正在初始化分页模型...");

        // 设置分页参数：每页显示默认数量的记录，缓存默认页数以提高响应速度
        paginationModel->setPageSize(PaginationConstants::DEFAULT_PAGE_SIZE);
        paginationModel->setCacheSize(PaginationConstants::DEFAULT_CACHE_SIZE);

        // 确保分页控制器的页面大小与模型一致
        paginationController->setPageSize(PaginationConstants::DEFAULT_PAGE_SIZE);

        statusLabel->setText(QString("高性能分页模式已启用（正在加载数据...）"));

        // 关键优化：setDatabase() 内部已经延迟100ms才调用refreshData()
        // 这里只需要设置数据库连接，数据加载会自动延迟执行
        paginationModel->setDatabase(dbManager->getDatabase());
        
        // 延迟隐藏进度条，让用户看到数据加载状态（提升用户体验）
        // 总延迟 = 300ms(loadDataFromDatabase) + 100ms(setDatabase) + 额外时间 = 约500ms后隐藏
        QTimer::singleShot(500, this, [this]() {
            progressBar->setVisible(false);
        });
    });
}

void MainWindow::updateStatusBar()
{
    int totalProjects = projectService->getTotalProjectCount();
    double totalBudget = projectService->getTotalBudget();
    QStringList statusStats = projectService->getProjectStatusStats();

    // 使用状态管理器统一更新状态栏
    uiStateManager->updateStatusBar(statusLabel, totalProjects, totalBudget, statusStats);
}

void MainWindow::adjustColumnWidths()
{
    // 获取表格的可用宽度
    int tableWidth = tableView->width();
    int availableWidth = tableWidth - 20; // 留出一些边距

    // 计算固定宽度列的总宽度
    int fixedWidth = 0;
    fixedWidth += 80;  // 项目ID
    fixedWidth += 100; // 项目经理
    fixedWidth += 100; // 开始日期
    fixedWidth += 100; // 结束日期
    fixedWidth += 100; // 预算
    fixedWidth += 80;  // 状态

    // 计算可拉伸列的宽度
    int stretchWidth = (availableWidth - fixedWidth) / 2; // 项目名称和描述各占一半

    // 设置列宽
    tableView->setColumnWidth(0, 80);   // 项目ID
    tableView->setColumnWidth(1, stretchWidth);  // 项目名称
    tableView->setColumnWidth(2, 100);  // 项目经理
    tableView->setColumnWidth(3, 100);  // 开始日期
    tableView->setColumnWidth(4, 100);  // 结束日期
    tableView->setColumnWidth(5, 100);  // 预算
    tableView->setColumnWidth(6, 80);   // 状态
    tableView->setColumnWidth(7, stretchWidth);  // 描述
}

void MainWindow::onSearch()
{
    QString keyword = searchLineEdit->text().trimmed();
    if (keyword.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入搜索关键词");
        return;
    }

    // 使用服务层转换过滤类型
    QString filterType = SearchService::filterTypeFromIndex(searchComboBox->currentIndex());

    progressBar->setVisible(true);
    progressBar->setRange(0, 0);
    statusLabel->setText("正在搜索...");
    QApplication::processEvents();

    if (uiStateManager->isPaginationMode()) {
        // 使用服务层在分页模型中搜索
        searchService->searchInPaginationModel(paginationModel, keyword, filterType);
        statusLabel->setText(QString("搜索完成，关键词: %1").arg(keyword));
    } else {
        // 使用服务层搜索
        QStandardItemModel* searchModel = searchService->searchProjects(keyword, filterType);
        if (searchModel) {
            // 设置搜索结果模型
            tableView->setModel(searchModel);
            uiStateManager->setSearchMode(true);
            statusLabel->setText(QString("搜索完成，找到 %1 条结果").arg(searchModel->rowCount()));
        } else {
            statusLabel->setText("搜索失败");
        }
    }

    progressBar->setVisible(false);
}

void MainWindow::onClearSearch()
{
    if (uiStateManager->isPaginationMode()) {
        // 使用服务层清除分页模型搜索
        searchService->clearSearchInPaginationModel(paginationModel);
        searchLineEdit->clear();
        searchComboBox->setCurrentIndex(0);
        statusLabel->setText("已清除搜索");
    } else if (uiStateManager->isSearchMode()) {
        loadDataFromDatabase();
        uiStateManager->setSearchMode(false);
        searchLineEdit->clear();
        searchComboBox->setCurrentIndex(0);
        statusLabel->setText("已清除搜索");
    }
}

void MainWindow::onRefreshData()
{
    qDebug() << "刷新系统数据";
    
    // 记录系统日志
    dbManager->logOperation("刷新数据", "刷新系统数据", QString(), "成功");
    
    if (uiStateManager->isPaginationMode()) {
        // 使用分页模型刷新
        paginationModel->refreshData();
        searchLineEdit->clear();
        searchComboBox->setCurrentIndex(0);
        statusLabel->setText("数据已刷新");
    } else {
        loadDataFromDatabase();
        uiStateManager->setSearchMode(false);
        searchLineEdit->clear();
        searchComboBox->setCurrentIndex(0);
    }
}

QDate MainWindow::getDateFromDialog(const QString &title, const QString &label, const QDate &defaultDate)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);

    QCalendarWidget *calendar = new QCalendarWidget(&dialog);
    calendar->setSelectedDate(defaultDate);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(label));
    layout->addWidget(calendar);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        return calendar->selectedDate();
    }
    return QDate();
}

void MainWindow::onAddProject()
{
    SimpleDialog dialog(this);
    dialog.setEditMode(false);

    // 使用服务层生成唯一的项目ID
    QString newId = projectService->generateUniqueProjectId();

    // 设置项目ID
    QStringList initialData;
    initialData << newId << "" << "" << "" << "" << "" << "" << "";
    dialog.setProjectData(initialData);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList projectData = dialog.getProjectData();

        // 记录操作开始时间
        QElapsedTimer opTimer;
        opTimer.start();
        
        // 使用服务层添加项目
        QString errorMessage;
        QString projectId = projectData[0];
        if (projectService->addProject(projectData, errorMessage)) {
            // 记录操作性能
            if (performanceMonitor) {
                performanceMonitor->recordOperation("添加项目", opTimer.elapsed());
            }
            
            // 记录系统日志
            dbManager->logOperation("添加项目", 
                QString("添加项目: %1 - %2").arg(projectId, projectData[1]),
                projectId, "成功");
            
            qDebug() << "项目添加成功，开始刷新数据...";

            // 使用状态管理器标记需要在数据加载完成后选中新记录
            uiStateManager->markShouldSelectNewRecord();

            // 刷新数据
            if (uiStateManager->isPaginationMode()) {
                qDebug() << "使用分页模式刷新数据";
                // 确保跳转到第一页显示新记录
                paginationModel->setCurrentPage(1);
                paginationModel->refreshData();
                // 高亮显示新添加的记录（第一行）
                paginationModel->setHighlightedRow(0);

                // 高亮显示指定时间后自动清除
                QTimer::singleShot(UIConstants::HIGHLIGHT_DURATION_MS, paginationModel, &SimplePaginationModel::clearHighlight);
                
                // 在数据加载完成后立即重新计算总记录数，确保状态栏即时更新
                // 使用一次性连接，避免重复计算
                QMetaObject::Connection *connection = new QMetaObject::Connection();
                *connection = connect(paginationModel, &SimplePaginationModel::dataLoadingFinished, this, [this, connection]() {
                    qDebug() << "数据加载完成，立即重新计算总记录数";
                    paginationModel->forceRecalculateTotalRecords();
                    // 断开连接，避免重复执行
                    disconnect(*connection);
                    delete connection;
                }, Qt::SingleShotConnection);
            } else {
                qDebug() << "使用非分页模式刷新数据";
                loadDataFromDatabase();
                highlightNewRecord();

                // 非分页模式立即选中新记录
                if (tableView->model() && tableView->model()->rowCount() > 0) {
                    qDebug() << "非分页模式：立即选中新添加的记录，行号: 0";
                    tableView->selectRow(0);
                    uiStateManager->setCurrentSelectedRow(0);
                    tableView->setFocus();
                    tableView->scrollToTop();
                }
            }
            qDebug() << "数据刷新完成";
        } else {
            // 记录失败日志
            dbManager->logOperation("添加项目", 
                QString("添加项目失败: %1").arg(errorMessage),
                projectId, "失败");
            QMessageBox::critical(this, "错误", "添加项目失败: " + errorMessage);
        }
    }
}

void MainWindow::onDeleteProject()
{
    // 检查删除权限
    PermissionManager& permManager = PermissionManager::getInstance();
    if (!permManager.canDeleteProject()) {
        QMessageBox::warning(this, "权限不足", "您没有权限删除项目！\n\n只有管理员可以删除项目记录。");
        return;
    }
    
    if (!uiStateManager->hasSelection()) {
        QMessageBox::warning(this, "警告", "请先选择要删除的项目!");
        return;
    }

    int selectedRow = uiStateManager->getCurrentSelectedRow();
    QString projectId;
    QString projectName;

    // 根据模式获取项目信息
    if (uiStateManager->isPaginationMode()) {
        // 分页模式：从分页模型获取数据
        QModelIndex idIndex = paginationModel->index(selectedRow, 0);
        QModelIndex nameIndex = paginationModel->index(selectedRow, 1);
        projectId = paginationModel->data(idIndex, Qt::DisplayRole).toString();
        projectName = paginationModel->data(nameIndex, Qt::DisplayRole).toString();
    } else {
        // 非分页模式：从标准模型获取数据
        projectId = model->item(selectedRow, 0)->text();
        projectName = model->item(selectedRow, 1)->text();
    }

    int ret = QMessageBox::question(this, "确认删除",
                                    QString("确定要删除项目:\n\n%1 - %2\n\n此操作不可撤销!").arg(projectId, projectName),
                                    QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        // 记录操作开始时间
        QElapsedTimer opTimer;
        opTimer.start();
        
        // 关键优化：在删除操作前暂停分页模型的查询，避免数据库锁定
        // 分页模型可能正在执行查询（如计算总记录数），这会持有数据库锁
        bool wasPaused = false;
        if (uiStateManager->isPaginationMode()) {
            wasPaused = paginationModel->isQueriesPaused();
            if (!wasPaused) {
                qDebug() << "删除操作：暂停分页模型查询，避免数据库锁定";
                paginationModel->pauseQueries(true);
                // 等待一小段时间，确保正在执行的查询能够完成或检测到暂停状态
                // 注意：我们不需要等待查询完全完成，因为pauseQueries会阻止新查询
                // 只需要等待足够的时间让当前查询检测到暂停状态
                QThread::msleep(200);
                QApplication::processEvents();
            }
        }
        
        // 使用服务层删除项目
        QString errorMessage;
        bool deleteSuccess = projectService->deleteProject(projectId, errorMessage);
        
        // 恢复分页模型的查询（如果之前暂停了）
        if (uiStateManager->isPaginationMode() && !wasPaused) {
            qDebug() << "删除操作：恢复分页模型查询";
            paginationModel->pauseQueries(false);
        }
        
        if (deleteSuccess) {
            // 记录操作性能
            if (performanceMonitor) {
                performanceMonitor->recordOperation("删除项目", opTimer.elapsed());
            }
            
            // 记录系统日志
            dbManager->logOperation("删除项目", 
                QString("删除项目: %1 - %2").arg(projectId, projectName),
                projectId, "成功");
            
            qDebug() << "项目删除成功，开始刷新数据...";
            // 刷新数据
            if (uiStateManager->isPaginationMode()) {
                qDebug() << "使用分页模式刷新数据";
                paginationModel->refreshData();
                
                // 在数据加载完成后立即重新计算总记录数，确保状态栏即时更新
                // 使用一次性连接，避免重复计算
                QMetaObject::Connection *connection = new QMetaObject::Connection();
                *connection = connect(paginationModel, &SimplePaginationModel::dataLoadingFinished, this, [this, connection]() {
                    qDebug() << "数据加载完成，立即重新计算总记录数";
                    paginationModel->forceRecalculateTotalRecords();
                    // 断开连接，避免重复执行
                    disconnect(*connection);
                    delete connection;
                }, Qt::SingleShotConnection);
            } else {
                qDebug() << "使用非分页模式刷新数据";
                loadDataFromDatabase();
            }
            uiStateManager->clearSelection();
            qDebug() << "数据刷新完成";
        } else {
            // 记录失败日志
            dbManager->logOperation("删除项目", 
                QString("删除项目失败: %1 - %2").arg(projectId, errorMessage),
                projectId, "失败");
            QMessageBox::critical(this, "错误", "删除项目失败: " + errorMessage);
        }
    }
}

void MainWindow::onEditProject()
{
    if (!uiStateManager->hasSelection()) {
        QMessageBox::warning(this, "警告", "请先选择要编辑的项目!");
        return;
    }

    int selectedRow = uiStateManager->getCurrentSelectedRow();
    // 获取当前项目数据
    QStringList currentData;

    // 根据模式获取项目数据
    if (uiStateManager->isPaginationMode()) {
        // 分页模式：从分页模型获取数据
        for (int i = 0; i < 8; ++i) {
            QModelIndex index = paginationModel->index(selectedRow, i);
            QString data = paginationModel->data(index, Qt::DisplayRole).toString();
            currentData << data;
        }
    } else {
        // 非分页模式：从标准模型获取数据
        for (int i = 0; i < 8; ++i) {
            QStandardItem *item = model->item(selectedRow, i);
            if (item) {
                currentData << item->text();
            } else {
                currentData << "";
            }
        }
    }

    SimpleDialog dialog(this);
    dialog.setEditMode(true);
    dialog.setProjectData(currentData);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList projectData = dialog.getProjectData();

        // 获取项目ID用于更新
        QString projectId = currentData[0]; // 项目ID在第一列

        // 记录操作开始时间
        QElapsedTimer opTimer;
        opTimer.start();
        
        // 使用服务层更新项目
        QString errorMessage;
        if (projectService->updateProject(projectId, projectData, errorMessage)) {
            // 记录操作性能
            if (performanceMonitor) {
                performanceMonitor->recordOperation("编辑项目", opTimer.elapsed());
            }
            
            // 记录系统日志
            dbManager->logOperation("编辑项目", 
                QString("编辑项目: %1 - %2").arg(projectId, projectData[1]),
                projectId, "成功");
            
            qDebug() << "项目更新成功，开始刷新数据...";

            // 保存当前选中的行号
            int selectedRow = uiStateManager->getCurrentSelectedRow();

            // 使用状态管理器标记需要在数据加载完成后选中编辑后的记录
            uiStateManager->markShouldSelectEditedRecord(selectedRow);

            // 刷新数据
            if (uiStateManager->isPaginationMode()) {
                qDebug() << "使用分页模式刷新数据";
                paginationModel->refreshData();
            } else {
                qDebug() << "使用非分页模式刷新数据";
                loadDataFromDatabase();

                // 非分页模式立即选中编辑后的记录
                if (tableView->model() && tableView->model()->rowCount() > 0 &&
                    selectedRow >= 0 && selectedRow < tableView->model()->rowCount()) {
                    qDebug() << "非分页模式：立即选中编辑后的记录，行号:" << selectedRow;
                    tableView->selectRow(selectedRow);
                    uiStateManager->setCurrentSelectedRow(selectedRow);
                    tableView->setFocus();
                    tableView->scrollTo(tableView->model()->index(selectedRow, 0));
                }
            }
            qDebug() << "数据刷新完成";
        } else {
            QMessageBox::critical(this, "错误", "更新项目失败: " + errorMessage);
        }
    }
}

void MainWindow::onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);

    if (selected.indexes().isEmpty()) {
        uiStateManager->clearSelection();
    } else {
        uiStateManager->setCurrentSelectedRow(selected.indexes().first().row());
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // 延迟调整列宽，确保表格已经完成大小调整（避免频繁调整影响性能）
    QTimer::singleShot(UIConstants::RESIZE_DELAY_MS, this, &MainWindow::adjustColumnWidths);
}

// generateUniqueProjectId方法已移至ProjectService

void MainWindow::highlightNewRecord()
{
    if (!uiStateManager->isPaginationMode() && model->rowCount() > 0) {
        // 为非分页模式设置第一行高亮
        for (int col = 0; col < model->columnCount(); ++col) {
            QStandardItem *item = model->item(0, col);
            if (item) {
                item->setBackground(QColor(173, 216, 230)); // 浅蓝色背景
                item->setForeground(QColor(0, 0, 139)); // 深蓝色文字
            }
        }

        // 高亮显示指定时间后自动清除
        QTimer::singleShot(UIConstants::HIGHLIGHT_DURATION_MS, [this]() {
            if (model->rowCount() > 0) {
                for (int col = 0; col < model->columnCount(); ++col) {
                    QStandardItem *item = model->item(0, col);
                    if (item) {
                        item->setBackground(QBrush()); // 恢复默认背景
                        item->setForeground(QBrush()); // 恢复默认前景色
                    }
                }
            }
        });
    }
}

void MainWindow::onGenerateTestData()
{
    // 安全检查
    if (!dataGenerationWorker || !dataGenerationThread) {
        QMessageBox::critical(this, "错误", "数据生成工作线程未正确初始化");
        return;
    }

    if (!progressBar || !statusLabel) {
        QMessageBox::critical(this, "错误", "UI组件未正确初始化");
        return;
    }

    // 获取当前数据库中已有的记录数
    int currentRecordCount = dbManager->getProjectCount();

    // 产品需求：每次固定生成100万条随机测试数据
    const int recordsToGenerate = DataGenerationConstants::FIXED_TEST_DATA_COUNT;
    const QString message = QString(
        "当前已有 %1 条记录\n\n"
        "本次将【新增】%2 条随机测试数据（会在现有数据基础上叠加，不会覆盖原数据）。\n"
        "这可能需要较长时间。\n\n"
        "注意：生成过程中窗口仍可正常操作。"
    ).arg(currentRecordCount).arg(recordsToGenerate);

    // 确认对话框
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "测试数据生成",
        message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        return;
    }

    // 显示进度条
    progressBar->setVisible(true);
    progressBar->setRange(0, 0); // 不确定进度
    statusLabel->setText(QString("正在生成 %1 条测试数据，请稍候...").arg(recordsToGenerate));

    // 重要：在数据生成期间暂停分页模型的查询，避免数据库锁定
    if (uiStateManager->isPaginationMode()) {
        paginationModel->pauseQueries(true);
    }
    
    // 关键优化1：等待后台维护线程完成（索引创建可能锁定数据库）
    statusLabel->setText("等待后台维护任务完成...");
    QApplication::processEvents();
    dbManager->waitForMaintenanceThread(30000);  // 最多等待30秒
    
    // 关键优化2：在数据生成期间，暂时关闭主线程的数据库连接
    // 这样可以避免主线程和Worker线程同时访问数据库导致的锁定
    // 注意：这会导致主线程在数据生成期间无法查询数据库，但这是可以接受的
    QSqlDatabase* mainDb = dbManager->getDatabase();
    if (mainDb && mainDb->isOpen()) {
        mainDb->close();
        qDebug() << "数据生成期间：已关闭主线程数据库连接，避免锁定";
    }

    // 启动数据生成工作线程
    const int totalRecords = recordsToGenerate;

    try {
        QMetaObject::invokeMethod(dataGenerationWorker, "generateTestData",
                                  Qt::QueuedConnection, Q_ARG(int, totalRecords));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "错误", QString("启动数据生成失败: %1").arg(e.what()));
        progressBar->setVisible(false);
        statusLabel->setText("数据生成启动失败");
    } catch (...) {
        QMessageBox::critical(this, "错误", "启动数据生成时发生未知错误");
        progressBar->setVisible(false);
        statusLabel->setText("数据生成启动失败");
    }
}

void MainWindow::onDataGenerationProgress(int current, int /*total*/, double rate)
{
    // 更新进度显示
    statusLabel->setText(QString("已生成 %1 条记录，速度: %2 条/秒")
                             .arg(current).arg(QString::number(rate, 'f', 0)));
}

void MainWindow::onDataGenerationCompleted(int totalRecords, int totalTime)
{
    // 隐藏进度条
    progressBar->setVisible(false);

    // 等待一小段时间，确保数据生成线程的数据库连接完全清理
    QThread::msleep(200);

    // 获取数据库管理器
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    QSqlDatabase* mainDb = dbManager.getDatabase();

    // ========== 关键修复：完全重建主线程数据库连接 ==========
    if (mainDb) {
        QString oldConnectionName = mainDb->connectionName();

        // 关闭旧连接
        if (mainDb->isOpen()) {
            mainDb->close();
        }

        // 移除旧连接
        if (QSqlDatabase::contains(oldConnectionName)) {
            QSqlDatabase::removeDatabase(oldConnectionName);
        }

        // 创建全新连接
        ConfigManager& config = ConfigManager::getInstance();
        QString dbPath = config.getDatabasePath();

        QSqlDatabase newDb = QSqlDatabase::addDatabase("QSQLITE", "main_connection");
        newDb.setDatabaseName(dbPath);

        if (newDb.open()) {
            qDebug() << "数据生成完成：已创建全新数据库连接";

            // 设置PRAGMA
            QSqlQuery pragmaQ(newDb);
            pragmaQ.exec("PRAGMA journal_mode = WAL");
            pragmaQ.exec("PRAGMA synchronous = NORMAL");
            pragmaQ.exec("PRAGMA busy_timeout = 30000");
            pragmaQ.exec("PRAGMA temp_store = MEMORY");

            // 替换 dbManager 内部的数据库对象
            *mainDb = newDb;
        } else {
            qDebug() << "警告：无法创建新数据库连接:" << newDb.lastError().text();
        }
    }

    // ========== 关键修复：强制刷新分页模型 ==========
    if (uiStateManager->isPaginationMode() && paginationModel) {
        qDebug() << "强制刷新分页模型";

        // 更新模型的数据库指针
        paginationModel->setDatabase(mainDb);

        // 清空所有缓存
        paginationModel->clearCache();

        // 强制重新计算总记录数
        paginationModel->forceRecalculateTotalRecords();

        // 刷新数据
        paginationModel->refreshData();
    } else {
        QTimer::singleShot(100, this, [this]() {
            loadDataFromDatabase();
        });
    }

    // 恢复分页模型的查询
    if (uiStateManager->isPaginationMode()) {
        paginationModel->pauseQueries(false);
    }

    // 显示完成信息
    QMessageBox::information(this, "完成",
                             QString("测试数据生成完成！\n\n"
                                     "生成记录数: %1 条\n"
                                     "耗时: %2 秒\n"
                                     "平均速度: %3 条/秒")
                                 .arg(totalRecords)
                                 .arg(totalTime / 1000.0, 0, 'f', 1)
                                 .arg(QString::number((double)totalRecords / totalTime * 1000, 'f', 0)));

    statusLabel->setText(QString("测试数据生成完成，共 %1 条记录").arg(totalRecords));

    qDebug() << "测试数据生成完成，共" << totalRecords << "条记录，耗时" << totalTime << "毫秒";
}

void MainWindow::onDataGenerationFailed(const QString &error)
{
    // 恢复主线程的数据库连接（如果之前关闭了）
    QSqlDatabase* mainDb = dbManager->getDatabase();
    if (mainDb && !mainDb->isOpen()) {
        if (mainDb->open()) {
            qDebug() << "数据生成失败：已重新打开主线程数据库连接";
        } else {
            qDebug() << "警告：无法重新打开主线程数据库连接:" << mainDb->lastError().text();
        }
    }
    
    // 恢复分页模型的查询（即使失败也要恢复）
    if (uiStateManager->isPaginationMode()) {
        paginationModel->pauseQueries(false);
    }
    
    // 隐藏进度条
    progressBar->setVisible(false);

    // 显示错误信息
    QMessageBox::critical(this, "错误", "数据生成失败: " + error);
    statusLabel->setText("数据生成失败");

    qDebug() << "数据生成失败:" << error;
}

// DataGenerationWorker 实现
DataGenerationWorker::DataGenerationWorker(QObject *parent)
    : QObject(parent), m_shouldStop(false)
{
}

/**
 * @brief DataGenerationWorker析构函数
 * 
 * 性能优化：确保资源正确清理
 * - 停止正在进行的生成任务
 * - 清理临时文件
 * - 确保信号槽断开连接
 */
DataGenerationWorker::~DataGenerationWorker()
{
    // 停止生成任务
    m_shouldStop = true;
    
    // 清理临时进度文件（如果存在）
    QFile::remove("generation_progress.txt");
    
    // Qt的父子对象机制会自动处理QObject的清理
    // 但显式停止任务可以确保资源及时释放
}

void DataGenerationWorker::stopGeneration()
{
    m_shouldStop = true;
}

void DataGenerationWorker::generateTestData(int totalRecords)
{
    m_shouldStop = false; // 重置停止标志

    // 安全检查：限制最大记录数以防止内存问题
    if (totalRecords <= 0 || totalRecords > DataGenerationConstants::MAX_TEST_DATA_COUNT) {
        emit generationFailed(QString("无效的记录数量，请设置1到%1之间的数值")
                             .arg(DataGenerationConstants::MAX_TEST_DATA_COUNT));
        return;
    }
    // 预定义的项目名称（100个）
    QStringList projectNames = {
        "智能办公系统", "电商平台开发", "移动应用开发", "数据分析平台", "客户管理系统",
        "财务管理系统", "人力资源系统", "库存管理系统", "销售管理系统", "项目管理系统",
        "在线教育平台", "医疗管理系统", "物流管理系统", "酒店管理系统", "餐厅管理系统",
        "图书馆管理系统", "学校管理系统", "企业门户网站", "社交网络平台", "内容管理系统",
        "博客系统", "论坛系统", "在线商城", "支付系统", "会员管理系统",
        "活动管理系统", "预约系统", "问卷调查系统", "报表系统", "监控系统",
        "安全管理系统", "权限管理系统", "文件管理系统", "备份系统", "恢复系统",
        "测试管理系统", "部署系统", "运维管理系统", "性能监控系统", "日志分析系统",
        "API网关系统", "微服务架构", "容器化平台", "云存储系统", "CDN系统",
        "搜索引擎系统", "推荐系统", "广告系统", "营销系统", "客服系统",
        "工单系统", "知识库系统", "培训系统", "考试系统", "证书系统",
        "积分系统", "优惠券系统", "促销系统", "会员卡系统", "积分商城",
        "游戏平台", "直播系统", "视频系统", "音频系统", "图片系统",
        "文档系统", "表格系统", "演示系统", "绘图系统", "设计系统",
        "开发工具", "测试工具", "部署工具", "监控工具", "分析工具",
        "自动化工具", "CI/CD工具", "版本控制工具", "代码审查工具", "性能测试工具",
        "安全测试工具", "接口测试工具", "UI测试工具", "压力测试工具", "兼容性测试工具",
        "移动端应用", "Web端应用", "桌面端应用", "小程序应用", "H5应用",
        "物联网应用", "人工智能应用", "区块链应用", "大数据应用", "云计算应用"
    };

    // 预定义的项目经理姓名（50个）
    QStringList managerNames = {
        "Lucy", "Sirius", "Mark", "宋江(及时雨)", "卢俊义(玉麒麟)", "吴用(智多星)", "公孙胜", "林冲(豹子头)", "鲁智深(花和尚)", "武松(行者)",
        "李逵(黑旋风)", "陈十四", "杨志(青面兽)", "潘金莲", "西门庆", "潘巧云", "高俅", "蔡京", "童贯", "李应",
        "张清", "刘唐", "史进", "穆弘", "李俊", "张顺", "朱武", "黄信", "孙立", "宣赞",
        "邓飞", "燕顺", "蒋敬", "安道全", "戚三七", "欧鹏", "魏定国", "喻四十", "彭玘", "韩滔",
        "扈三娘", "鲍旭", "孔明", "孟康", "丁得孙", "施恩", "邹润", "李云龙", "楚云飞", "赵刚"
    };

    // 预定义的状态
    QStringList statuses = {"进行中", "已完成", "暂停", "计划中", "已取消"};

    // 预定义的描述模板
    QStringList descriptions = {
        "这是一个重要的项目，需要认真对待。",
        "项目涉及多个模块，需要团队协作完成。",
        "技术难度较高，需要经验丰富的开发人员。",
        "项目时间紧迫，需要加班完成。",
        "项目预算充足，可以投入更多资源。",
        "项目风险较高，需要制定详细的风险控制计划。",
        "项目涉及新技术，需要学习成本。",
        "项目规模较大，需要分阶段实施。",
        "项目客户要求严格，需要高质量交付。",
        "项目需要与第三方系统集成。"
    };

    // 使用常量定义批量插入大小，提高代码可维护性
    const int batchSize = DataGenerationConstants::BATCH_SIZE;

    qDebug() << "开始生成" << totalRecords << "条测试数据...";

    // 预分配内存以提高性能
    QList<QStringList> batchData;
    batchData.reserve(batchSize);

    // 重要：该Worker运行在子线程中，不能复用主线程创建的QSqlDatabase连接（否则可能导致卡死/未响应）
    // 为当前线程创建独立数据库连接
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.initialize()) {
        qDebug() << "警告：配置管理器初始化失败，尝试继续使用默认数据库路径";
    }
    const QString dbPath = config.getDatabasePath();
    // 使用唯一的连接名，确保真正独立（包含线程ID和时间戳）
    const QString connName = QString("pm_datagen_%1_%2")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
        .arg(QDateTime::currentMSecsSinceEpoch());
    
    // 确保连接名唯一，避免复用旧连接
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    
    QSqlDatabase localDb = QSqlDatabase::addDatabase("QSQLITE", connName);
    localDb.setDatabaseName(dbPath);
    
    // 添加重试机制打开数据库
    int openRetryCount = 0;
    const int maxOpenRetries = 5;
    bool dbOpened = false;
    while (openRetryCount < maxOpenRetries && !dbOpened) {
        dbOpened = localDb.open();
        if (!dbOpened) {
            openRetryCount++;
            if (openRetryCount < maxOpenRetries) {
                QThread::msleep(100 * openRetryCount);
                qDebug() << "数据库打开失败，重试" << openRetryCount << "/" << maxOpenRetries;
            }
        }
    }
    
    if (!dbOpened) {
        QSqlDatabase::removeDatabase(connName);
        emit generationFailed("无法打开数据库（重试" + QString::number(maxOpenRetries) + "次后仍失败）: " + localDb.lastError().text());
        return;
    }
    
    // 注意：连接将在函数结束时手动清理
    {
        // 关键优化：临时删除FTS触发器，避免每次INSERT都触发FTS更新（这是最大的性能瓶颈）
        // 数据生成完成后会重新创建触发器并同步FTS数据
        QSqlQuery triggerQuery(localDb);
        QStringList triggerNames = {
            "trg_projects_fts_ai",  // INSERT触发器
            "trg_projects_fts_au",  // UPDATE触发器
            "trg_projects_fts_ad"   // DELETE触发器
        };
        
        bool triggersDeleted = false;
        for (const QString &triggerName : triggerNames) {
            QString dropSql = QString("DROP TRIGGER IF EXISTS %1").arg(triggerName);
            if (triggerQuery.exec(dropSql)) {
                triggersDeleted = true;
                qDebug() << "已临时删除FTS触发器:" << triggerName;
            }
        }
        
        if (triggersDeleted) {
            qDebug() << "FTS触发器已删除，数据生成速度将大幅提升";
        }
        
        // 批量插入性能优化：临时调整PRAGMA以提升插入速度（数据生成场景）
        QSqlQuery pragmaQ(localDb);
        pragmaQ.exec("PRAGMA journal_mode = WAL");           // WAL模式提高并发性能
        pragmaQ.exec("PRAGMA synchronous = OFF");             // 关键优化：数据生成时关闭同步，大幅提升速度（数据生成场景可接受）
        pragmaQ.exec("PRAGMA temp_store = MEMORY");           // 临时表存储在内存
        pragmaQ.exec("PRAGMA cache_size = -128000");          // 128MB缓存（增大缓存）
        pragmaQ.exec("PRAGMA mmap_size = 2147483648");        // 2GB内存映射（增大内存映射）
        pragmaQ.exec("PRAGMA page_size = 4096");             // 4KB页面大小
        pragmaQ.exec("PRAGMA threads = 4");                  // 多线程支持
        pragmaQ.exec("PRAGMA busy_timeout = 60000");          // 60秒超时，等待锁释放
        pragmaQ.exec("PRAGMA locking_mode = EXCLUSIVE");      // 独占模式，避免锁定问题
        
        qDebug() << "PRAGMA优化设置完成（synchronous=OFF，缓存和内存映射已增大）";
    }

    // 获取当前数据库中最大的测试数据项目ID（仅匹配 P+8位数字），确保生成的ID不与已有记录冲突
    // 说明：不能使用 LIKE 'P%'，因为会误匹配到 PRJ001/PRJ002 这类人工ID，导致解析失败并从 P00000001 重新开始生成，触发 UNIQUE 约束。
    QSqlQuery maxIdQuery(localDb);
    long long maxProjectIdNum = 0;
    maxIdQuery.prepare(R"(
        SELECT MAX(CAST(SUBSTR(project_id, 2) AS INTEGER))
        FROM projects
        WHERE project_id GLOB 'P[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]'
    )");
    if (maxIdQuery.exec() && maxIdQuery.next()) {
        // 注意：若无匹配记录，value 为 NULL
        bool ok = false;
        const QString v = maxIdQuery.value(0).toString();
        const long long num = v.toLongLong(&ok);
        if (ok && num > 0) {
            maxProjectIdNum = num;
        }
    }
    
    int startFrom = 0; // 生成数据的起始索引（用于进度跟踪）
    long long baseRecordIndex = maxProjectIdNum; // 用于生成唯一ID的基础索引（基于最大ID）
    
    int insertedCount = 0;
    QElapsedTimer timer;
    timer.start(); // 使用QElapsedTimer更准确测量时间

    qDebug() << "最大项目ID: P" << QString("%1").arg(maxProjectIdNum, 8, 10, QChar('0'));
    qDebug() << "将生成" << totalRecords << "条新记录，新记录ID将从 P" << QString("%1").arg(baseRecordIndex + 1, 8, 10, QChar('0')) << "开始";

    try {
        /**
         * @brief 批量生成数据循环
         * 
         * 性能优化策略：
         * 1. 分批处理：每次处理batchSize条记录，避免单次事务过大
         * 2. 小事务：每批使用独立事务，失败时只影响当前批次
         * 3. 进度更新：按指定间隔更新进度，避免频繁更新影响性能
         * 4. 内存优化：预分配内存，减少动态分配开销
         */
        for (int i = startFrom; i < totalRecords; i += batchSize) {
            // 检查是否需要停止（支持用户取消操作）
            if (m_shouldStop) {
                emit generationFailed("数据生成被用户取消");
                return;
            }

            // 计算当前批次大小（最后一批可能小于batchSize）
            int currentBatchSize = qMin(batchSize, totalRecords - i);

            // 开始小事务（每批独立事务，提高容错性）
            // 使用BEGIN IMMEDIATE获得独占锁，避免其他连接干扰
            // 添加重试机制，避免database is locked错误
            int retryCount = 0;
            const int maxRetries = 10;  // 增加重试次数
            bool transactionStarted = false;
            while (retryCount < maxRetries && !transactionStarted) {
                // 使用BEGIN IMMEDIATE立即获得写锁，避免等待
                QSqlQuery beginQuery(localDb);
                if (beginQuery.exec("BEGIN IMMEDIATE")) {
                    transactionStarted = true;
                } else {
                    retryCount++;
                    if (retryCount < maxRetries) {
                        int delayMs = qMin(500 * retryCount, 2000); // 递增延迟，最多2秒
                        QThread::msleep(delayMs);
                        qDebug() << "事务开始失败，重试" << retryCount << "/" << maxRetries << "错误:" << beginQuery.lastError().text();
                    }
                }
            }
            if (!transactionStarted) {
                emit generationFailed("无法开始数据库事务（重试" + QString::number(maxRetries) + "次后仍失败）: " + localDb.lastError().text());
                return;
            }

            // 准备批量插入数据
            batchData.clear();

            for (int j = 0; j < currentBatchSize; ++j) {
                QStringList projectData;

                // 生成项目ID（基于当前记录数 + 当前生成的索引，确保ID唯一）
                // baseRecordIndex 是已有记录数，i 是当前批次起始位置，j 是批次内索引
                long long recordIndex = static_cast<long long>(baseRecordIndex) + static_cast<long long>(i) + j + 1;
                QString projectId = QString("P%1").arg(recordIndex, 8, 10, QChar('0'));

                // 随机选择项目名称
                QString projectName = projectNames[QRandomGenerator::global()->bounded(projectNames.size())];
                projectName += QString("_%1").arg(recordIndex);

                // 随机选择项目经理
                QString manager = managerNames[QRandomGenerator::global()->bounded(managerNames.size())];

                // 生成随机日期
                QDate startDate = QDate::currentDate().addDays(-QRandomGenerator::global()->bounded(365));
                QDate endDate = startDate.addDays(QRandomGenerator::global()->bounded(180) + 30);

                // 生成随机预算
                double budget = QRandomGenerator::global()->bounded(5000000, 10000000) +
                                QRandomGenerator::global()->generateDouble() * 1000;

                // 随机选择状态
                QString status = statuses[QRandomGenerator::global()->bounded(statuses.size())];

                // 随机选择描述
                QString description = descriptions[QRandomGenerator::global()->bounded(descriptions.size())];
                description += QString(" 项目编号: %1").arg(projectId);

                projectData << projectId << projectName << manager
                            << startDate.toString("yyyy-MM-dd")
                            << endDate.toString("yyyy-MM-dd")
                            << QString::number(budget, 'f', 2) << status << description;

                batchData << projectData;
            }

            // 高性能批量插入：使用多值INSERT语法，一次插入多条记录（比循环单条INSERT快10-50倍）
            // 将batchData分成多个VALUES_PER_INSERT大小的子批次，构建多值INSERT语句
            const int valuesPerInsert = DataGenerationConstants::VALUES_PER_INSERT;
            for (int subBatchStart = 0; subBatchStart < currentBatchSize; subBatchStart += valuesPerInsert) {
                const int subBatchSize = qMin(valuesPerInsert, currentBatchSize - subBatchStart);
                
                // 构建多值INSERT SQL：INSERT INTO ... VALUES (?,?,...), (?,?,...), ...
                // 注意：SQLite要求所有占位符按顺序绑定，每个记录8个字段
                QString sql = "INSERT INTO projects (project_id, project_name, manager, start_date, end_date, budget, status, description) VALUES ";
                QStringList valueParts;
                QVariantList bindValues;
                
                // 为每条记录构建VALUES部分并收集绑定值
                for (int subIdx = 0; subIdx < subBatchSize; ++subIdx) {
                    if (subIdx > 0) {
                        valueParts << ",";
                    }
                    valueParts << "(?, ?, ?, ?, ?, ?, ?, ?)";
                    
                    // 按顺序添加8个字段的绑定值
                    const QStringList &projectData = batchData[subBatchStart + subIdx];
                    if (projectData.size() < 8) {
                        localDb.rollback();
                        emit generationFailed(QString("数据格式错误：记录字段数不足（需要8个，实际%1个）").arg(projectData.size()));
                        return;
                    }
                    for (int k = 0; k < 8; ++k) {
                        bindValues << projectData[k];
                    }
                }
                sql += valueParts.join("");
                
                // 验证参数数量匹配（调试用）
                const int expectedParams = subBatchSize * 8;
                if (bindValues.size() != expectedParams) {
                    localDb.rollback();
                    emit generationFailed(QString("参数数量不匹配：SQL需要%1个参数，实际绑定%2个").arg(expectedParams).arg(bindValues.size()));
                    return;
                }
                
                // 执行多值INSERT（一次插入多条记录，性能大幅提升）
                QSqlQuery insertQuery(localDb);
                if (!insertQuery.prepare(sql)) {
                    localDb.rollback();
                    emit generationFailed("SQL准备失败: " + insertQuery.lastError().text());
                    return;
                }
                
                // 按顺序绑定所有参数
                for (int idx = 0; idx < bindValues.size(); ++idx) {
                    insertQuery.bindValue(idx, bindValues[idx]);
                }
                
                if (!insertQuery.exec()) {
                    localDb.rollback();
                    emit generationFailed("批量插入数据失败: " + insertQuery.lastError().text());
                    return;
                }
            }

            // 提交小事务（使用COMMIT显式提交，释放锁）
            QSqlQuery commitQuery(localDb);
            if (!commitQuery.exec("COMMIT")) {
                localDb.rollback();  // 如果COMMIT失败，尝试ROLLBACK
                emit generationFailed("提交事务失败: " + commitQuery.lastError().text());
                return;
            }

            insertedCount += currentBatchSize;

            // 发送进度更新信号（按指定间隔更新，避免频繁更新影响性能）
            if (insertedCount % DataGenerationConstants::PROGRESS_UPDATE_INTERVAL == 0) {
                qint64 elapsed = timer.elapsed(); // 使用elapsed()获取经过的毫秒数
                double rate = (double)insertedCount / elapsed * 1000; // 每秒插入记录数
                emit progressUpdated(insertedCount, totalRecords, rate);

                // 保存进度
                saveProgress(insertedCount, totalRecords);

                // 注意：WAL checkpoint在批量插入期间可能导致锁定，已移除
                // 数据会在事务提交时自动写入WAL文件，程序退出或正常关闭时会自动checkpoint
            }
        }

        qint64 totalTime = timer.elapsed(); // 使用elapsed()获取总耗时

        // 关键优化：数据生成完成后，重新创建FTS触发器并同步FTS数据
        qDebug() << "数据生成完成，开始重建FTS触发器和同步FTS数据...";
        QElapsedTimer ftsTimer;
        ftsTimer.start();
        
        // 重新创建FTS触发器
        QSqlQuery triggerQuery(localDb);
        QStringList triggerSql = {
            // 插入触发器
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ai
                AFTER INSERT ON projects
                BEGIN
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )",
            // 删除触发器
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ad
                AFTER DELETE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                END
            )",
            // 更新触发器
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_au
                AFTER UPDATE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )"
        };
        
        for (const QString &sql : triggerSql) {
            if (!triggerQuery.exec(sql)) {
                qDebug() << "FTS触发器创建失败（非致命）:" << triggerQuery.lastError().text();
            }
        }
        qint64 triggerTime = ftsTimer.elapsed();
        qDebug() << "FTS触发器重建完成，耗时:" << triggerTime << "ms";
        
        // 批量同步FTS数据（只同步新插入的数据，比全量同步快得多）
        // 获取FTS表中最大的rowid
        QSqlQuery maxRowIdQuery(localDb);
        qint64 maxFtsRowId = 0;
        if (maxRowIdQuery.exec("SELECT COALESCE(MAX(rowid), 0) FROM projects_fts") && maxRowIdQuery.next()) {
            maxFtsRowId = maxRowIdQuery.value(0).toLongLong();
        }
        
        // 只同步新插入的数据（id > maxFtsRowId）
        QSqlQuery syncQuery(localDb);
        syncQuery.prepare(R"(
            INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
            SELECT id, project_id, project_name, manager, status, description
            FROM projects
            WHERE id > ?
        )");
        syncQuery.bindValue(0, maxFtsRowId);
        syncQuery.setForwardOnly(true);
        
        if (syncQuery.exec()) {
            qint64 syncTime = ftsTimer.elapsed() - triggerTime;
            qDebug() << "FTS数据同步完成，同步记录数:" << syncQuery.numRowsAffected() 
                     << "，耗时:" << syncTime << "ms";
        } else {
            qDebug() << "FTS数据同步失败（非致命）:" << syncQuery.lastError().text();
        }
        
        // 恢复PRAGMA设置（数据生成完成后恢复安全性设置）
        QSqlQuery restorePragma(localDb);
        restorePragma.exec("PRAGMA synchronous = NORMAL");  // 恢复同步设置
        restorePragma.exec("PRAGMA locking_mode = NORMAL"); // 恢复锁定模式
        
        qint64 ftsTotalTime = ftsTimer.elapsed();
        qDebug() << "数据生成和FTS同步全部完成，数据生成耗时:" << totalTime << "ms，"
                 << "FTS同步耗时:" << ftsTotalTime << "ms，生成记录数:" << insertedCount;

        // 清理进度文件
        QFile::remove("generation_progress.txt");

        emit generationCompleted(insertedCount, static_cast<int>(totalTime));

    } catch (const std::exception& e) {
        emit generationFailed(QString("数据生成异常: %1").arg(e.what()));
    }
    
    // 关键优化：确保所有查询都完成后再清理数据库连接
    // 清理数据库连接（确保所有查询都完成）
    if (localDb.isOpen()) {
        // 确保所有事务都提交（如果有未提交的事务）
        QSqlQuery commitQuery(localDb);
        commitQuery.exec("COMMIT");  // 确保没有未提交的事务
        
        localDb.close();
        qDebug() << "数据生成线程：数据库连接已关闭";
    }
    
    // 等待一小段时间，确保所有数据库操作完成
    QThread::msleep(100);
    
    // 移除数据库连接（延迟移除，确保所有操作完成）
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
        qDebug() << "数据生成线程：数据库连接已移除";
    }
}

void DataGenerationWorker::saveProgress(int currentCount, int totalCount)
{
    QFile file("generation_progress.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << currentCount << "\n" << totalCount;
        file.close();
    }
}

int DataGenerationWorker::loadProgress()
{
    QFile file("generation_progress.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        int currentCount = in.readLine().toInt();
        file.close();
        return currentCount;
    }
    return 0;
}

void MainWindow::onDataLoadingFinished()
{
    // 检查状态管理器中的选择标记
    if (uiStateManager->m_shouldSelectNewRecord) {
        // 确保表格有数据
        if (tableView->model() && tableView->model()->rowCount() > 0) {
            qDebug() << "数据加载完成，选中新添加的记录，行号: 0";

            // 选中第一行（新添加的记录）
            tableView->selectRow(0);
            uiStateManager->setCurrentSelectedRow(0);

            // 确保选中状态可见
            tableView->setFocus();
            tableView->scrollToTop();
        }
        uiStateManager->clearSelectionMarks();
    }
    // 如果需要选中编辑后的记录
    else if (uiStateManager->m_shouldSelectEditedRecord) {
        int editedRow = uiStateManager->m_editedRecordRow;
        
        // 确保表格有数据且行号有效
        if (tableView->model() && tableView->model()->rowCount() > 0 &&
            editedRow >= 0 && editedRow < tableView->model()->rowCount()) {
            qDebug() << "数据加载完成，选中编辑后的记录，行号:" << editedRow;

            // 选中编辑后的记录
            tableView->selectRow(editedRow);
            uiStateManager->setCurrentSelectedRow(editedRow);

            // 确保选中状态可见
            tableView->setFocus();
            tableView->scrollTo(tableView->model()->index(editedRow, 0));
        }
        uiStateManager->clearSelectionMarks();
    }
}

void MainWindow::onTotalRecordsChanged(int totalRecords)
{
    // 当总记录数变化时，更新状态栏显示
    if (uiStateManager->isPaginationMode()) {
        uiStateManager->updateStatusBar(statusLabel, totalRecords, 0.0, QStringList());
    }
    
    // 同步更新性能监控面板的总记录数
    if (performanceMonitor) {
        // 获取数据库文件大小
        ConfigManager& config = ConfigManager::getInstance();
        QString dbPath = config.getDatabasePath();
        QFile dbFile(dbPath);
        qint64 dbSize = dbFile.size();
        performanceMonitor->updateDatabaseStats(totalRecords, dbSize);
    }
}

void MainWindow::onExportCSV()
{
    // 检查是否有数据
    if (uiStateManager->isPaginationMode()) {
        if (paginationModel->getTotalRecords() == 0) {
            QMessageBox::information(this, "提示", "没有数据可以导出！");
            return;
        }
    } else {
        if (!tableView->model() || tableView->model()->rowCount() == 0) {
            QMessageBox::information(this, "提示", "没有数据可以导出！");
            return;
        }
    }

    // 获取保存文件路径（只选择目录，文件名自动生成）
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString baseFileName = QString("项目数据_%1")
                            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    
    QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        "选择CSV文件保存目录",
        defaultDir
    );

    if (selectedDir.isEmpty()) {
        return; // 用户取消了选择
    }

    QString basePath = QString("%1/%2").arg(selectedDir, baseFileName);

    // 显示进度条
    progressBar->setVisible(true);
    progressBar->setRange(0, 0); // 不确定进度
    statusLabel->setText("正在准备导出CSV文件...");
    QApplication::processEvents();

    try {
        // 连接导出服务信号
        connect(exportService, &ExportService::exportProgress, this, [this](int current, int total) {
            progressBar->setRange(0, total);
            progressBar->setValue(current);
            statusLabel->setText(QString("正在导出: %1/%2 条记录...").arg(current).arg(total));
            QApplication::processEvents();
        });

        bool success = false;
        int totalRecords = 0;

        if (uiStateManager->isPaginationMode()) {
            // 分页模式：使用服务层从数据库流式导出
            qDebug() << "分页模式：开始流式导出所有数据到CSV文件";
            totalRecords = paginationModel->getTotalRecords();
            progressBar->setRange(0, totalRecords);
            
            success = exportService->exportFromDatabase(
                dbManager->getDatabase(),
                basePath,
                [this](int current, int total) {
                    progressBar->setValue(current);
                    statusLabel->setText(QString("正在导出: %1/%2 条记录...").arg(current).arg(total));
                    QApplication::processEvents();
                }
            );
        } else {
            // 非分页模式：使用服务层导出当前模型数据
            qDebug() << "非分页模式：导出当前显示的数据到CSV文件";
            totalRecords = tableView->model()->rowCount();
            progressBar->setRange(0, totalRecords);
            
            QString filePath = basePath + ".csv";
            success = exportService->exportToCSV(
                tableView->model(),
                filePath,
                [this](int current, int total) {
                    progressBar->setValue(current);
                    statusLabel->setText(QString("正在导出: %1/%2 条记录...").arg(current).arg(total));
                    QApplication::processEvents();
                }
            );
        }

        // 隐藏进度条
        progressBar->setVisible(false);

        if (success) {
            QStringList createdFiles = exportService->getCreatedFiles();
            int exportedRecords = exportService->getExportedRecordCount();
            
            // 记录系统日志
            dbManager->logOperation("导出CSV", 
                QString("导出CSV文件: %1 条记录，%2 个文件，保存目录: %3")
                    .arg(exportedRecords).arg(createdFiles.size()).arg(selectedDir),
                QString(), "成功");
            
            QMessageBox::information(this, "导出成功", 
                QString("CSV文件导出成功！\n\n"
                       "导出记录数: %1 条\n"
                       "总记录数: %2 条\n"
                       "生成文件数: %3 个\n"
                       "保存目录: %4")
                      .arg(exportedRecords)
                      .arg(totalRecords)
                      .arg(createdFiles.size())
                      .arg(selectedDir));
            
            statusLabel->setText(QString("CSV导出完成，共导出 %1 条记录到 %2 个文件")
                                 .arg(exportedRecords).arg(createdFiles.size()));
            
            qDebug() << "CSV导出完成，文件数:" << createdFiles.size() << "记录数:" << exportedRecords;
        } else {
            // 记录失败日志
            dbManager->logOperation("导出CSV", 
                "CSV导出失败",
                QString(), "失败");
            QMessageBox::critical(this, "错误", "CSV导出失败");
            statusLabel->setText("CSV导出失败");
        }

    } catch (const std::exception& e) {
        progressBar->setVisible(false);
        QMessageBox::critical(this, "错误", QString("导出CSV时发生异常: %1").arg(e.what()));
        statusLabel->setText("CSV导出失败");
    } catch (...) {
        progressBar->setVisible(false);
        QMessageBox::critical(this, "错误", "导出CSV时发生未知错误");
        statusLabel->setText("CSV导出失败");
    }
}

void MainWindow::onShowPerformanceMonitor()
{
    if (performanceMonitor) {
        // 更新数据库统计
        // 优先使用分页模型中的总记录数（更准确，实时同步）
        int totalRecords = 0;
        if (uiStateManager->isPaginationMode() && paginationModel) {
            totalRecords = paginationModel->getTotalRecords();
        } else {
            // 非分页模式或分页模型不可用时，使用数据库管理器
            totalRecords = dbManager->getProjectCount();
        }
        
        // 获取数据库文件大小
        // 从配置管理器获取数据库路径（符合千万级产品标准化要求）
        ConfigManager& config = ConfigManager::getInstance();
        QString dbPath = config.getDatabasePath();
        QFile dbFile(dbPath);
        qint64 dbSize = dbFile.size();
        
        performanceMonitor->updateDatabaseStats(totalRecords, dbSize);
        
        // 显示监控面板
        performanceMonitor->show();
        performanceMonitor->raise();
        performanceMonitor->activateWindow();
    }
}

void MainWindow::onShowSystemLog()
{
    if (systemLogWindow) {
        // 检查权限并显示窗口
        if (systemLogWindow->checkPermissionAndShow()) {
            systemLogWindow->show();
            systemLogWindow->raise();
            systemLogWindow->activateWindow();
        }
    }
}

void MainWindow::onShowUserManagement()
{
    if (userManagementWindow) {
        // 检查权限（只有管理员可以访问）
        PermissionManager& permManager = PermissionManager::getInstance();
        if (!permManager.isAdmin()) {
            QMessageBox::warning(this, "权限不足", "您没有权限访问用户管理！\n\n只有管理员可以管理用户。");
            return;
        }
        
        // 刷新用户列表
        userManagementWindow->refreshUsers();
        
        // 显示窗口
        userManagementWindow->show();
        userManagementWindow->raise();
        userManagementWindow->activateWindow();
    }
}

void MainWindow::onShowReport()
{
    // 检查报表服务是否已初始化
    if (!reportService) {
        QMessageBox::critical(this, "错误", "报表服务未初始化，无法打开报表窗口");
        // 记录错误日志
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("报表操作", "打开项目报表窗口", QString(), "失败：报表服务未初始化");
        return;
    }
    
    // 延迟创建报表窗口（按需创建）
    bool isNewWindow = (reportWindow == nullptr);
    if (!reportWindow) {
        try {
            qDebug() << "MainWindow: 正在创建报表窗口...";
            reportWindow = new ReportWindow(reportService, this);
            qDebug() << "MainWindow: 报表窗口创建成功";
            // 注意：报表窗口的构造函数中已经记录了"打开报表窗口"的日志
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "错误", QString("创建报表窗口失败: %1").arg(e.what()));
            // 记录错误日志
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", "打开项目报表窗口", QString(), QString("失败：%1").arg(e.what()));
            return;
        } catch (...) {
            QMessageBox::critical(this, "错误", "创建报表窗口时发生未知错误");
            // 记录错误日志
            DatabaseManager& dbManager = DatabaseManager::getInstance();
            dbManager.logOperation("报表操作", "打开项目报表窗口", QString(), "失败：未知错误");
            return;
        }
    }
    
    // 先显示窗口，让用户看到界面（避免阻塞感）
    // 注意：如果是新创建的窗口，构造函数中已经记录了日志；如果是已存在的窗口，这里不需要重复记录
    reportWindow->show();
    reportWindow->raise();
    reportWindow->activateWindow();
    
    // 关键优化：延迟刷新报表数据，让窗口先显示出来（避免启动卡顿）
    // 延迟150ms执行，确保UI已经渲染完成，然后再加载数据
    QTimer::singleShot(150, reportWindow, &ReportWindow::refreshReport);
}

void MainWindow::onLogout()
{
    // 确认退出系统
    int ret = QMessageBox::question(this, "退出系统", 
        "确定要退出系统并返回登录窗口吗？",
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        qDebug() << "用户确认退出系统";
        // 设置退出系统标志
        m_logoutRequested = true;
        // 发出退出系统信号
        emit logoutRequested();
        qDebug() << "已发出退出系统信号";
        // 关闭主窗口（这会触发closeEvent，然后退出事件循环）
        close();
    }
}

void MainWindow::onClearAllData()
{
    // 检查权限（只有管理员可以清空数据）
    PermissionManager& permManager = PermissionManager::getInstance();
    if (!permManager.isAdmin()) {
        QMessageBox::warning(this, "权限不足", "只有管理员可以清空所有数据！");
        return;
    }

    // 确认对话框
    int ret = QMessageBox::question(this, "确认清空",
                                    "警告：此操作将删除所有项目数据，不可恢复！\n\n"
                                    "确定要清空所有数据吗？",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);

    if (ret != QMessageBox::Yes) {
        return;
    }

    // 二次确认
    ret = QMessageBox::question(this, "最终确认",
                                "请再次确认：真的要删除所有项目数据吗？\n\n"
                                "此操作无法撤销！",
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No);

    if (ret != QMessageBox::Yes) {
        return;
    }

    // 显示进度
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);  // 不确定进度
    statusLabel->setText("正在清空数据...");
    QApplication::processEvents();

    // 执行清空操作
    QSqlDatabase* db = dbManager->getDatabase();
    if (!db || !db->isOpen()) {
        QMessageBox::critical(this, "错误", "数据库未连接");
        progressBar->setVisible(false);
        return;
    }

    // 开始事务
    QSqlQuery query(*db);
    if (!query.exec("BEGIN IMMEDIATE")) {
        QMessageBox::critical(this, "错误", "无法开始事务: " + query.lastError().text());
        progressBar->setVisible(false);
        return;
    }

    bool success = true;

    // 清空 projects 表
    if (!query.exec("DELETE FROM projects")) {
        qDebug() << "清空 projects 表失败:" << query.lastError().text();
        success = false;
    }

    // 清空 FTS 表
    if (success && !query.exec("DELETE FROM projects_fts")) {
        qDebug() << "清空 FTS 表失败:" << query.lastError().text();
        success = false;
    }

    // 重置自增序列
    if (success && !query.exec("DELETE FROM sqlite_sequence WHERE name='projects'")) {
        qDebug() << "重置自增序列失败:" << query.lastError().text();
        // 这个失败不影响主要功能，继续
    }

    if (success) {
        // 提交事务
        if (!db->commit()) {
            qDebug() << "提交事务失败:" << db->lastError().text();
            db->rollback();
            success = false;
        }
    } else {
        db->rollback();
    }

    if (success) {
        // 记录日志
        dbManager->logOperation("清空数据", "管理员清空了所有项目数据", QString(), "成功");

        // 刷新界面
        if (uiStateManager->isPaginationMode() && paginationModel) {
            paginationModel->clearCache();
            paginationModel->forceRecalculateTotalRecords();
            paginationModel->refreshData();
        } else {
            loadDataFromDatabase();
        }

        QMessageBox::information(this, "成功", "所有数据已清空！");
        statusLabel->setText("数据已清空");
    } else {
        QMessageBox::critical(this, "错误", "清空数据失败，请重试");
        statusLabel->setText("清空数据失败");
    }

    progressBar->setVisible(false);
}



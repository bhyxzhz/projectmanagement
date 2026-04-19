#ifndef MAINWINDOWUIBUILDER_H
#define MAINWINDOWUIBUILDER_H

#include <QObject>

QT_BEGIN_NAMESPACE
class QMainWindow;
class QTableView;
class QToolBar;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QStandardItemModel;
class SimplePaginationModel;
class PaginationController;
class UIStateManager;
class PerformanceMonitor;
QT_END_NAMESPACE

/**
 * @file mainwindowuibuilder.h
 * @brief MainWindow UI构建器 - 进一步拆分MainWindow职责
 * 
 * 功能：
 * 1. 负责UI组件的创建和初始化
 * 2. 统一管理工具栏、搜索栏、状态栏的创建逻辑
 * 3. 减少MainWindow的代码量，提高可维护性
 * 
 * 符合千万级产品标准化要求：
 * - 职责单一：专门负责UI构建
 * - 降低MainWindow复杂度
 * - 提高代码可测试性
 */

/**
 * @brief MainWindow UI构建器
 * 
 * 负责创建和初始化MainWindow的所有UI组件，
 * 将UI创建逻辑从MainWindow中分离出来。
 */
class MainWindowUIBuilder : public QObject
{
    Q_OBJECT

public:
    explicit MainWindowUIBuilder(QObject *parent = nullptr);

    /**
     * @brief 构建UI组件
     * @param mainWindow 主窗口
     * @param tableView 表格视图（输出参数）
     * @param model 标准模型（输出参数）
     * @param paginationModel 分页模型（输出参数）
     * @param paginationController 分页控制器（输出参数）
     * @param searchLineEdit 搜索输入框（输出参数）
     * @param searchComboBox 搜索类型下拉框（输出参数）
     * @param searchButton 搜索按钮（输出参数）
     * @param clearButton 清除按钮（输出参数）
     * @param statusLabel 状态标签（输出参数）
     * @param progressBar 进度条（输出参数）
     * @param uiStateManager UI状态管理器
     */
    void buildUI(QMainWindow *mainWindow,
                 QTableView *&tableView,
                 QStandardItemModel *&model,
                 SimplePaginationModel *&paginationModel,
                 PaginationController *&paginationController,
                 QLineEdit *&searchLineEdit,
                 QComboBox *&searchComboBox,
                 QPushButton *&searchButton,
                 QPushButton *&clearButton,
                 QLabel *&statusLabel,
                 QProgressBar *&progressBar,
                 UIStateManager *uiStateManager);

private:
    /**
     * @brief 创建工具栏
     */
    void createToolbar(QMainWindow *mainWindow);

    /**
     * @brief 创建搜索栏
     */
    void createSearchBar(QMainWindow *mainWindow,
                        QLineEdit *&searchLineEdit,
                        QComboBox *&searchComboBox,
                        QPushButton *&searchButton,
                        QPushButton *&clearButton);

    /**
     * @brief 创建状态栏
     */
    void createStatusBar(QMainWindow *mainWindow,
                        QLabel *&statusLabel,
                        QProgressBar *&progressBar);

    /**
     * @brief 设置表格视图属性
     */
    void setupTableView(QTableView *tableView,
                       QStandardItemModel *model,
                       SimplePaginationModel *paginationModel,
                       PaginationController *paginationController,
                       UIStateManager *uiStateManager);

    /**
     * @brief 设置模型表头
     */
    void setupModelHeaders(QStandardItemModel *model);
};

#endif // MAINWINDOWUIBUILDER_H

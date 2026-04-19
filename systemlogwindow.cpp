#include "systemlogwindow.h"
#include "databasemanager.h"
#include "configmanager.h"
#include "permissionmanager.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>
#include <QDateTime>
#include <QTableWidgetItem>
#include <QColor>
#include <QInputDialog>
#include <QLineEdit>

SystemLogWindow::SystemLogWindow(QWidget *parent)
    : QDialog(parent)
    , m_hasPermission(false)
{
    setWindowTitle("系统日志");
    setMinimumSize(900, 600);
    resize(1000, 700);

    setupUI();
    updateUIForPermission();
}

SystemLogWindow::~SystemLogWindow()
{
}

void SystemLogWindow::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 日志表格
    m_logTable = new QTableWidget(this);
    m_logTable->setColumnCount(5);
    m_logTable->setHorizontalHeaderLabels({
        "时间", "操作类型", "操作内容", "项目ID", "结果"
    });
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSortingEnabled(true);
    m_logTable->sortItems(0, Qt::DescendingOrder); // 默认按时间降序

    // 设置列宽
    m_logTable->setColumnWidth(0, 180);  // 时间
    m_logTable->setColumnWidth(1, 100);  // 操作类型
    m_logTable->setColumnWidth(2, 300);  // 操作内容
    m_logTable->setColumnWidth(3, 120);  // 项目ID

    mainLayout->addWidget(m_logTable);

    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_deleteButton = new QPushButton("删除日志", this);
    m_refreshButton = new QPushButton("刷新", this);
    m_clearButton = new QPushButton("清空日志", this);
    m_closeButton = new QPushButton("关闭", this);

    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_clearButton);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);

    // 连接信号槽
    connect(m_deleteButton, &QPushButton::clicked, this, &SystemLogWindow::onDeleteLog);
    connect(m_refreshButton, &QPushButton::clicked, this, &SystemLogWindow::onRefresh);
    connect(m_clearButton, &QPushButton::clicked, this, &SystemLogWindow::onClearLogs);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool SystemLogWindow::checkPermissionAndShow()
{
    PermissionManager& permManager = PermissionManager::getInstance();
    
    // 检查是否有查看权限
    if (!permManager.canViewSystemLog()) {
        // 请求管理员密码
        if (!requestAdminPassword()) {
            QMessageBox::warning(this, "权限不足", "您没有权限查看系统日志！\n\n只有管理员可以查看系统日志。");
            return false;
        }
        // 密码验证成功，临时提升为管理员
        permManager.setUserRole(PermissionManager::Admin);
        m_hasPermission = true;
    } else {
        m_hasPermission = true;
    }
    
    updateUIForPermission();
    loadLogsFromDatabase();
    return true;
}

bool SystemLogWindow::requestAdminPassword()
{
    bool ok;
    QString password = QInputDialog::getText(this, "管理员验证", 
                                           "请输入管理员密码：",
                                           QLineEdit::Password, 
                                           QString(), &ok);
    
    if (!ok) {
        return false;
    }
    
    PermissionManager& permManager = PermissionManager::getInstance();
    if (permManager.verifyAdminPassword(password)) {
        return true;
    } else {
        QMessageBox::warning(this, "验证失败", "密码错误！");
        return false;
    }
}

void SystemLogWindow::updateUIForPermission()
{
    PermissionManager& permManager = PermissionManager::getInstance();
    
    // 根据权限显示/隐藏删除日志和清空日志按钮（只有管理员可见）
    bool canDelete = permManager.canDeleteSystemLog();
    m_deleteButton->setVisible(canDelete);
    m_clearButton->setVisible(canDelete);
    
    // 如果没有权限，禁用表格
    m_logTable->setEnabled(m_hasPermission);
    m_refreshButton->setEnabled(m_hasPermission);
}

void SystemLogWindow::loadLogsFromDatabase()
{
    // 检查权限
    if (!m_hasPermission) {
        qDebug() << "没有权限，无法加载日志";
        return;
    }
    
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    
    // 确保数据库已初始化
    if (!dbManager.isConnected()) {
        if (!dbManager.initializeDatabase()) {
            qDebug() << "数据库初始化失败，无法加载日志";
            return;
        }
    }
    
    QSqlDatabase *db = dbManager.getDatabase();
    
    if (!db || !db->isOpen()) {
        qDebug() << "数据库未连接，无法加载日志";
        return;
    }

    QSqlQuery query(*db);
    query.prepare("SELECT id, timestamp, operation_type, operation_content, project_id, result "
                  "FROM system_logs ORDER BY timestamp DESC LIMIT 1000");
    query.setForwardOnly(true);

    if (!query.exec()) {
        qDebug() << "查询系统日志失败:" << query.lastError().text();
        return;
    }

    m_logTable->setRowCount(0);
    int row = 0;

    while (query.next()) {
        m_logTable->insertRow(row);

        // 获取日志ID（用于删除操作）
        int logId = query.value(0).toInt();

        // 时间 - 直接使用数据库中的时间字符串（已经是本地时间）
        QString timeStr = query.value(1).toString();
        // 如果时间字符串格式正确，直接使用；否则尝试解析
        QDateTime timestamp;
        if (!timeStr.isEmpty()) {
            // 尝试多种格式解析
            timestamp = QDateTime::fromString(timeStr, "yyyy-MM-dd hh:mm:ss");
            if (!timestamp.isValid()) {
                timestamp = QDateTime::fromString(timeStr, Qt::ISODate);
            }
            // 如果仍然无效，使用当前时间
            if (!timestamp.isValid()) {
                timestamp = QDateTime::currentDateTime();
            }
        } else {
            timestamp = QDateTime::currentDateTime();
        }
        QTableWidgetItem *timeItem = new QTableWidgetItem(
            timestamp.toString("yyyy-MM-dd hh:mm:ss"));
        timeItem->setData(Qt::UserRole, logId);  // 存储日志ID到第一列
        m_logTable->setItem(row, 0, timeItem);

        // 操作类型
        m_logTable->setItem(row, 1, new QTableWidgetItem(query.value(2).toString()));

        // 操作内容
        m_logTable->setItem(row, 2, new QTableWidgetItem(query.value(3).toString()));

        // 项目ID
        m_logTable->setItem(row, 3, new QTableWidgetItem(query.value(4).toString()));

        // 结果
        QString result = query.value(5).toString();
        QTableWidgetItem *resultItem = new QTableWidgetItem(result);
        if (result == "成功") {
            resultItem->setForeground(Qt::darkGreen);
        } else if (result == "失败") {
            resultItem->setForeground(Qt::red);
        }
        m_logTable->setItem(row, 4, resultItem);

        row++;
    }

    qDebug() << "加载了" << row << "条日志记录";
}

void SystemLogWindow::onRefresh()
{
    // 检查权限
    PermissionManager& permManager = PermissionManager::getInstance();
    if (!permManager.canViewSystemLog()) {
        // 如果没有权限，重新请求密码
        if (!requestAdminPassword()) {
            QMessageBox::warning(this, "权限不足", "您没有权限查看系统日志！");
            return;
        }
        permManager.setUserRole(PermissionManager::Admin);
        m_hasPermission = true;
        updateUIForPermission();
    }
    
    loadLogsFromDatabase();
}

void SystemLogWindow::onClearLogs()
{
    PermissionManager& permManager = PermissionManager::getInstance();
    
    // 再次检查删除权限
    if (!permManager.canDeleteSystemLog()) {
        QMessageBox::warning(this, "权限不足", "您没有权限删除系统日志！\n\n只有管理员可以删除日志记录。");
        return;
    }
    
    // 再次确认管理员身份
    if (!requestAdminPassword()) {
        QMessageBox::warning(this, "验证失败", "需要管理员权限才能删除日志！");
        return;
    }
    
    int ret = QMessageBox::question(this, "确认", "确定要清空所有日志记录吗？此操作不可撤销！",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        
        // 确保数据库已初始化
        if (!dbManager.isConnected()) {
            if (!dbManager.initializeDatabase()) {
                QMessageBox::warning(this, "错误", "数据库初始化失败，无法清空日志");
                return;
            }
        }
        
        QSqlDatabase *db = dbManager.getDatabase();
        
        if (!db || !db->isOpen()) {
            QMessageBox::warning(this, "错误", "数据库未连接，无法清空日志");
            return;
        }

        QSqlQuery query(*db);
        if (query.exec("DELETE FROM system_logs")) {
            // 记录清空日志的操作
            dbManager.logOperation("清空日志", "管理员清空了所有系统日志", QString(), "成功");
            
            QMessageBox::information(this, "成功", "日志已清空");
            loadLogsFromDatabase();
        } else {
            QMessageBox::critical(this, "错误", "清空日志失败: " + query.lastError().text());
        }
    }
}

void SystemLogWindow::onDeleteLog()
{
    PermissionManager& permManager = PermissionManager::getInstance();
    
    // 检查删除权限
    if (!permManager.canDeleteSystemLog()) {
        QMessageBox::warning(this, "权限不足", "您没有权限删除系统日志！\n\n只有管理员可以删除日志记录。");
        return;
    }
    
    // 获取选中的行
    QList<QTableWidgetItem*> selectedItems = m_logTable->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的日志记录！");
        return;
    }
    
    // 获取选中行的第一列（包含日志ID）
    int currentRow = m_logTable->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "提示", "请先选择要删除的日志记录！");
        return;
    }
    
    QTableWidgetItem *firstItem = m_logTable->item(currentRow, 0);
    if (!firstItem) {
        QMessageBox::warning(this, "错误", "无法获取日志记录信息！");
        return;
    }
    
    // 从第一列获取日志ID
    int logId = firstItem->data(Qt::UserRole).toInt();
    if (logId <= 0) {
        QMessageBox::warning(this, "错误", "无法获取日志记录ID！");
        return;
    }
    
    // 获取日志信息用于确认对话框
    QString operationType = m_logTable->item(currentRow, 1) ? 
                           m_logTable->item(currentRow, 1)->text() : "";
    QString operationContent = m_logTable->item(currentRow, 2) ? 
                              m_logTable->item(currentRow, 2)->text() : "";
    QString timestamp = firstItem->text();
    
    // 要求管理员输入密码验证（确保安全性）
    if (!requestAdminPassword()) {
        QMessageBox::warning(this, "验证失败", "需要管理员密码才能删除日志！");
        return;
    }
    
    // 确认删除
    int ret = QMessageBox::question(this, "确认删除", 
        QString("确定要删除以下日志记录吗？\n\n时间: %1\n操作类型: %2\n操作内容: %3\n\n此操作不可撤销！")
            .arg(timestamp, operationType, operationContent),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret != QMessageBox::Yes) {
        return;
    }
    
    // 执行删除操作
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    
    // 确保数据库已初始化
    if (!dbManager.isConnected()) {
        if (!dbManager.initializeDatabase()) {
            QMessageBox::warning(this, "错误", "数据库初始化失败，无法删除日志");
            return;
        }
    }
    
    QSqlDatabase *db = dbManager.getDatabase();
    
    if (!db || !db->isOpen()) {
        QMessageBox::warning(this, "错误", "数据库未连接，无法删除日志");
        return;
    }
    
    QSqlQuery query(*db);
    query.prepare("DELETE FROM system_logs WHERE id = ?");
    query.bindValue(0, logId);
    
    if (query.exec()) {
        // 记录删除日志的操作
        dbManager.logOperation("删除日志", 
            QString("管理员删除了日志记录: %1 - %2").arg(operationType, operationContent),
            QString(), "成功");
        
        QMessageBox::information(this, "成功", "日志记录已删除");
        // 刷新日志列表
        loadLogsFromDatabase();
    } else {
        QMessageBox::critical(this, "错误", "删除日志失败: " + query.lastError().text());
    }
}

void SystemLogWindow::refreshLogs()
{
    loadLogsFromDatabase();
}

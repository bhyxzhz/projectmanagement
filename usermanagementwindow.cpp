#include "usermanagementwindow.h"
#include "usereditdialog.h"
#include "databasemanager.h"
#include "permissionmanager.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>
#include <QTableWidgetItem>
#include <QCryptographicHash>
#include <QColor>
#include <QDateTime>

UserManagementWindow::UserManagementWindow(QWidget *parent)
    : QDialog(parent)
    , m_hasPermission(false)
{
    setWindowTitle("用户管理");
    setMinimumSize(600, 400);
    resize(700, 500);

    setupUI();
    updateUIForPermission();
}

UserManagementWindow::~UserManagementWindow()
{
}

void UserManagementWindow::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 用户表格
    m_userTable = new QTableWidget(this);
    m_userTable->setColumnCount(5);
    m_userTable->setHorizontalHeaderLabels({
        "ID", "用户名", "角色", "状态", "创建时间"
    });
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->setAlternatingRowColors(true);
    m_userTable->setSortingEnabled(true);

    // 设置列宽
    m_userTable->setColumnWidth(0, 60);   // ID
    m_userTable->setColumnWidth(1, 150);  // 用户名
    m_userTable->setColumnWidth(2, 100);  // 角色
    m_userTable->setColumnWidth(3, 80);   // 状态

    mainLayout->addWidget(m_userTable);

    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_addButton = new QPushButton("添加用户", this);
    m_editButton = new QPushButton("编辑用户", this);
    m_deleteButton = new QPushButton("删除用户", this);
    m_refreshButton = new QPushButton("刷新", this);
    m_closeButton = new QPushButton("关闭", this);

    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);

    // 连接信号槽
    connect(m_addButton, &QPushButton::clicked, this, &UserManagementWindow::onAddUser);
    connect(m_editButton, &QPushButton::clicked, this, &UserManagementWindow::onEditUser);
    connect(m_deleteButton, &QPushButton::clicked, this, &UserManagementWindow::onDeleteUser);
    connect(m_refreshButton, &QPushButton::clicked, this, &UserManagementWindow::onRefresh);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

bool UserManagementWindow::requestAdminPassword()
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

void UserManagementWindow::updateUIForPermission()
{
    PermissionManager& permManager = PermissionManager::getInstance();
    
    // 只有管理员可以操作
    bool canManage = permManager.isAdmin();
    m_addButton->setEnabled(canManage);
    m_editButton->setEnabled(canManage);
    m_deleteButton->setEnabled(canManage);
    m_userTable->setEnabled(canManage);
    m_refreshButton->setEnabled(true);  // 刷新按钮始终可用
    
    m_hasPermission = canManage;
}

void UserManagementWindow::loadUsersFromDatabase()
{
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    
    // 确保数据库已初始化
    if (!dbManager.isConnected()) {
        if (!dbManager.initializeDatabase()) {
            qDebug() << "数据库初始化失败，无法加载用户";
            return;
        }
    }
    
    QSqlDatabase *db = dbManager.getDatabase();
    
    if (!db || !db->isOpen()) {
        qDebug() << "数据库未连接，无法加载用户";
        return;
    }

    QSqlQuery query(*db);
    query.prepare("SELECT id, username, role, enabled, created_at FROM users ORDER BY id");
    query.setForwardOnly(true);

    if (!query.exec()) {
        qDebug() << "查询用户失败:" << query.lastError().text();
        return;
    }

    m_userTable->setRowCount(0);
    int row = 0;

    while (query.next()) {
        m_userTable->insertRow(row);

        // ID
        int userId = query.value(0).toInt();
        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(userId));
        idItem->setData(Qt::UserRole, userId);  // 存储用户ID
        m_userTable->setItem(row, 0, idItem);

        // 用户名
        m_userTable->setItem(row, 1, new QTableWidgetItem(query.value(1).toString()));

        // 角色
        int role = query.value(2).toInt();
        QString roleText = (role == 0) ? "管理员" : "普通用户";
        QTableWidgetItem *roleItem = new QTableWidgetItem(roleText);
        roleItem->setData(Qt::UserRole, role);  // 存储角色值
        m_userTable->setItem(row, 2, roleItem);

        // 状态（启用/禁用）
        bool enabled = query.value(3).toInt() != 0;
        QString statusText = enabled ? "启用" : "禁用";
        QTableWidgetItem *statusItem = new QTableWidgetItem(statusText);
        statusItem->setData(Qt::UserRole, enabled ? 1 : 0);  // 存储启用状态
        if (!enabled) {
            statusItem->setForeground(QColor(Qt::red));  // 禁用状态显示为红色
        } else {
            statusItem->setForeground(QColor(Qt::darkGreen));  // 启用状态显示为绿色
        }
        m_userTable->setItem(row, 3, statusItem);

        // 创建时间 - 确保正确显示本地时间
        QString createdAt = query.value(4).toString();
        // 如果时间字符串格式正确，直接使用；否则尝试解析并格式化
        if (!createdAt.isEmpty()) {
            QDateTime createdDateTime = QDateTime::fromString(createdAt, "yyyy-MM-dd hh:mm:ss");
            if (!createdDateTime.isValid()) {
                createdDateTime = QDateTime::fromString(createdAt, Qt::ISODate);
            }
            if (createdDateTime.isValid()) {
                createdAt = createdDateTime.toString("yyyy-MM-dd hh:mm:ss");
            }
        }
        m_userTable->setItem(row, 4, new QTableWidgetItem(createdAt));

        row++;
    }

    qDebug() << "加载了" << row << "个用户";
}

void UserManagementWindow::onAddUser()
{
    if (!m_hasPermission) {
        QMessageBox::warning(this, "权限不足", "您没有权限添加用户！\n\n只有管理员可以管理用户。");
        return;
    }

    // 显示添加用户对话框
    UserEditDialog dialog(this, false);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString username = dialog.getUsername();
    QString password = dialog.getPassword();
    int role = dialog.getRole();
    bool enabled = dialog.isEnabled();

    // 检查用户名是否已存在
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    QSqlDatabase *db = dbManager.getDatabase();
    if (!db || !db->isOpen()) {
        QMessageBox::warning(this, "错误", "数据库未连接");
        return;
    }

    QSqlQuery checkQuery(*db);
    checkQuery.prepare("SELECT id FROM users WHERE username = ?");
    checkQuery.bindValue(0, username);
    if (checkQuery.exec() && checkQuery.next()) {
        QMessageBox::warning(this, "错误", "用户名已存在！");
        return;
    }

    // 计算密码哈希
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(password.toUtf8());
    QString passwordHash = hash.result().toHex();

    // 插入用户（使用本地时间）
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery insertQuery(*db);
    insertQuery.prepare("INSERT INTO users (username, password_hash, role, enabled, created_at) VALUES (?, ?, ?, ?, ?)");
    insertQuery.bindValue(0, username);
    insertQuery.bindValue(1, passwordHash);
    insertQuery.bindValue(2, role);
    insertQuery.bindValue(3, enabled ? 1 : 0);
    insertQuery.bindValue(4, currentTime);

    if (insertQuery.exec()) {
        // 记录操作日志
        QString roleText = (role == 0) ? "管理员" : "普通用户";
        QString statusText = enabled ? "启用" : "禁用";
        dbManager.logOperation("用户管理", 
            QString("管理员添加用户: %1 (%2, %3)").arg(username, roleText, statusText),
            QString(), "成功");
        
        QMessageBox::information(this, "成功", QString("用户 %1 添加成功！").arg(username));
        loadUsersFromDatabase();
    } else {
        QMessageBox::critical(this, "错误", "添加用户失败: " + insertQuery.lastError().text());
    }
}

void UserManagementWindow::onEditUser()
{
    if (!m_hasPermission) {
        QMessageBox::warning(this, "权限不足", "您没有权限编辑用户！\n\n只有管理员可以管理用户。");
        return;
    }

    // 获取选中的行
    int currentRow = m_userTable->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "提示", "请先选择要编辑的用户！");
        return;
    }

    // 获取用户信息
    QTableWidgetItem *idItem = m_userTable->item(currentRow, 0);
    QTableWidgetItem *usernameItem = m_userTable->item(currentRow, 1);
    QTableWidgetItem *roleItem = m_userTable->item(currentRow, 2);
    QTableWidgetItem *statusItem = m_userTable->item(currentRow, 3);

    if (!idItem || !usernameItem || !roleItem || !statusItem) {
        QMessageBox::warning(this, "错误", "无法获取用户信息！");
        return;
    }

    int userId = idItem->data(Qt::UserRole).toInt();
    QString username = usernameItem->text();
    int oldRole = roleItem->data(Qt::UserRole).toInt();
    bool oldEnabled = statusItem->data(Qt::UserRole).toInt() != 0;

    // 显示编辑用户对话框
    UserEditDialog dialog(this, true, userId, username, oldRole, oldEnabled);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int newRole = dialog.getRole();
    bool newEnabled = dialog.isEnabled();
    bool passwordChanged = dialog.isPasswordChanged();

    DatabaseManager& dbManager = DatabaseManager::getInstance();
    QSqlDatabase *db = dbManager.getDatabase();
    if (!db || !db->isOpen()) {
        QMessageBox::warning(this, "错误", "数据库未连接");
        return;
    }

    // 更新用户
    QSqlQuery updateQuery(*db);
    if (passwordChanged) {
        // 更新密码、角色和启用状态
        QString newPassword = dialog.getPassword();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(newPassword.toUtf8());
        QString passwordHash = hash.result().toHex();
        
        updateQuery.prepare("UPDATE users SET password_hash = ?, role = ?, enabled = ? WHERE id = ?");
        updateQuery.bindValue(0, passwordHash);
        updateQuery.bindValue(1, newRole);
        updateQuery.bindValue(2, newEnabled ? 1 : 0);
        updateQuery.bindValue(3, userId);
    } else {
        // 只更新角色和启用状态
        updateQuery.prepare("UPDATE users SET role = ?, enabled = ? WHERE id = ?");
        updateQuery.bindValue(0, newRole);
        updateQuery.bindValue(1, newEnabled ? 1 : 0);
        updateQuery.bindValue(2, userId);
    }

    if (updateQuery.exec()) {
        // 记录操作日志
        QString oldRoleText = (oldRole == 0) ? "管理员" : "普通用户";
        QString newRoleText = (newRole == 0) ? "管理员" : "普通用户";
        QString oldStatusText = oldEnabled ? "启用" : "禁用";
        QString newStatusText = newEnabled ? "启用" : "禁用";
        QString logContent;
        if (passwordChanged) {
            logContent = QString("管理员编辑用户: %1 (角色: %2 -> %3, 状态: %4 -> %5, 密码已修改)")
                .arg(username, oldRoleText, newRoleText, oldStatusText, newStatusText);
        } else {
            logContent = QString("管理员编辑用户: %1 (角色: %2 -> %3, 状态: %4 -> %5)")
                .arg(username, oldRoleText, newRoleText, oldStatusText, newStatusText);
        }
        dbManager.logOperation("用户管理", logContent, QString(), "成功");
        
        QMessageBox::information(this, "成功", QString("用户 %1 编辑成功！").arg(username));
        loadUsersFromDatabase();
    } else {
        QMessageBox::critical(this, "错误", "编辑用户失败: " + updateQuery.lastError().text());
    }
}

void UserManagementWindow::onDeleteUser()
{
    if (!m_hasPermission) {
        QMessageBox::warning(this, "权限不足", "您没有权限删除用户！\n\n只有管理员可以管理用户。");
        return;
    }

    // 获取选中的行
    int currentRow = m_userTable->currentRow();
    if (currentRow < 0) {
        QMessageBox::information(this, "提示", "请先选择要删除的用户！");
        return;
    }

    // 获取用户信息
    QTableWidgetItem *idItem = m_userTable->item(currentRow, 0);
    QTableWidgetItem *usernameItem = m_userTable->item(currentRow, 1);
    QTableWidgetItem *roleItem = m_userTable->item(currentRow, 2);

    if (!idItem || !usernameItem || !roleItem) {
        QMessageBox::warning(this, "错误", "无法获取用户信息！");
        return;
    }

    int userId = idItem->data(Qt::UserRole).toInt();
    QString username = usernameItem->text();
    QString roleText = roleItem->text();

    // 要求管理员输入密码验证
    if (!requestAdminPassword()) {
        QMessageBox::warning(this, "验证失败", "需要管理员密码才能删除用户！");
        return;
    }

    // 确认删除
    int ret = QMessageBox::question(this, "确认删除", 
        QString("确定要删除用户 %1 (%2) 吗？\n\n此操作不可撤销！").arg(username, roleText),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret != QMessageBox::Yes) {
        return;
    }

    DatabaseManager& dbManager = DatabaseManager::getInstance();
    QSqlDatabase *db = dbManager.getDatabase();
    if (!db || !db->isOpen()) {
        QMessageBox::warning(this, "错误", "数据库未连接");
        return;
    }

    // 删除用户
    QSqlQuery deleteQuery(*db);
    deleteQuery.prepare("DELETE FROM users WHERE id = ?");
    deleteQuery.bindValue(0, userId);

    if (deleteQuery.exec()) {
        // 记录操作日志
        dbManager.logOperation("用户管理", 
            QString("管理员删除用户: %1 (%2)").arg(username, roleText),
            QString(), "成功");
        
        QMessageBox::information(this, "成功", QString("用户 %1 删除成功！").arg(username));
        loadUsersFromDatabase();
    } else {
        QMessageBox::critical(this, "错误", "删除用户失败: " + deleteQuery.lastError().text());
    }
}

void UserManagementWindow::onRefresh()
{
    loadUsersFromDatabase();
}

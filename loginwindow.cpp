#include "loginwindow.h"
#include "databasemanager.h"
#include "permissionmanager.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QCryptographicHash>
#include <QIcon>
#include <QPixmap>
#include <QDebug>
#include <QDateTime>

LoginWindow::LoginWindow(QWidget *parent)
    : QDialog(parent)
    , m_loginSuccess(false)
    , m_userRole(1)  // 默认普通用户
{
    setWindowTitle("系统登录");
    setModal(true);
    setFixedSize(350, 280);  // 增加高度以容纳图标
    
    // 设置窗口图标
    setWindowIcon(QIcon(":/new/prefix1/images/logol.ico"));
    
    setupUI();
}

LoginWindow::~LoginWindow()
{
}

void LoginWindow::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 图标（标题上方）
    QLabel *iconLabel = new QLabel(this);
    QPixmap iconPixmap(":/new/prefix1/images/logol.ico");
    if (!iconPixmap.isNull()) {
        // 设置图标大小（48x48像素）
        iconPixmap = iconPixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        iconLabel->setPixmap(iconPixmap);
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(iconLabel);

    // 标题
    QLabel *titleLabel = new QLabel("项目管理系统登录", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // 用户名输入
    QHBoxLayout *usernameLayout = new QHBoxLayout();
    QLabel *usernameLabel = new QLabel("用户名:", this);
    usernameLabel->setMinimumWidth(80);
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("请输入用户名");
    usernameLayout->addWidget(usernameLabel);
    usernameLayout->addWidget(m_usernameEdit);
    mainLayout->addLayout(usernameLayout);

    // 密码输入
    QHBoxLayout *passwordLayout = new QHBoxLayout();
    QLabel *passwordLabel = new QLabel("密码:", this);
    passwordLabel->setMinimumWidth(80);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText("请输入密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    passwordLayout->addWidget(passwordLabel);
    passwordLayout->addWidget(m_passwordEdit);
    mainLayout->addLayout(passwordLayout);

    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_loginButton = new QPushButton("登录", this);
    m_loginButton->setMinimumWidth(80);
    m_cancelButton = new QPushButton("取消", this);
    m_cancelButton->setMinimumWidth(80);
    buttonLayout->addWidget(m_loginButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    // 提示信息
    QLabel *hintLabel = new QLabel("默认管理员: admin / admin123\n默认普通用户: user / user123", this);
    hintLabel->setAlignment(Qt::AlignCenter);
    QFont hintFont = hintLabel->font();
    hintFont.setPointSize(9);
    hintLabel->setFont(hintFont);
    hintLabel->setStyleSheet("color: gray;");
    mainLayout->addWidget(hintLabel);

    setLayout(mainLayout);

    // 连接信号槽
    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::onLogin);
    connect(m_cancelButton, &QPushButton::clicked, this, &LoginWindow::onCancel);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::onLogin);

    // 设置焦点
    m_usernameEdit->setFocus();
}

void LoginWindow::onLogin()
{
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    if (username.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入用户名！");
        m_usernameEdit->setFocus();
        return;
    }

    if (password.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入密码！");
        m_passwordEdit->setFocus();
        return;
    }

    if (verifyUser(username, password)) {
        m_loginSuccess = true;
        m_username = username;
        
        // 记录登录成功日志
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        QString roleText = (m_userRole == 0) ? "管理员" : "普通用户";
        dbManager.logOperation("用户登录", 
            QString("用户 %1 (%2) 登录成功").arg(username, roleText),
            QString(), "成功");
        
        accept();
    } else {
        // 记录登录失败日志
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("用户登录", 
            QString("用户 %1 登录失败：用户名或密码错误").arg(username),
            QString(), "失败");
        
        QMessageBox::warning(this, "登录失败", "用户名或密码错误！");
        m_passwordEdit->clear();
        m_passwordEdit->setFocus();
    }
}

void LoginWindow::onCancel()
{
    // 记录取消登录日志（如果用户输入了用户名）
    QString username = m_usernameEdit->text().trimmed();
    if (!username.isEmpty()) {
        DatabaseManager& dbManager = DatabaseManager::getInstance();
        dbManager.logOperation("用户登录", 
            QString("用户 %1 取消登录").arg(username),
            QString(), "取消");
    }
    
    reject();
}

bool LoginWindow::verifyUser(const QString& username, const QString& password)
{
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    
    // 确保数据库已初始化
    if (!dbManager.isConnected()) {
        if (!dbManager.initializeDatabase()) {
            qDebug() << "数据库初始化失败，无法验证用户";
            return false;
        }
    }
    
    QSqlDatabase *db = dbManager.getDatabase();
    if (!db || !db->isOpen()) {
        qDebug() << "数据库未连接，无法验证用户";
        return false;
    }

    // 计算密码哈希值
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(password.toUtf8());
    QString passwordHash = hash.result().toHex();

    // 查询用户（包括启用状态）
    QSqlQuery query(*db);
    query.prepare("SELECT id, username, password_hash, role, enabled FROM users WHERE username = ?");
    query.bindValue(0, username);
    query.setForwardOnly(true);

    if (!query.exec()) {
        qDebug() << "查询用户失败:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        // 检查用户是否被禁用
        bool enabled = query.value(4).toInt() != 0;
        if (!enabled) {
            qDebug() << "用户已被禁用:" << username;
            // 记录登录失败日志
            dbManager.logOperation("用户登录", 
                QString("用户 %1 登录失败：账户已被禁用").arg(username),
                QString(), "失败");
            return false;
        }
        
        QString storedHash = query.value(2).toString();
        if (passwordHash == storedHash) {
            m_userRole = query.value(3).toInt();  // 0=管理员, 1=普通用户
            qDebug() << "用户验证成功:" << username << "角色:" << (m_userRole == 0 ? "管理员" : "普通用户");
            
            // 更新用户最后登录时间（使用本地时间）
            int userId = query.value(0).toInt();
            QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            QSqlQuery updateQuery(*db);
            updateQuery.prepare("UPDATE users SET last_login = ? WHERE id = ?");
            updateQuery.bindValue(0, currentTime);
            updateQuery.bindValue(1, userId);
            if (!updateQuery.exec()) {
                qDebug() << "更新用户最后登录时间失败:" << updateQuery.lastError().text();
            }
            
            return true;
        }
    }

    qDebug() << "用户验证失败:" << username;
    return false;
}

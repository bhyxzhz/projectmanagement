#include "permissionmanager.h"
#include "configmanager.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QSettings>

PermissionManager* PermissionManager::instance = nullptr;

PermissionManager& PermissionManager::getInstance()
{
    if (!instance) {
        instance = new PermissionManager();
    }
    return *instance;
}

PermissionManager::PermissionManager(QObject *parent)
    : QObject(parent)
    , m_userRole(User)
    , m_currentUsername("")
{
    loadPermissionSettings();
}

PermissionManager::~PermissionManager()
{
    savePermissionSettings();
}

void PermissionManager::loadPermissionSettings()
{
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.initialize()) {
        qDebug() << "配置管理器初始化失败，使用默认权限设置";
        m_userRole = User;
        return;
    }
    
    // 从配置读取用户角色
    int role = config.getValue("Permission/UserRole", static_cast<int>(User)).toInt();
    m_userRole = static_cast<UserRole>(role);
    
    // 从配置读取管理员密码（加密后的哈希值）
    m_adminPassword = config.getValue("Permission/AdminPassword", QString()).toString();
    
    // 如果还没有设置管理员密码，设置默认密码（admin123）
    if (m_adminPassword.isEmpty()) {
        setAdminPassword("admin123");
    }
    
    qDebug() << "权限设置加载完成，当前角色:" << (m_userRole == Admin ? "管理员" : "普通用户");
}

void PermissionManager::savePermissionSettings()
{
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.initialize()) {
        return;
    }
    
    config.setValue("Permission/UserRole", static_cast<int>(m_userRole));
    config.setValue("Permission/AdminPassword", m_adminPassword);
}

bool PermissionManager::isAdmin() const
{
    return m_userRole == Admin;
}

bool PermissionManager::canViewSystemLog() const
{
    // 只有管理员可以查看系统日志
    return isAdmin();
}

bool PermissionManager::canDeleteSystemLog() const
{
    // 只有管理员可以删除系统日志
    return isAdmin();
}

bool PermissionManager::canDeleteProject() const
{
    // 只有管理员可以删除项目，普通用户不能删除
    return isAdmin();
}

void PermissionManager::setCurrentUser(const QString& username, UserRole role)
{
    m_currentUsername = username;
    setUserRole(role);
    qDebug() << "当前用户:" << username << "角色:" << (role == Admin ? "管理员" : "普通用户");
}

void PermissionManager::setUserRole(UserRole role)
{
    if (m_userRole != role) {
        m_userRole = role;
        savePermissionSettings();
        emit userRoleChanged(m_userRole);
        qDebug() << "用户角色已更改为:" << (m_userRole == Admin ? "管理员" : "普通用户");
    }
}

bool PermissionManager::verifyAdminPassword(const QString& password)
{
    // 计算输入密码的哈希值
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(password.toUtf8());
    QString passwordHash = hash.result().toHex();
    
    // 与存储的密码哈希值比较
    return passwordHash == m_adminPassword;
}

void PermissionManager::setAdminPassword(const QString& password)
{
    // 使用SHA256加密密码
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(password.toUtf8());
    m_adminPassword = hash.result().toHex();
    
    savePermissionSettings();
    qDebug() << "管理员密码已设置";
}

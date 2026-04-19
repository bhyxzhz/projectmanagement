#ifndef PERMISSIONMANAGER_H
#define PERMISSIONMANAGER_H

#include <QObject>
#include <QString>

/**
 * @brief 权限管理器
 * 
 * 功能：
 * 1. 管理用户权限（管理员/普通用户）
 * 2. 检查操作权限
 * 3. 权限持久化存储
 */
class PermissionManager : public QObject
{
    Q_OBJECT

public:
    enum UserRole {
        Admin = 0,      // 管理员
        User = 1        // 普通用户
    };

    static PermissionManager& getInstance();
    
    // 权限检查
    bool isAdmin() const;
    bool canViewSystemLog() const;
    bool canDeleteSystemLog() const;
    bool canDeleteProject() const;  // 是否可以删除项目
    
    // 权限设置
    void setUserRole(UserRole role);
    UserRole getUserRole() const { return m_userRole; }
    
    // 用户登录
    void setCurrentUser(const QString& username, UserRole role);
    QString getCurrentUsername() const { return m_currentUsername; }
    
    // 验证管理员密码
    bool verifyAdminPassword(const QString& password);
    
    // 设置管理员密码
    void setAdminPassword(const QString& password);

signals:
    void userRoleChanged(UserRole role);

private:
    explicit PermissionManager(QObject *parent = nullptr);
    ~PermissionManager();
    
    void loadPermissionSettings();
    void savePermissionSettings();
    
    UserRole m_userRole;
    QString m_adminPassword;  // 管理员密码（加密存储）
    QString m_currentUsername;  // 当前登录用户名
    
    static PermissionManager* instance;
};

#endif // PERMISSIONMANAGER_H

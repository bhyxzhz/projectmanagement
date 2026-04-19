#ifndef USERMANAGEMENTWINDOW_H
#define USERMANAGEMENTWINDOW_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>

/**
 * @brief 用户管理窗口
 * 
 * 功能：
 * 1. 显示所有用户列表
 * 2. 支持用户的增删改查操作
 * 3. 只有管理员可以操作
 * 4. 所有操作记录到系统日志
 */
class UserManagementWindow : public QDialog
{
    Q_OBJECT

public:
    explicit UserManagementWindow(QWidget *parent = nullptr);
    ~UserManagementWindow();
    
    // 刷新用户列表（供外部调用）
    void refreshUsers() { onRefresh(); }

public slots:
    void onAddUser();
    void onEditUser();
    void onDeleteUser();
    void onRefresh();

private:
    void setupUI();
    void loadUsersFromDatabase();
    bool requestAdminPassword();
    void updateUIForPermission();

    QTableWidget *m_userTable;
    QPushButton *m_addButton;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;
    QPushButton *m_refreshButton;
    QPushButton *m_closeButton;
    bool m_hasPermission;
};

#endif // USERMANAGEMENTWINDOW_H

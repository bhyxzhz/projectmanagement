#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

/**
 * @brief 登录窗口
 * 
 * 功能：
 * 1. 用户登录验证
 * 2. 支持管理员和普通用户登录
 * 3. 密码加密验证
 */
class LoginWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();

    // 获取登录结果
    bool isLoginSuccess() const { return m_loginSuccess; }
    QString getUsername() const { return m_username; }
    int getUserRole() const { return m_userRole; }

public slots:
    void onLogin();
    void onCancel();

private:
    void setupUI();
    bool verifyUser(const QString& username, const QString& password);

    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QPushButton *m_loginButton;
    QPushButton *m_cancelButton;
    
    bool m_loginSuccess;
    QString m_username;
    int m_userRole;  // 0=管理员, 1=普通用户
};

#endif // LOGINWINDOW_H

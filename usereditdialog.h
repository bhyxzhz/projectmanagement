#ifndef USEREDITDIALOG_H
#define USEREDITDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

/**
 * @brief 用户编辑对话框
 * 
 * 功能：
 * 1. 添加新用户
 * 2. 编辑现有用户
 * 3. 设置用户名、密码、角色、启用状态
 */
class UserEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserEditDialog(QWidget *parent = nullptr, bool isEditMode = false, 
                           int userId = -1, const QString &username = QString(), 
                           int role = 1, bool enabled = true);
    ~UserEditDialog();

    // 获取输入的值
    QString getUsername() const;
    QString getPassword() const;
    int getRole() const;
    bool isEnabled() const;
    bool isPasswordChanged() const;

private slots:
    void onOk();
    void onCancel();

private:
    void setupUI();
    bool validateInput();

    bool m_isEditMode;
    int m_userId;
    
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_passwordConfirmEdit;
    QComboBox *m_roleComboBox;
    QCheckBox *m_enabledCheckBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    
    bool m_passwordChanged;
};

#endif // USEREDITDIALOG_H

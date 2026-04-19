#include "usereditdialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

UserEditDialog::UserEditDialog(QWidget *parent, bool isEditMode, int userId, 
                               const QString &username, int role, bool enabled)
    : QDialog(parent)
    , m_isEditMode(isEditMode)
    , m_userId(userId)
    , m_passwordChanged(false)
{
    setWindowTitle(isEditMode ? "编辑用户" : "添加用户");
    setModal(true);
    setFixedSize(400, 280);
    
    setupUI();
    
    // 如果是编辑模式，填充现有数据
    if (isEditMode) {
        m_usernameEdit->setText(username);
        m_usernameEdit->setEnabled(false);  // 编辑模式下不允许修改用户名
        m_roleComboBox->setCurrentIndex(role == 0 ? 0 : 1);
        m_enabledCheckBox->setChecked(enabled);
        m_passwordEdit->setPlaceholderText("留空则不修改密码");
        m_passwordConfirmEdit->setPlaceholderText("留空则不修改密码");
    }
}

UserEditDialog::~UserEditDialog()
{
}

void UserEditDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 表单区域
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    // 用户名
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("请输入用户名");
    formLayout->addRow("用户名:", m_usernameEdit);

    // 密码
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText("请输入密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("密码:", m_passwordEdit);

    // 确认密码
    m_passwordConfirmEdit = new QLineEdit(this);
    m_passwordConfirmEdit->setPlaceholderText("请再次输入密码");
    m_passwordConfirmEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("确认密码:", m_passwordConfirmEdit);

    // 角色
    m_roleComboBox = new QComboBox(this);
    m_roleComboBox->addItem("管理员", 0);
    m_roleComboBox->addItem("普通用户", 1);
    formLayout->addRow("角色:", m_roleComboBox);

    // 启用状态
    m_enabledCheckBox = new QCheckBox("启用", this);
    m_enabledCheckBox->setChecked(true);
    formLayout->addRow("状态:", m_enabledCheckBox);

    mainLayout->addLayout(formLayout);
    mainLayout->addStretch();

    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_okButton = new QPushButton("确定", this);
    m_okButton->setMinimumWidth(80);
    m_cancelButton = new QPushButton("取消", this);
    m_cancelButton->setMinimumWidth(80);

    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);

    // 连接信号槽
    connect(m_okButton, &QPushButton::clicked, this, &UserEditDialog::onOk);
    connect(m_cancelButton, &QPushButton::clicked, this, &UserEditDialog::onCancel);
    
    // 设置焦点
    if (m_isEditMode) {
        m_passwordEdit->setFocus();
    } else {
        m_usernameEdit->setFocus();
    }
}

bool UserEditDialog::validateInput()
{
    // 验证用户名
    QString username = m_usernameEdit->text().trimmed();
    if (username.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入用户名！");
        m_usernameEdit->setFocus();
        return false;
    }

    // 验证密码
    QString password = m_passwordEdit->text();
    QString passwordConfirm = m_passwordConfirmEdit->text();
    
    if (m_isEditMode) {
        // 编辑模式：如果输入了密码，则必须验证
        if (!password.isEmpty()) {
            if (password != passwordConfirm) {
                QMessageBox::warning(this, "输入错误", "两次输入的密码不一致！");
                m_passwordConfirmEdit->setFocus();
                return false;
            }
            if (password.length() < 3) {
                QMessageBox::warning(this, "输入错误", "密码长度至少为3个字符！");
                m_passwordEdit->setFocus();
                return false;
            }
            m_passwordChanged = true;
        } else {
            m_passwordChanged = false;
        }
    } else {
        // 添加模式：密码必填
        if (password.isEmpty()) {
            QMessageBox::warning(this, "输入错误", "请输入密码！");
            m_passwordEdit->setFocus();
            return false;
        }
        if (password != passwordConfirm) {
            QMessageBox::warning(this, "输入错误", "两次输入的密码不一致！");
            m_passwordConfirmEdit->setFocus();
            return false;
        }
        if (password.length() < 3) {
            QMessageBox::warning(this, "输入错误", "密码长度至少为3个字符！");
            m_passwordEdit->setFocus();
            return false;
        }
        m_passwordChanged = true;
    }

    return true;
}

void UserEditDialog::onOk()
{
    if (validateInput()) {
        accept();
    }
}

void UserEditDialog::onCancel()
{
    reject();
}

QString UserEditDialog::getUsername() const
{
    return m_usernameEdit->text().trimmed();
}

QString UserEditDialog::getPassword() const
{
    return m_passwordEdit->text();
}

int UserEditDialog::getRole() const
{
    return m_roleComboBox->currentData().toInt();
}

bool UserEditDialog::isEnabled() const
{
    return m_enabledCheckBox->isChecked();
}

bool UserEditDialog::isPasswordChanged() const
{
    return m_passwordChanged;
}

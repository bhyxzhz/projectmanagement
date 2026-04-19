#include "simpledialog.h"
#include "appconstants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QDate>

SimpleDialog::SimpleDialog(QWidget *parent)
    : QDialog(parent), isEditMode(false)
{
    setupUI();
    setWindowTitle("添加项目");
}

SimpleDialog::SimpleDialog(const QStringList& projectData, QWidget *parent)
    : QDialog(parent), isEditMode(true)
{
    setupUI();
    setProjectData(projectData);
    setWindowTitle("编辑项目");
}

void SimpleDialog::setupUI()
{
    setModal(true);
    setMinimumSize(500, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 表单布局
    QFormLayout *formLayout = new QFormLayout();

    // 项目ID
    projectIdEdit = new QLineEdit();
    projectIdEdit->setReadOnly(true);
    projectIdEdit->setPlaceholderText("自动生成");
    formLayout->addRow("项目ID:", projectIdEdit);

    // 项目名称
    projectNameEdit = new QLineEdit();
    projectNameEdit->setPlaceholderText("请输入项目名称");
    formLayout->addRow("项目名称:", projectNameEdit);

    // 项目经理
    managerEdit = new QLineEdit();
    managerEdit->setPlaceholderText("请输入项目经理姓名");
    formLayout->addRow("项目经理:", managerEdit);

    // 开始日期
    startDateEdit = new QDateEdit();
    startDateEdit->setDate(QDate::currentDate());
    startDateEdit->setCalendarPopup(true);
    formLayout->addRow("开始日期:", startDateEdit);

    // 结束日期
    endDateEdit = new QDateEdit();
    endDateEdit->setDate(QDate::currentDate().addDays(30));
    endDateEdit->setCalendarPopup(true);
    formLayout->addRow("结束日期:", endDateEdit);

    // 预算（使用常量定义范围）
    budgetSpinBox = new QDoubleSpinBox();
    budgetSpinBox->setRange(ProjectValidationConstants::MIN_BUDGET, ProjectValidationConstants::MAX_BUDGET);
    budgetSpinBox->setValue(100.0);
    budgetSpinBox->setSuffix(" 万元");
    budgetSpinBox->setDecimals(2);
    formLayout->addRow("预算:", budgetSpinBox);

    // 状态
    statusComboBox = new QComboBox();
    statusComboBox->addItems({"规划中", "进行中", "已延期", "已完成", "已取消"});
    formLayout->addRow("状态:", statusComboBox);

    // 描述
    descriptionEdit = new QTextEdit();
    descriptionEdit->setPlaceholderText("请输入项目描述...");
    descriptionEdit->setMaximumHeight(100);
    formLayout->addRow("描述:", descriptionEdit);

    mainLayout->addLayout(formLayout);

    // 错误提示标签
    errorLabel = new QLabel();
    errorLabel->setStyleSheet("color: red; font-size: 12px;");
    errorLabel->setVisible(false);
    mainLayout->addWidget(errorLabel);

    // 按钮
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    // 连接按钮信号 - 使用自定义验证函数
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SimpleDialog::validateAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SimpleDialog::reject);
}

void SimpleDialog::setProjectData(const QStringList& projectData)
{
    if (projectData.size() >= 8) {
        projectIdEdit->setText(projectData[0]);
        projectNameEdit->setText(projectData[1]);
        managerEdit->setText(projectData[2]);
        startDateEdit->setDate(QDate::fromString(projectData[3], "yyyy-MM-dd"));
        endDateEdit->setDate(QDate::fromString(projectData[4], "yyyy-MM-dd"));
        budgetSpinBox->setValue(projectData[5].toDouble());

        int statusIndex = statusComboBox->findText(projectData[6]);
        if (statusIndex >= 0) {
            statusComboBox->setCurrentIndex(statusIndex);
        }

        descriptionEdit->setPlainText(projectData[7]);
    }
}

QStringList SimpleDialog::getProjectData() const
{
    QStringList data;
    data << projectIdEdit->text();
    data << projectNameEdit->text();
    data << managerEdit->text();
    data << startDateEdit->date().toString("yyyy-MM-dd");
    data << endDateEdit->date().toString("yyyy-MM-dd");
    data << QString::number(budgetSpinBox->value(), 'f', 2);
    data << statusComboBox->currentText();
    data << descriptionEdit->toPlainText();
    return data;
}

void SimpleDialog::setEditMode(bool editMode)
{
    isEditMode = editMode;
    // 项目ID在添加和编辑模式下都应该只读（添加时自动生成，编辑时不允许修改）
    projectIdEdit->setReadOnly(true);
    setWindowTitle(editMode ? "编辑项目" : "添加项目");
}

void SimpleDialog::validateAndAccept()
{
    QString errorMessage;
    if (validateInput(errorMessage)) {
        errorLabel->setVisible(false);
        errorLabel->clear();
        accept();
    } else {
        errorLabel->setText(errorMessage);
        errorLabel->setVisible(true);
    }
}

bool SimpleDialog::validateInput(QString &errorMessage)
{
    // 验证项目名称
    if (projectNameEdit->text().trimmed().isEmpty()) {
        errorMessage = "错误：项目名称不能为空！";
        projectNameEdit->setFocus();
        return false;
    }

    if (projectNameEdit->text().trimmed().length() > ProjectValidationConstants::MAX_PROJECT_NAME_LENGTH) {
        errorMessage = QString("错误：项目名称长度不能超过%1个字符！")
                       .arg(ProjectValidationConstants::MAX_PROJECT_NAME_LENGTH);
        projectNameEdit->setFocus();
        return false;
    }

    // 验证项目经理
    if (managerEdit->text().trimmed().isEmpty()) {
        errorMessage = "错误：项目经理姓名不能为空！";
        managerEdit->setFocus();
        return false;
    }

    if (managerEdit->text().trimmed().length() > ProjectValidationConstants::MAX_MANAGER_NAME_LENGTH) {
        errorMessage = QString("错误：项目经理姓名长度不能超过%1个字符！")
                       .arg(ProjectValidationConstants::MAX_MANAGER_NAME_LENGTH);
        managerEdit->setFocus();
        return false;
    }

    // 验证日期
    QDate startDate = startDateEdit->date();
    QDate endDate = endDateEdit->date();

    if (startDate > endDate) {
        errorMessage = "错误：开始日期不能晚于结束日期！";
        startDateEdit->setFocus();
        return false;
    }

    // 验证预算（使用常量定义范围）
    double budget = budgetSpinBox->value();
    if (budget < ProjectValidationConstants::MIN_BUDGET || budget > ProjectValidationConstants::MAX_BUDGET) {
        errorMessage = QString("错误：预算必须在%1到%2万元之间！")
                       .arg(ProjectValidationConstants::MIN_BUDGET)
                       .arg(ProjectValidationConstants::MAX_BUDGET);
        budgetSpinBox->setFocus();
        return false;
    }

    // 验证描述长度（可选，但不为空时检查长度）
    QString description = descriptionEdit->toPlainText().trimmed();
    if (description.length() > ProjectValidationConstants::MAX_DESCRIPTION_LENGTH) {
        errorMessage = QString("错误：项目描述长度不能超过%1个字符！")
                       .arg(ProjectValidationConstants::MAX_DESCRIPTION_LENGTH);
        descriptionEdit->setFocus();
        return false;
    }

    return true;
}

#ifndef SIMPLEDIALOG_H
#define SIMPLEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QStringList>

class SimpleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SimpleDialog(QWidget *parent = nullptr);
    explicit SimpleDialog(const QStringList& projectData, QWidget *parent = nullptr);

    QStringList getProjectData() const;
    void setProjectData(const QStringList& projectData);

    void setEditMode(bool editMode);

private slots:
    void validateAndAccept();

private:
    void setupUI();
    bool validateInput(QString &errorMessage);

    QLineEdit *projectIdEdit;
    QLineEdit *projectNameEdit;
    QLineEdit *managerEdit;
    QDateEdit *startDateEdit;
    QDateEdit *endDateEdit;
    QDoubleSpinBox *budgetSpinBox;
    QComboBox *statusComboBox;
    QTextEdit *descriptionEdit;
    QDialogButtonBox *buttonBox;
    QLabel *errorLabel;
    bool isEditMode;
};

#endif // SIMPLEDIALOG_H

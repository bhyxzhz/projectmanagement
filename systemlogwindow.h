#ifndef SYSTEMLOGWINDOW_H
#define SYSTEMLOGWINDOW_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>
#include <QInputDialog>
#include <QLineEdit>

/**
 * @brief 系统日志窗口
 * 
 * 功能：
 * 1. 显示用户操作记录
 * 2. 从SQLite数据库读取历史日志
 * 3. 支持刷新和清空日志
 * 4. 权限控制：只有管理员可以查看和删除日志
 */
class SystemLogWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SystemLogWindow(QWidget *parent = nullptr);
    ~SystemLogWindow();

    // 刷新日志显示
    void refreshLogs();
    
    // 检查权限并显示窗口
    bool checkPermissionAndShow();

public slots:
    void onRefresh();
    void onClearLogs();
    void onDeleteLog();

private:
    void setupUI();
    void loadLogsFromDatabase();
    bool requestAdminPassword();
    void updateUIForPermission();

    QTableWidget *m_logTable;
    QPushButton *m_deleteButton;
    QPushButton *m_refreshButton;
    QPushButton *m_clearButton;
    QPushButton *m_closeButton;
    bool m_hasPermission;
};

#endif // SYSTEMLOGWINDOW_H

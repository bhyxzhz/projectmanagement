#include "mainwindow.h"
#include "loginwindow.h"
#include "databasemanager.h"
#include "permissionmanager.h"
#include <QApplication>
#include <QScreen>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QDebug>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 设置应用程序样式
    app.setStyle("Fusion");

    // 先初始化数据库（因为登录需要查询用户表）
    DatabaseManager& dbManager = DatabaseManager::getInstance();
    if (!dbManager.initializeDatabase()) {
        qDebug() << "数据库初始化失败，无法启动应用";
        return 1;
    }

    // 循环处理登录和主窗口显示，支持退出系统后重新登录
    while (true) {
        // 显示登录窗口
        LoginWindow loginWindow;
        
        // 将登录窗口显示在屏幕中心
        QScreen *loginScreen = QApplication::primaryScreen();
        if (loginScreen) {
            QRect screenGeometry = loginScreen->geometry();
            int x = (screenGeometry.width() - loginWindow.width()) / 2;
            int y = (screenGeometry.height() - loginWindow.height()) / 2;
            loginWindow.move(x, y);
        }
        
        // exec()会自动显示模态对话框，不需要手动调用show()
        // 但为了确保窗口居中显示，我们在exec()之前设置位置
        int result = loginWindow.exec();
        if (result != QDialog::Accepted || !loginWindow.isLoginSuccess()) {
            // 用户取消登录或登录失败，正常退出
            qDebug() << "用户取消登录或登录失败，退出应用程序";
            // 确保所有资源清理完成
            QCoreApplication::processEvents();
            // 正常退出，不显示异常终止
            app.quit();
            return 0;
        }

        // 设置当前用户权限
        PermissionManager& permManager = PermissionManager::getInstance();
        PermissionManager::UserRole role = static_cast<PermissionManager::UserRole>(loginWindow.getUserRole());
        permManager.setCurrentUser(loginWindow.getUsername(), role);

        // 登录成功，创建并显示主窗口
        MainWindow mainWindow;
        mainWindow.setWindowIcon(QIcon(":/new/prefix1/images/logo.ico"));
        mainWindow.setWindowTitle(QString("高性能软件项目--信息管理系统 v1.0 - [%1]")
                                  .arg(loginWindow.getUsername()));
        
        // 将窗口显示在屏幕中心
        QScreen *screen = QApplication::primaryScreen();
        if (screen) {
            QRect screenGeometry = screen->geometry();
            int x = (screenGeometry.width() - mainWindow.width()) / 2;
            int y = (screenGeometry.height() - mainWindow.height()) / 2;
            mainWindow.move(x, y);
        }

        mainWindow.show();

        // 使用标志来跟踪退出系统请求
        bool logoutRequested = false;
        
        // 连接退出系统信号
        QObject::connect(&mainWindow, &MainWindow::logoutRequested, [&logoutRequested]() {
            logoutRequested = true;
            qDebug() << "收到退出系统信号，设置标志";
        });
        
        // 运行应用程序主事件循环
        app.exec();
        
        qDebug() << "应用程序事件循环退出，退出系统标志:" << logoutRequested;

        // 检查是否是退出系统（返回登录窗口）
        if (logoutRequested) {
            qDebug() << "用户退出系统，返回登录窗口";
            // 重置退出系统标志，准备下一次循环
            logoutRequested = false;
            // 处理事件，确保主窗口完全关闭和资源清理
            QCoreApplication::processEvents();
            // 继续循环，重新显示登录窗口
            // 注意：不需要调用app.quit()，因为app.exec()已经返回了
            continue;
        } else {
            // 正常关闭应用（用户关闭主窗口）
            qDebug() << "应用程序正常退出";
            break;
        }
    }

    return 0;
}


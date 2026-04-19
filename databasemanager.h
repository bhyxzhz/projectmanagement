#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardItemModel>
#include <QStringList>
#include <QDate>
#include <QThread>
#include "idatabaseaccessor.h"

/**
 * @brief 数据库管理器 - 实现IDatabaseAccessor接口
 * 
 * 符合依赖倒置原则（DIP）：
 * - 实现IDatabaseAccessor接口
 * - 服务层依赖IDatabaseAccessor接口，而不是DatabaseManager具体类
 * - 便于单元测试和扩展（可以创建Mock实现）
 */
class DatabaseManager : public QObject, public IDatabaseAccessor
{
    Q_OBJECT

public:
    static DatabaseManager& getInstance();

    bool initializeDatabase();
    bool isConnected() const;

    // 项目数据操作
    bool addProject(const QStringList& projectData);
    bool updateProject(int row, const QStringList& projectData);
    bool updateProjectById(const QString& projectId, const QStringList& projectData);
    bool deleteProject(int row);
    bool deleteProjectById(const QString& projectId);
    bool loadProjects(QStandardItemModel* model);

    // 搜索功能
    QStandardItemModel* searchProjects(const QString& keyword, const QString& filterType = "all");
    
    // FTS全文搜索功能（高性能）
    QStandardItemModel* searchProjectsFTS(const QString& keyword, const QString& filterType = "all");
    bool createFTSTable();  // 创建FTS虚拟表
    bool syncFTSData();     // 同步FTS数据

    // 统计功能
    int getProjectCount();
    double getTotalBudget();
    QStringList getProjectStatusStats();

    // 性能优化功能
    bool createIndexes();//创建索引，加速查询
    bool optimizeDatabase();//优化数据库
    bool vacuumDatabase();//清理碎片、收缩文件
    bool analyzeDatabase();//分析表，让查询计划更优

    // 批量操作
    bool batchInsertProjects(const QList<QStringList>& projectsData, bool manageTransaction = true);
    bool batchUpdateProjects(const QList<QPair<int, QStringList>>& updatesData);
    bool batchDeleteProjects(const QList<int>& rowIds);

    // 流式导出（用于大数据量CSV导出）
    bool exportToCSVStream(const QString& filePath, std::function<void(int, int)> progressCallback = nullptr);

    QString getLastError() const { return lastError; }

    // 提供数据库访问接口
    QSqlDatabase* getDatabase() { return &db; }
    
    // 等待后台维护线程完成（数据生成前调用，避免锁定）
    void waitForMaintenanceThread(int timeoutMs = 30000);

    // 系统日志功能
    bool logOperation(const QString& operationType, const QString& operationContent, 
                     const QString& projectId = QString(), const QString& result = "成功");

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();
    QString generateProjectId();
    void migrateDatabase();

    QSqlDatabase db;
    QString lastError;
    static DatabaseManager* instance;
    QThread* m_maintenanceThread;  // 后台维护线程（索引创建）
    bool m_initialized;  // 是否已初始化
};

#endif // DATABASEMANAGER_H

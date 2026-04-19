#ifndef IDATABASEACCESSOR_H
#define IDATABASEACCESSOR_H

#include <QStandardItemModel>
#include <QStringList>
#include <QList>
#include <QPair>
#include <functional>

/**
 * @file idatabaseaccessor.h
 * @brief 数据库访问接口 - 实现依赖倒置原则（DIP）
 * 
 * 设计目的：
 * 1. 实现依赖倒置原则：高层模块（服务层）依赖抽象接口，而不是具体实现
 * 2. 提高可测试性：可以轻松创建Mock实现进行单元测试
 * 3. 提高可扩展性：可以轻松替换不同的数据库实现（SQLite、MySQL等）
 * 
 * 符合千万级产品标准化要求：
 * - 接口设计清晰，职责单一
 * - 支持依赖注入，降低耦合度
 * - 便于单元测试和集成测试
 */

/**
 * @brief 数据库访问接口
 * 
 * 定义所有数据库操作的标准接口，实现依赖倒置原则。
 * 服务层依赖此接口，而不是具体的DatabaseManager实现。
 */
class IDatabaseAccessor
{
public:
    virtual ~IDatabaseAccessor() = default;

    // 数据库连接管理
    virtual bool initializeDatabase() = 0;
    virtual bool isConnected() const = 0;
    virtual QString getLastError() const = 0;

    // 项目数据操作
    virtual bool addProject(const QStringList& projectData) = 0;
    virtual bool updateProject(int row, const QStringList& projectData) = 0;
    virtual bool updateProjectById(const QString& projectId, const QStringList& projectData) = 0;
    virtual bool deleteProject(int row) = 0;
    virtual bool deleteProjectById(const QString& projectId) = 0;
    virtual bool loadProjects(QStandardItemModel* model) = 0;

    // 搜索功能
    virtual QStandardItemModel* searchProjects(const QString& keyword, const QString& filterType = "all") = 0;
    virtual QStandardItemModel* searchProjectsFTS(const QString& keyword, const QString& filterType = "all") = 0;

    // 统计功能
    virtual int getProjectCount() = 0;
    virtual double getTotalBudget() = 0;
    virtual QStringList getProjectStatusStats() = 0;

    // 批量操作
    virtual bool batchInsertProjects(const QList<QStringList>& projectsData, bool manageTransaction = true) = 0;
    virtual bool batchUpdateProjects(const QList<QPair<int, QStringList>>& updatesData) = 0;
    virtual bool batchDeleteProjects(const QList<int>& rowIds) = 0;

    // 流式导出
    virtual bool exportToCSVStream(const QString& filePath, std::function<void(int, int)> progressCallback = nullptr) = 0;

    // 提供数据库访问接口（用于需要直接访问数据库的场景）
    virtual class QSqlDatabase* getDatabase() = 0;
};

#endif // IDATABASEACCESSOR_H

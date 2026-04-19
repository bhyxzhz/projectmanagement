#ifndef PROJECTSERVICE_H
#define PROJECTSERVICE_H

#include <QObject>
#include <QStringList>
#include <QStandardItemModel>
#include "idatabaseaccessor.h"

// 前向声明
class DatabaseManager;

/**
 * @brief 项目业务服务层
 * 
 * 职责：
 * 1. 项目CRUD操作
 * 2. 生成唯一项目ID
 * 3. 项目数据验证
 * 4. 项目状态管理
 */
/**
 * @brief 项目业务服务层
 * 
 * 符合依赖倒置原则（DIP）：
 * - 依赖IDatabaseAccessor接口，而不是DatabaseManager具体类
 * - 便于单元测试（可以注入Mock实现）
 * - 提高可扩展性（可以轻松替换不同的数据库实现）
 */
class ProjectService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数 - 使用依赖注入
     * @param dbAccessor 数据库访问接口（实现依赖倒置原则）
     * @param parent 父对象
     */
    explicit ProjectService(IDatabaseAccessor *dbAccessor, QObject *parent = nullptr);

    // 项目操作
    bool addProject(const QStringList &projectData, QString &errorMessage);
    bool updateProject(const QString &projectId, const QStringList &projectData, QString &errorMessage);
    bool deleteProject(const QString &projectId, QString &errorMessage);
    
    // 项目ID生成
    QString generateUniqueProjectId();
    
    // 数据验证
    bool validateProjectData(const QStringList &projectData, QString &errorMessage);
    
    // 获取项目信息
    QStringList getProjectData(const QString &projectId, QString &errorMessage);
    int getTotalProjectCount() const;
    double getTotalBudget() const;
    QStringList getProjectStatusStats() const;

signals:
    void projectAdded(const QString &projectId);
    void projectUpdated(const QString &projectId);
    void projectDeleted(const QString &projectId);
    void operationCompleted(const QString &operation, bool success, const QString &message);

private:
    IDatabaseAccessor *m_dbAccessor;  // 依赖接口（依赖倒置原则）
    DatabaseManager *m_dbManager;     // 兼容性：保持对DatabaseManager的直接访问（用于需要特定功能的场景）
    
    // 验证辅助方法
    bool validateProjectName(const QString &name, QString &errorMessage);
    bool validateManager(const QString &manager, QString &errorMessage);
    bool validateDates(const QString &startDate, const QString &endDate, QString &errorMessage);
    bool validateBudget(const QString &budget, QString &errorMessage);
};

#endif // PROJECTSERVICE_H

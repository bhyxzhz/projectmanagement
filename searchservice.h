#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include <QObject>
#include <QStandardItemModel>
#include <QString>
#include "idatabaseaccessor.h"

// 前向声明
class DatabaseManager;
class SimplePaginationModel;

/**
 * @brief 搜索服务层
 * 
 * 职责：
 * 1. 统一搜索逻辑
 * 2. 支持FTS全文搜索和传统搜索
 * 3. 搜索条件构建
 * 4. 搜索结果管理
 */
/**
 * @brief 搜索服务层
 * 
 * 符合依赖倒置原则（DIP）：
 * - 依赖IDatabaseAccessor接口，而不是DatabaseManager具体类
 * - 便于单元测试和扩展
 */
class SearchService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数 - 使用依赖注入
     * @param dbAccessor 数据库访问接口（实现依赖倒置原则）
     * @param parent 父对象
     */
    explicit SearchService(IDatabaseAccessor *dbAccessor, QObject *parent = nullptr);

    // 搜索方法
    QStandardItemModel* searchProjects(const QString &keyword, const QString &filterType = "all");
    void searchInPaginationModel(SimplePaginationModel *model, const QString &keyword, const QString &filterType = "all");
    void clearSearchInPaginationModel(SimplePaginationModel *model);

    // 搜索条件转换
    static QString filterTypeFromIndex(int index);
    static int filterIndexFromType(const QString &filterType);

signals:
    void searchCompleted(int resultCount);
    void searchFailed(const QString &error);

private:
    IDatabaseAccessor *m_dbAccessor;  // 依赖接口（依赖倒置原则）
    DatabaseManager *m_dbManager;     // 兼容性：保持对DatabaseManager的直接访问
    
    // 搜索辅助方法
    QStandardItemModel* performFTSSearch(const QString &keyword, const QString &filterType);
    QStandardItemModel* performTraditionalSearch(const QString &keyword, const QString &filterType);
};

#endif // SEARCHSERVICE_H

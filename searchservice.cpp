#include "searchservice.h"
#include "databasemanager.h"
#include "simplepaginationmodel.h"
#include <QDebug>

SearchService::SearchService(IDatabaseAccessor *dbAccessor, QObject *parent)
    : QObject(parent)
    , m_dbManager(dynamic_cast<DatabaseManager*>(dbAccessor))  // 兼容性
    , m_dbAccessor(dbAccessor)  // 依赖注入：使用接口
{
    if (!m_dbAccessor) {
        qDebug() << "警告：SearchService接收到空的数据库访问接口";
    }
}

QStandardItemModel* SearchService::searchProjects(const QString &keyword, const QString &filterType)
{
    if (keyword.isEmpty()) {
        emit searchFailed("搜索关键词不能为空");
        return nullptr;
    }

    // 优先使用FTS全文搜索（如果可用），否则使用传统搜索
    QStandardItemModel* searchModel = performFTSSearch(keyword, filterType);
    
    if (!searchModel || searchModel->rowCount() == 0) {
        // 如果FTS搜索无结果，尝试传统搜索
        if (searchModel) {
            delete searchModel;
            searchModel = nullptr;
        }
        searchModel = performTraditionalSearch(keyword, filterType);
    }

    if (searchModel) {
        emit searchCompleted(searchModel->rowCount());
    } else {
        emit searchFailed(m_dbAccessor ? m_dbAccessor->getLastError() : "搜索失败");
    }

    return searchModel;
}

void SearchService::searchInPaginationModel(SimplePaginationModel *model, const QString &keyword, const QString &filterType)
{
    if (!model) {
        emit searchFailed("分页模型为空");
        return;
    }

    if (keyword.isEmpty()) {
        emit searchFailed("搜索关键词不能为空");
        return;
    }

    model->setSearchFilter(keyword, filterType);
    emit searchCompleted(0); // 分页模型会通过信号通知结果
}

void SearchService::clearSearchInPaginationModel(SimplePaginationModel *model)
{
    if (model) {
        model->clearSearchFilter();
    }
}

QString SearchService::filterTypeFromIndex(int index)
{
    switch (index) {
    case 0: return "all";
    case 1: return "name";
    case 2: return "manager";
    case 3: return "status";
    case 4: return "id";
    default: return "all";
    }
}

int SearchService::filterIndexFromType(const QString &filterType)
{
    if (filterType == "all") return 0;
    if (filterType == "name") return 1;
    if (filterType == "manager") return 2;
    if (filterType == "status") return 3;
    if (filterType == "id") return 4;
    return 0;
}

QStandardItemModel* SearchService::performFTSSearch(const QString &keyword, const QString &filterType)
{
    if (!m_dbManager) {
        return nullptr;
    }
    return m_dbManager->searchProjectsFTS(keyword, filterType);
}

QStandardItemModel* SearchService::performTraditionalSearch(const QString &keyword, const QString &filterType)
{
    if (!m_dbManager) {
        return nullptr;
    }
    return m_dbManager->searchProjects(keyword, filterType);
}

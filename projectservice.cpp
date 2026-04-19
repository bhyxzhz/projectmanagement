#include "projectservice.h"
#include "databasemanager.h"
#include "appconstants.h"
#include "errorhandler.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDateTime>
#include <QDebug>

ProjectService::ProjectService(IDatabaseAccessor *dbAccessor, QObject *parent)
    : QObject(parent)
    , m_dbManager(dynamic_cast<DatabaseManager*>(dbAccessor))  // 兼容性：保持对DatabaseManager的访问
    , m_dbAccessor(dbAccessor)  // 依赖注入：使用接口
{
    // 验证接口有效性
    if (!m_dbAccessor) {
        qDebug() << "警告：ProjectService接收到空的数据库访问接口";
    }
}

bool ProjectService::addProject(const QStringList &projectData, QString &errorMessage)
{
    // 验证数据
    if (!validateProjectData(projectData, errorMessage)) {
        return false;
    }

    // 检查项目ID是否已存在
    QString projectId = projectData[0];
    if (m_dbAccessor && m_dbAccessor->isConnected()) {
        QSqlQuery checkQuery(*m_dbAccessor->getDatabase());
        checkQuery.prepare("SELECT COUNT(*) FROM projects WHERE project_id = ?");
        checkQuery.bindValue(0, projectId);

        if (checkQuery.exec() && checkQuery.next()) {
            if (checkQuery.value(0).toInt() > 0) {
                ErrorInfo error = ErrorHandler::recordError(
                    ErrorCode::DuplicateData,
                    ErrorLevel::Warning,
                    QString("项目ID已存在: %1").arg(projectId),
                    "ProjectService::addProject()",
                    QString("项目ID: %1").arg(projectId)
                );
                errorMessage = error.getShortDescription();
                return false;
            }
        }
    }

    // 添加到数据库（通过接口）
    if (m_dbAccessor && m_dbAccessor->addProject(projectData)) {
        emit projectAdded(projectId);
        emit operationCompleted("添加项目", true, QString("项目 %1 添加成功").arg(projectId));
        return true;
    } else {
        errorMessage = m_dbAccessor ? m_dbAccessor->getLastError() : "数据库访问接口无效";
        emit operationCompleted("添加项目", false, errorMessage);
        return false;
    }
}

bool ProjectService::updateProject(const QString &projectId, const QStringList &projectData, QString &errorMessage)
{
    // 验证数据
    if (!validateProjectData(projectData, errorMessage)) {
        return false;
    }

    // 更新数据库（通过接口）
    if (m_dbAccessor && m_dbAccessor->updateProjectById(projectId, projectData)) {
        emit projectUpdated(projectId);
        emit operationCompleted("更新项目", true, QString("项目 %1 更新成功").arg(projectId));
        return true;
    } else {
        QString dbError = m_dbManager->getLastError();
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::OperationFailed,
            ErrorLevel::Error,
            "更新项目失败",
            "ProjectService::updateProject()",
            dbError
        );
        errorMessage = error.getShortDescription();
        emit operationCompleted("更新项目", false, errorMessage);
        return false;
    }
}

bool ProjectService::deleteProject(const QString &projectId, QString &errorMessage)
{
    // 删除项目（通过接口）
    if (m_dbAccessor && m_dbAccessor->deleteProjectById(projectId)) {
        emit projectDeleted(projectId);
        emit operationCompleted("删除项目", true, "项目已删除");
        return true;
    } else {
        QString dbError = m_dbManager->getLastError();
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::OperationFailed,
            ErrorLevel::Error,
            "删除项目失败",
            "ProjectService::deleteProject()",
            dbError
        );
        errorMessage = error.getShortDescription();
        emit operationCompleted("删除项目", false, errorMessage);
        return false;
    }
}

QString ProjectService::generateUniqueProjectId()
{
    if (!m_dbAccessor || !m_dbAccessor->isConnected()) {
        return "PRJ001";
    }

    // 使用事务确保原子性，避免竞争条件
    QSqlDatabase *db = m_dbAccessor->getDatabase();
    if (!db->transaction()) {
        qDebug() << "无法开始事务，使用默认ID";
        return "PRJ001";
    }

    // 查询数据库中最大的项目ID（使用预编译语句）
    QSqlQuery query(*db);
    query.prepare("SELECT MAX(CAST(SUBSTR(project_id, 4) AS INTEGER)) FROM projects WHERE project_id LIKE 'PRJ%'");
    
    int maxId = 0;
    if (query.exec() && query.next()) {
        bool ok;
        maxId = query.value(0).toInt(&ok);
        if (!ok) {
            maxId = 0;
        }
    }

    // 生成新的唯一ID
    maxId++;
    QString newId = QString("PRJ%1").arg(maxId, 3, 10, QLatin1Char('0'));

    // 检查ID是否已存在，如果存在则递增直到找到可用的ID
    int attempts = 0;
    while (attempts < DatabaseConstants::MAX_ID_GENERATION_ATTEMPTS) {
        QSqlQuery checkQuery(*db);
        checkQuery.prepare("SELECT COUNT(*) FROM projects WHERE project_id = ?");
        checkQuery.bindValue(0, newId);

        if (checkQuery.exec() && checkQuery.next()) {
            if (checkQuery.value(0).toInt() == 0) {
                break; // ID不存在，可以使用
            }
        }

        maxId++;
        newId = QString("PRJ%1").arg(maxId, 3, 10, QLatin1Char('0'));
        attempts++;
    }

    // 提交事务
    if (!db->commit()) {
        qDebug() << "提交事务失败，回滚";
        db->rollback();
    }

    if (attempts >= DatabaseConstants::MAX_ID_GENERATION_ATTEMPTS) {
        qDebug() << "警告：生成唯一ID时尝试次数过多，可能存在问题";
        // 使用时间戳作为后备方案
        newId = QString("PRJ%1").arg(QDateTime::currentMSecsSinceEpoch() % 999999, 3, 10, QLatin1Char('0'));
    }

    return newId;
}

bool ProjectService::validateProjectData(const QStringList &projectData, QString &errorMessage)
{
    if (projectData.size() < 8) {
        errorMessage = "项目数据不完整";
        return false;
    }

    // 验证项目名称
    if (!validateProjectName(projectData[1], errorMessage)) {
        return false;
    }

    // 验证项目经理
    if (!validateManager(projectData[2], errorMessage)) {
        return false;
    }

    // 验证日期
    if (!validateDates(projectData[3], projectData[4], errorMessage)) {
        return false;
    }

    // 验证预算
    if (!validateBudget(projectData[5], errorMessage)) {
        return false;
    }

    // 验证描述长度
    if (projectData[7].length() > ProjectValidationConstants::MAX_DESCRIPTION_LENGTH) {
        errorMessage = QString("项目描述长度不能超过%1个字符").arg(ProjectValidationConstants::MAX_DESCRIPTION_LENGTH);
        return false;
    }

    return true;
}

bool ProjectService::validateProjectName(const QString &name, QString &errorMessage)
{
    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            "项目名称不能为空",
            "ProjectService::validateProjectName()",
            "project_name"
        );
        errorMessage = error.getShortDescription();
        return false;
    }
    if (trimmedName.length() > ProjectValidationConstants::MAX_PROJECT_NAME_LENGTH) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("项目名称长度不能超过%1个字符").arg(ProjectValidationConstants::MAX_PROJECT_NAME_LENGTH),
            "ProjectService::validateProjectName()",
            "project_name"
        );
        errorMessage = error.getShortDescription();
        return false;
    }
    return true;
}

bool ProjectService::validateManager(const QString &manager, QString &errorMessage)
{
    QString trimmedManager = manager.trimmed();
    if (trimmedManager.isEmpty()) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            "项目经理姓名不能为空",
            "ProjectService::validateManager()",
            "manager"
        );
        errorMessage = error.getShortDescription();
        return false;
    }
    if (trimmedManager.length() > ProjectValidationConstants::MAX_MANAGER_NAME_LENGTH) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("项目经理姓名长度不能超过%1个字符").arg(ProjectValidationConstants::MAX_MANAGER_NAME_LENGTH),
            "ProjectService::validateManager()",
            "manager"
        );
        errorMessage = error.getShortDescription();
        return false;
    }
    return true;
}

bool ProjectService::validateDates(const QString &startDate, const QString &endDate, QString &errorMessage)
{
    QDate start = QDate::fromString(startDate, "yyyy-MM-dd");
    QDate end = QDate::fromString(endDate, "yyyy-MM-dd");

    if (!start.isValid() || !end.isValid()) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("日期格式不正确（开始: %1, 结束: %2）").arg(startDate, endDate),
            "ProjectService::validateDates()",
            "dates"
        );
        errorMessage = error.getShortDescription();
        return false;
    }

    if (start > end) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("开始日期不能晚于结束日期（开始: %1, 结束: %2）").arg(startDate, endDate),
            "ProjectService::validateDates()",
            "dates"
        );
        errorMessage = error.getShortDescription();
        return false;
    }

    return true;
}

bool ProjectService::validateBudget(const QString &budget, QString &errorMessage)
{
    bool ok;
    double budgetValue = budget.toDouble(&ok);
    
    if (!ok) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("预算格式不正确: %1").arg(budget),
            "ProjectService::validateBudget()",
            "budget"
        );
        errorMessage = error.getShortDescription();
        return false;
    }

    if (budgetValue < ProjectValidationConstants::MIN_BUDGET || budgetValue > ProjectValidationConstants::MAX_BUDGET) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            QString("预算必须在%1到%2万元之间（当前值: %3）")
                .arg(ProjectValidationConstants::MIN_BUDGET)
                .arg(ProjectValidationConstants::MAX_BUDGET)
                .arg(budgetValue),
            "ProjectService::validateBudget()",
            "budget"
        );
        errorMessage = error.getShortDescription();
        return false;
    }

    return true;
}

QStringList ProjectService::getProjectData(const QString &projectId, QString &errorMessage)
{
    QStringList projectData;
    
    if (!m_dbAccessor || !m_dbAccessor->isConnected()) {
        errorMessage = "数据库未连接";
        return projectData;
    }

    QSqlQuery query(*m_dbAccessor->getDatabase());
    query.prepare("SELECT project_id, project_name, manager, start_date, end_date, budget, status, description "
                  "FROM projects WHERE project_id = ?");
    query.bindValue(0, projectId);

    if (query.exec() && query.next()) {
        for (int i = 0; i < 8; ++i) {
            projectData << query.value(i).toString();
        }
    } else {
        errorMessage = "项目不存在: " + projectId;
    }

    return projectData;
}

int ProjectService::getTotalProjectCount() const
{
    return m_dbManager ? m_dbManager->getProjectCount() : 0;
}

double ProjectService::getTotalBudget() const
{
    return m_dbAccessor ? m_dbAccessor->getTotalBudget() : 0.0;
}

QStringList ProjectService::getProjectStatusStats() const
{
    return m_dbAccessor ? m_dbAccessor->getProjectStatusStats() : QStringList();
}

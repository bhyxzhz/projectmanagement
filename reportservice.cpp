#include "reportservice.h"
#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDate>
#include <algorithm>
#include <cmath>

ReportService::ReportService(IDatabaseAccessor *dbAccessor, QObject *parent)
    : QObject(parent)
    , m_dbAccessor(dbAccessor)
{
    // 兼容性：保持对DatabaseManager的直接访问
    m_dbManager = dynamic_cast<DatabaseManager*>(dbAccessor);
}

int ReportService::getTotalProjectCount()
{
    if (!m_dbAccessor || !m_dbManager) {
        return 0;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT COUNT(*) FROM projects");
    
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

double ReportService::getTotalBudget()
{
    if (!m_dbAccessor || !m_dbManager) {
        return 0.0;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT SUM(budget) FROM projects");
    
    if (query.exec() && query.next()) {
        return query.value(0).toDouble();
    }
    
    return 0.0;
}

double ReportService::getAverageBudget()
{
    int count = getTotalProjectCount();
    if (count == 0) {
        return 0.0;
    }
    return getTotalBudget() / count;
}

QMap<QString, int> ReportService::getStatusStatistics()
{
    QMap<QString, int> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT status, COUNT(*) FROM projects GROUP BY status");
    
    if (query.exec()) {
        while (query.next()) {
            QString status = query.value(0).toString();
            int count = query.value(1).toInt();
            stats[status] = count;
        }
    }
    
    return stats;
}

QMap<QString, int> ReportService::getManagerStatistics()
{
    QMap<QString, int> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT manager, COUNT(*) FROM projects GROUP BY manager ORDER BY COUNT(*) DESC");
    
    if (query.exec()) {
        while (query.next()) {
            QString manager = query.value(0).toString();
            int count = query.value(1).toInt();
            stats[manager] = count;
        }
    }
    
    return stats;
}

QMap<QString, double> ReportService::getManagerBudgetStatistics()
{
    QMap<QString, double> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT manager, SUM(budget) FROM projects GROUP BY manager ORDER BY SUM(budget) DESC");
    
    if (query.exec()) {
        while (query.next()) {
            QString manager = query.value(0).toString();
            double total = query.value(1).toDouble();
            stats[manager] = total;
        }
    }
    
    return stats;
}

QMap<QString, int> ReportService::getProjectsByCreateDate(const QDate &startDate, const QDate &endDate, const QString &groupBy)
{
    QMap<QString, int> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QString dateFormat;
    if (groupBy == "day") {
        dateFormat = "DATE(created_at)";
    } else if (groupBy == "week") {
        dateFormat = "strftime('%Y-W%W', created_at)";
    } else if (groupBy == "month") {
        dateFormat = "strftime('%Y-%m', created_at)";
    } else if (groupBy == "year") {
        dateFormat = "strftime('%Y', created_at)";
    } else {
        dateFormat = "DATE(created_at)";
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare(QString("SELECT %1, COUNT(*) FROM projects WHERE DATE(created_at) BETWEEN ? AND ? GROUP BY %1 ORDER BY %1")
                  .arg(dateFormat));
    query.bindValue(0, startDate.toString("yyyy-MM-dd"));
    query.bindValue(1, endDate.toString("yyyy-MM-dd"));
    
    if (query.exec()) {
        while (query.next()) {
            QString dateKey = query.value(0).toString();
            int count = query.value(1).toInt();
            stats[dateKey] = count;
        }
    }
    
    return stats;
}

QMap<QString, int> ReportService::getProjectsByStartDate(const QDate &startDate, const QDate &endDate, const QString &groupBy)
{
    QMap<QString, int> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QString dateFormat;
    if (groupBy == "day") {
        dateFormat = "DATE(start_date)";
    } else if (groupBy == "week") {
        dateFormat = "strftime('%Y-W%W', start_date)";
    } else if (groupBy == "month") {
        dateFormat = "strftime('%Y-%m', start_date)";
    } else if (groupBy == "year") {
        dateFormat = "strftime('%Y', start_date)";
    } else {
        dateFormat = "DATE(start_date)";
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare(QString("SELECT %1, COUNT(*) FROM projects WHERE DATE(start_date) BETWEEN ? AND ? GROUP BY %1 ORDER BY %1")
                  .arg(dateFormat));
    query.bindValue(0, startDate.toString("yyyy-MM-dd"));
    query.bindValue(1, endDate.toString("yyyy-MM-dd"));
    
    if (query.exec()) {
        while (query.next()) {
            QString dateKey = query.value(0).toString();
            int count = query.value(1).toInt();
            stats[dateKey] = count;
        }
    }
    
    return stats;
}

QMap<QString, double> ReportService::getBudgetTrend(const QDate &startDate, const QDate &endDate, const QString &groupBy)
{
    QMap<QString, double> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QString dateFormat;
    if (groupBy == "day") {
        dateFormat = "DATE(created_at)";
    } else if (groupBy == "week") {
        dateFormat = "strftime('%Y-W%W', created_at)";
    } else if (groupBy == "month") {
        dateFormat = "strftime('%Y-%m', created_at)";
    } else if (groupBy == "year") {
        dateFormat = "strftime('%Y', created_at)";
    } else {
        dateFormat = "DATE(created_at)";
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare(QString("SELECT %1, SUM(budget) FROM projects WHERE DATE(created_at) BETWEEN ? AND ? GROUP BY %1 ORDER BY %1")
                  .arg(dateFormat));
    query.bindValue(0, startDate.toString("yyyy-MM-dd"));
    query.bindValue(1, endDate.toString("yyyy-MM-dd"));
    
    if (query.exec()) {
        while (query.next()) {
            QString dateKey = query.value(0).toString();
            double total = query.value(1).toDouble();
            stats[dateKey] = total;
        }
    }
    
    return stats;
}

QMap<QString, int> ReportService::getBudgetDistribution(const QList<double> &ranges)
{
    QMap<QString, int> distribution;
    
    if (!m_dbAccessor || !m_dbManager || ranges.isEmpty()) {
        return distribution;
    }

    // 初始化区间
    for (int i = 0; i < ranges.size() - 1; ++i) {
        QString rangeKey = QString("%1-%2").arg(ranges[i]).arg(ranges[i+1]);
        distribution[rangeKey] = 0;
    }
    // 最后一个区间（大于最大值）
    if (!ranges.isEmpty()) {
        QString rangeKey = QString(">%1").arg(ranges.last());
        distribution[rangeKey] = 0;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT budget FROM projects");
    
    if (query.exec()) {
        while (query.next()) {
            double budget = query.value(0).toDouble();
            bool found = false;
            
            for (int i = 0; i < ranges.size() - 1; ++i) {
                if (budget >= ranges[i] && budget < ranges[i+1]) {
                    QString rangeKey = QString("%1-%2").arg(ranges[i]).arg(ranges[i+1]);
                    distribution[rangeKey]++;
                    found = true;
                    break;
                }
            }
            
            if (!found && !ranges.isEmpty() && budget >= ranges.last()) {
                QString rangeKey = QString(">%1").arg(ranges.last());
                distribution[rangeKey]++;
            }
        }
    }
    
    return distribution;
}

int ReportService::getOverBudgetProjectsCount(double threshold)
{
    if (!m_dbAccessor || !m_dbManager) {
        return 0;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT COUNT(*) FROM projects WHERE budget > ?");
    query.bindValue(0, threshold);
    
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

QList<QPair<QString, double>> ReportService::getTopBudgetProjects(int topN)
{
    QList<QPair<QString, double>> topProjects;
    
    if (!m_dbAccessor || !m_dbManager) {
        return topProjects;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT project_name, budget FROM projects ORDER BY budget DESC LIMIT ?");
    query.bindValue(0, topN);
    
    if (query.exec()) {
        while (query.next()) {
            QString name = query.value(0).toString();
            double budget = query.value(1).toDouble();
            topProjects.append(qMakePair(name, budget));
        }
    }
    
    return topProjects;
}

QMap<QString, double> ReportService::getBudgetStatistics()
{
    QMap<QString, double> stats;
    
    if (!m_dbAccessor || !m_dbManager) {
        return stats;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT MIN(budget), MAX(budget), AVG(budget) FROM projects");
    
    if (query.exec() && query.next()) {
        stats["min"] = query.value(0).toDouble();
        stats["max"] = query.value(1).toDouble();
        stats["avg"] = query.value(2).toDouble();
    }

    // 计算中位数
    QList<double> budgets;
    QSqlQuery budgetQuery(*m_dbManager->getDatabase());
    budgetQuery.prepare("SELECT budget FROM projects ORDER BY budget");
    
    if (budgetQuery.exec()) {
        while (budgetQuery.next()) {
            budgets.append(budgetQuery.value(0).toDouble());
        }
    }
    
    stats["median"] = calculateMedian(budgets);
    
    return stats;
}

QMap<QString, double> ReportService::getStatusPercentage()
{
    QMap<QString, double> percentages;
    QMap<QString, int> statusStats = getStatusStatistics();
    
    int total = getTotalProjectCount();
    if (total == 0) {
        return percentages;
    }
    
    for (auto it = statusStats.begin(); it != statusStats.end(); ++it) {
        double percentage = (it.value() * 100.0) / total;
        percentages[it.key()] = percentage;
    }
    
    return percentages;
}

double ReportService::getCompletionRate()
{
    QMap<QString, int> statusStats = getStatusStatistics();
    int total = getTotalProjectCount();
    
    if (total == 0) {
        return 0.0;
    }
    
    int completed = 0;
    QStringList completedStatuses = {"已完成", "完成", "已完成", "closed", "done"};
    
    for (const QString &status : completedStatuses) {
        if (statusStats.contains(status)) {
            completed += statusStats[status];
        }
    }
    
    return (completed * 100.0) / total;
}

int ReportService::getDelayedProjectsCount()
{
    if (!m_dbAccessor || !m_dbManager) {
        return 0;
    }

    QDate today = QDate::currentDate();
    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare("SELECT COUNT(*) FROM projects WHERE DATE(end_date) < ? AND status NOT IN ('已完成', '完成', 'closed', 'done')");
    query.bindValue(0, today.toString("yyyy-MM-dd"));
    
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}

QMap<QString, int> ReportService::getManagerProjectCounts()
{
    return getManagerStatistics();
}

QMap<QString, double> ReportService::getManagerTotalBudgets()
{
    return getManagerBudgetStatistics();
}

QMap<QString, double> ReportService::getManagerCompletionRates()
{
    QMap<QString, double> rates;
    
    if (!m_dbAccessor || !m_dbManager) {
        return rates;
    }

    QSqlQuery query(*m_dbManager->getDatabase());
    query.prepare(R"(
        SELECT manager, 
               COUNT(*) as total,
               SUM(CASE WHEN status IN ('已完成', '完成', 'closed', 'done') THEN 1 ELSE 0 END) as completed
        FROM projects 
        GROUP BY manager
    )");
    
    if (query.exec()) {
        while (query.next()) {
            QString manager = query.value(0).toString();
            int total = query.value(1).toInt();
            int completed = query.value(2).toInt();
            
            if (total > 0) {
                double rate = (completed * 100.0) / total;
                rates[manager] = rate;
            }
        }
    }
    
    return rates;
}

QMap<QString, QVariant> ReportService::getOverviewReport()
{
    QMap<QString, QVariant> report;
    
    report["totalProjects"] = getTotalProjectCount();
    report["totalBudget"] = getTotalBudget();
    report["averageBudget"] = getAverageBudget();
    report["statusStatistics"] = QVariant::fromValue(getStatusStatistics());
    report["managerStatistics"] = QVariant::fromValue(getManagerStatistics());
    report["completionRate"] = getCompletionRate();
    report["delayedProjects"] = getDelayedProjectsCount();
    
    return report;
}

QMap<QString, QVariant> ReportService::getStatisticsReport()
{
    QMap<QString, QVariant> report;
    
    report["statusStats"] = QVariant::fromValue(getStatusStatistics());
    report["statusPercentages"] = QVariant::fromValue(getStatusPercentage());
    report["managerStats"] = QVariant::fromValue(getManagerStatistics());
    report["managerBudgets"] = QVariant::fromValue(getManagerBudgetStatistics());
    report["budgetStats"] = QVariant::fromValue(getBudgetStatistics());
    report["topProjects"] = QVariant::fromValue(getTopBudgetProjects(10));
    
    return report;
}

QMap<QString, QVariant> ReportService::getTrendReport(const QDate &startDate, const QDate &endDate)
{
    QMap<QString, QVariant> report;
    
    report["projectsByCreateDate"] = QVariant::fromValue(getProjectsByCreateDate(startDate, endDate, "month"));
    report["projectsByStartDate"] = QVariant::fromValue(getProjectsByStartDate(startDate, endDate, "month"));
    report["budgetTrend"] = QVariant::fromValue(getBudgetTrend(startDate, endDate, "month"));
    
    return report;
}

QDate ReportService::parseDate(const QString &dateStr)
{
    return QDate::fromString(dateStr, "yyyy-MM-dd");
}

double ReportService::calculateMedian(const QList<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    
    QList<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    
    int size = sorted.size();
    if (size % 2 == 0) {
        return (sorted[size/2 - 1] + sorted[size/2]) / 2.0;
    } else {
        return sorted[size/2];
    }
}

int ReportService::calculateDaysBetween(const QDate &start, const QDate &end)
{
    return start.daysTo(end);
}

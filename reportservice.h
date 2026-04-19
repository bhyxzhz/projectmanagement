#ifndef REPORTSERVICE_H
#define REPORTSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <QDate>
#include "idatabaseaccessor.h"

// 前向声明
class DatabaseManager;

/**
 * @brief 报表业务服务层
 * 
 * 职责：
 * 1. 数据统计计算
 * 2. 报表数据生成
 * 3. 报表导出
 */
class ReportService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数 - 使用依赖注入
     * @param dbAccessor 数据库访问接口
     * @param parent 父对象
     */
    explicit ReportService(IDatabaseAccessor *dbAccessor, QObject *parent = nullptr);

    // 基础统计
    int getTotalProjectCount();
    double getTotalBudget();
    double getAverageBudget();
    QMap<QString, int> getStatusStatistics();
    QMap<QString, int> getManagerStatistics();
    QMap<QString, double> getManagerBudgetStatistics();

    // 时间维度统计
    QMap<QString, int> getProjectsByCreateDate(const QDate &startDate, const QDate &endDate, const QString &groupBy = "day");
    QMap<QString, int> getProjectsByStartDate(const QDate &startDate, const QDate &endDate, const QString &groupBy = "day");
    QMap<QString, double> getBudgetTrend(const QDate &startDate, const QDate &endDate, const QString &groupBy = "day");

    // 预算分析
    QMap<QString, int> getBudgetDistribution(const QList<double> &ranges);
    int getOverBudgetProjectsCount(double threshold);
    QList<QPair<QString, double>> getTopBudgetProjects(int topN = 10);
    QMap<QString, double> getBudgetStatistics(); // 平均值、中位数、最大值、最小值

    // 项目状态分析
    QMap<QString, double> getStatusPercentage();
    double getCompletionRate();
    int getDelayedProjectsCount();

    // 项目经理分析
    QMap<QString, int> getManagerProjectCounts();
    QMap<QString, double> getManagerTotalBudgets();
    QMap<QString, double> getManagerCompletionRates();

    // 综合报表数据
    QMap<QString, QVariant> getOverviewReport();
    QMap<QString, QVariant> getStatisticsReport();
    QMap<QString, QVariant> getTrendReport(const QDate &startDate, const QDate &endDate);

signals:
    void reportGenerated(const QString &reportType, bool success, const QString &message);

private:
    IDatabaseAccessor *m_dbAccessor;
    DatabaseManager *m_dbManager;

    // 辅助方法
    QDate parseDate(const QString &dateStr);
    double calculateMedian(const QList<double> &values);
    int calculateDaysBetween(const QDate &start, const QDate &end);
};

#endif // REPORTSERVICE_H

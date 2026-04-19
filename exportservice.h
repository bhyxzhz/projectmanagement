#ifndef EXPORTSERVICE_H
#define EXPORTSERVICE_H

#include <QObject>
#include <QString>
#include <functional>
#include "appconstants.h"

class QAbstractItemModel;
class QSqlDatabase;
class QFile;
class QTextStream;

/**
 * @brief 数据导出服务层
 * 
 * 职责：
 * 1. CSV格式导出
 * 2. 大数据量流式导出
 * 3. 导出进度管理
 * 4. 文件分割（大文件）
 */
class ExportService : public QObject
{
    Q_OBJECT

public:
    explicit ExportService(QObject *parent = nullptr);

    // 导出方法
    bool exportToCSV(QAbstractItemModel *model, const QString &filePath, 
                     std::function<void(int, int)> progressCallback = nullptr);
    bool exportFromDatabase(QSqlDatabase *database, const QString &basePath,
                           std::function<void(int, int)> progressCallback = nullptr);
    
    // 获取导出结果
    QStringList getCreatedFiles() const { return m_createdFiles; }
    int getExportedRecordCount() const { return m_exportedRecordCount; }

signals:
    void exportProgress(int current, int total);
    void exportCompleted(const QString &filePath, int recordCount);
    void exportFailed(const QString &error);

private:
    // 使用全局常量定义（在appconstants.h中定义）
    
    // 导出辅助方法
    bool writeCSVHeader(QTextStream &out, QAbstractItemModel *model);
    bool writeCSVRow(QTextStream &out, const QStringList &rowData);
    QString escapeCSVField(const QString &field);
    QString generateFileName(const QString &basePath, int fileIndex);
    bool createNewFile(const QString &basePath, int fileIndex, QFile* &file, QTextStream* &stream, const QStringList &headers);
    
    // 导出结果
    QStringList m_createdFiles;
    int m_exportedRecordCount;
};

#endif // EXPORTSERVICE_H

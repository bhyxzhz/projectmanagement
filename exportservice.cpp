#include "exportservice.h"
#include "appconstants.h"
#include <QAbstractItemModel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDebug>
#include <QModelIndex>

ExportService::ExportService(QObject *parent)
    : QObject(parent)
    , m_exportedRecordCount(0)
{
}

bool ExportService::exportToCSV(QAbstractItemModel *model, const QString &filePath,
                                std::function<void(int, int)> progressCallback)
{
    if (!model) {
        emit exportFailed("模型为空");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFailed("无法创建文件: " + file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    // 写入BOM头，确保Excel正确识别UTF-8编码
    out << "\xEF\xBB\xBF";
    
    // 写入表头
    if (!writeCSVHeader(out, model)) {
        file.close();
        emit exportFailed("写入表头失败");
        return false;
    }

    int totalRows = model->rowCount();
    int exportedRows = 0;

    // 导出数据
    for (int row = 0; row < totalRows; ++row) {
        QStringList rowData;
        for (int col = 0; col < model->columnCount(); ++col) {
            QModelIndex index = model->index(row, col);
            QString cellData = model->data(index, Qt::DisplayRole).toString();
            rowData << escapeCSVField(cellData);
        }
        
        if (!writeCSVRow(out, rowData)) {
            file.close();
            emit exportFailed("写入数据失败");
            return false;
        }
        
        exportedRows++;
        
            // 更新进度（按指定频率更新，避免频繁更新影响性能）
            if (progressCallback && exportedRows % ExportConstants::PROGRESS_UPDATE_FREQUENCY == 0) {
                progressCallback(exportedRows, totalRows);
            }
            if (exportedRows % ExportConstants::PROGRESS_UPDATE_FREQUENCY == 0) {
                emit exportProgress(exportedRows, totalRows);
            }
    }

    file.close();
    emit exportCompleted(filePath, exportedRows);
    return true;
}

bool ExportService::exportFromDatabase(QSqlDatabase *database, const QString &basePath,
                                      std::function<void(int, int)> progressCallback)
{
    if (!database || !database->isOpen()) {
        emit exportFailed("数据库未连接");
        return false;
    }

    m_createdFiles.clear();
    m_exportedRecordCount = 0;
    
    // 获取总记录数
    QSqlQuery countQuery(*database);
    countQuery.prepare("SELECT COUNT(*) FROM projects");
    int totalRecords = 0;
    if (countQuery.exec() && countQuery.next()) {
        totalRecords = countQuery.value(0).toInt();
    }

    QStringList headers = {"项目ID", "项目名称", "项目经理", "开始日期",
                           "结束日期", "预算(万元)", "状态", "描述"};
    
    QFile* currentFile = nullptr;
    QTextStream* currentOut = nullptr;
    int currentFileIndex = 1;
    int currentFileRecords = 0;

    // 创建第一个文件
    if (!createNewFile(basePath, currentFileIndex, currentFile, currentOut, headers)) {
        emit exportFailed("无法创建文件");
        return false;
    }

    // 流式导出数据
    QSqlQuery query(*database);
    query.prepare(R"(
        SELECT project_id, project_name, manager, start_date, 
               end_date, budget, status, description
        FROM projects
        ORDER BY created_at DESC, project_id DESC
    )");
    query.setForwardOnly(true);  // 只向前查询，节省内存

    if (query.exec()) {
        while (query.next()) {
            // 检查是否需要创建新文件（达到单文件最大记录数时分割）
            if (currentFileRecords >= ExportConstants::MAX_RECORDS_PER_FILE) {
                if (currentFile) {
                    currentFile->close();
                    delete currentFile;
                    delete currentOut;
                }
                currentFileIndex++;
                if (!createNewFile(basePath, currentFileIndex, currentFile, currentOut, headers)) {
                    emit exportFailed("无法创建新文件");
                    return false;
                }
                currentFileRecords = 0;
            }

            QStringList rowData;
            for (int i = 0; i < 8; ++i) {
                rowData << escapeCSVField(query.value(i).toString());
            }
            *currentOut << rowData.join(",") << "\n";
            currentFileRecords++;
            m_exportedRecordCount++;

            // 更新进度
            if (progressCallback && m_exportedRecordCount % 1000 == 0) {
                progressCallback(m_exportedRecordCount, totalRecords);
            }
            if (m_exportedRecordCount % 1000 == 0) {
                emit exportProgress(m_exportedRecordCount, totalRecords);
            }
        }
    } else {
        if (currentFile) {
            currentFile->close();
            delete currentFile;
            delete currentOut;
        }
        emit exportFailed("查询失败: " + query.lastError().text());
        return false;
    }

    // 关闭最后一个文件
    if (currentFile) {
        currentFile->close();
        delete currentFile;
        delete currentOut;
    }

    emit exportCompleted(basePath, m_exportedRecordCount);
    return true;
}

bool ExportService::writeCSVHeader(QTextStream &out, QAbstractItemModel *model)
{
    QStringList headers;
    for (int col = 0; col < model->columnCount(); ++col) {
        QString header = model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
        headers << escapeCSVField(header);
    }
    out << headers.join(",") << "\n";
    return true;
}

bool ExportService::writeCSVRow(QTextStream &out, const QStringList &rowData)
{
    out << rowData.join(",") << "\n";
    return true;
}

QString ExportService::escapeCSVField(const QString &field)
{
    QString escaped = field;
    
    // 如果包含逗号、引号或换行符，需要用引号包围并转义
    if (escaped.contains(",") || escaped.contains("\"") || escaped.contains("\n")) {
        escaped.replace("\"", "\"\""); // 转义引号
        escaped = "\"" + escaped + "\"";
    }
    
    return escaped;
}

QString ExportService::generateFileName(const QString &basePath, int fileIndex)
{
    return QString("%1_%2.csv").arg(basePath).arg(fileIndex, 2, 10, QChar('0'));
}

bool ExportService::createNewFile(const QString &basePath, int fileIndex, QFile* &file, QTextStream* &stream, const QStringList &headers)
{
    QString fileName = QString("%1_%2.csv")
                        .arg(basePath)
                        .arg(fileIndex, 2, 10, QChar('0'));
    
    file = new QFile(fileName);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete file;
        file = nullptr;
        return false;
    }

    stream = new QTextStream(file);
    stream->setEncoding(QStringConverter::Utf8);
    
    // 写入BOM头，确保Excel正确识别UTF-8编码
    *stream << "\xEF\xBB\xBF";
    
    // 写入表头
    QStringList escapedHeaders;
    for (const QString &header : headers) {
        escapedHeaders << escapeCSVField(header);
    }
    *stream << escapedHeaders.join(",") << "\n";
    
    m_createdFiles << fileName;
    return true;
}

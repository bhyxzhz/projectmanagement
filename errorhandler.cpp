#include "errorhandler.h"
#include <QDebug>

ErrorInfo ErrorHandler::recordError(ErrorCode code,
                                    ErrorLevel level,
                                    const QString &message,
                                    const QString &context,
                                    const QString &details)
{
    ErrorInfo error;
    error.code = code;
    error.level = level;
    error.message = message;
    error.context = context;
    error.details = details;
    error.timestamp = QDateTime::currentDateTime();
    
    // 记录到日志（根据错误级别选择不同的日志级别）
    QString logMessage = formatError(error);
    switch (level) {
        case ErrorLevel::Info:
            qDebug() << "[INFO]" << logMessage;
            break;
        case ErrorLevel::Warning:
            qDebug() << "[WARNING]" << logMessage;
            break;
        case ErrorLevel::Error:
            qDebug() << "[ERROR]" << logMessage;
            break;
        case ErrorLevel::Critical:
            qDebug() << "[CRITICAL]" << logMessage;
            break;
    }
    
    return error;
}

ErrorInfo ErrorHandler::recordDatabaseError(ErrorCode code,
                                            const QString &message,
                                            const QString &context,
                                            const QString &sqlError)
{
    QString fullMessage = message;
    if (!sqlError.isEmpty()) {
        fullMessage += ": " + sqlError;
    }
    
    return recordError(code, ErrorLevel::Error, fullMessage, context, sqlError);
}

ErrorInfo ErrorHandler::recordValidationError(const QString &message,
                                               const QString &context,
                                               const QString &fieldName)
{
    QString fullContext = context;
    if (!fieldName.isEmpty()) {
        fullContext += QString(" (字段: %1)").arg(fieldName);
    }
    
    return recordError(ErrorCode::ValidationFailed, ErrorLevel::Warning, message, fullContext);
}

QString ErrorHandler::formatError(const ErrorInfo &error)
{
    QString formatted = QString("[%1][%2][%3] %4")
                       .arg(error.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"))
                       .arg(getErrorLevelDescription(error.level))
                       .arg(getErrorCodeDescription(error.code))
                       .arg(error.message);
    
    if (!error.context.isEmpty()) {
        formatted += QString(" | 上下文: %1").arg(error.context);
    }
    if (!error.details.isEmpty()) {
        formatted += QString(" | 详情: %1").arg(error.details);
    }
    
    return formatted;
}

QString ErrorHandler::formatUserMessage(const ErrorInfo &error)
{
    // 用户友好的错误提示（不包含技术细节）
    QString userMessage = error.message;
    
    // 对于某些错误，提供更友好的提示
    switch (error.code) {
        case ErrorCode::DatabaseConnectionFailed:
            userMessage = "无法连接到数据库，请检查数据库文件是否存在。";
            break;
        case ErrorCode::DatabaseQueryFailed:
            userMessage = "数据库操作失败，请稍后重试。";
            break;
        case ErrorCode::ValidationFailed:
            // 验证错误消息通常已经足够友好
            break;
        case ErrorCode::ResourceNotFound:
            userMessage = "未找到请求的资源。";
            break;
        default:
            // 使用原始消息
            break;
    }
    
    return userMessage;
}

QString ErrorHandler::getErrorCodeDescription(ErrorCode code)
{
    switch (code) {
        case ErrorCode::DatabaseConnectionFailed:
            return "DB_CONNECTION_FAILED";
        case ErrorCode::DatabaseQueryFailed:
            return "DB_QUERY_FAILED";
        case ErrorCode::DatabaseTransactionFailed:
            return "DB_TRANSACTION_FAILED";
        case ErrorCode::DatabaseRollbackFailed:
            return "DB_ROLLBACK_FAILED";
        case ErrorCode::ValidationFailed:
            return "VALIDATION_FAILED";
        case ErrorCode::InvalidDataFormat:
            return "INVALID_DATA_FORMAT";
        case ErrorCode::DataOutOfRange:
            return "DATA_OUT_OF_RANGE";
        case ErrorCode::DuplicateData:
            return "DUPLICATE_DATA";
        case ErrorCode::OperationFailed:
            return "OPERATION_FAILED";
        case ErrorCode::ResourceNotFound:
            return "RESOURCE_NOT_FOUND";
        case ErrorCode::PermissionDenied:
            return "PERMISSION_DENIED";
        case ErrorCode::SystemError:
            return "SYSTEM_ERROR";
        case ErrorCode::MemoryError:
            return "MEMORY_ERROR";
        case ErrorCode::FileIOError:
            return "FILE_IO_ERROR";
        case ErrorCode::UnknownError:
        default:
            return "UNKNOWN_ERROR";
    }
}

QString ErrorHandler::getErrorLevelDescription(ErrorLevel level)
{
    switch (level) {
        case ErrorLevel::Info:
            return "INFO";
        case ErrorLevel::Warning:
            return "WARNING";
        case ErrorLevel::Error:
            return "ERROR";
        case ErrorLevel::Critical:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

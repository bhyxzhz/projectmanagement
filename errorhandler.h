#ifndef ERRORHANDLER_H
#define ERRORHANDLER_H

#include <QString>
#include <QDateTime>
#include <QDebug>

/**
 * @file errorhandler.h
 * @brief 统一错误处理工具类 - 针对千万级产品标准化要求
 * 
 * 功能：
 * 1. 统一错误处理接口
 * 2. 详细的错误信息记录（包含上下文、时间戳、错误代码）
 * 3. 错误日志记录
 * 4. 错误分类和级别管理
 * 
 * 设计原则：
 * - 所有错误通过统一的接口处理
 * - 错误信息包含足够的上下文信息，便于定位问题
 * - 支持错误日志记录，便于问题追踪
 */

/**
 * @brief 错误级别枚举
 */
enum class ErrorLevel {
    Info,       // 信息级别（非错误）
    Warning,    // 警告级别
    Error,      // 错误级别
    Critical    // 严重错误级别
};

/**
 * @brief 错误代码枚举
 */
enum class ErrorCode {
    // 数据库相关错误 (1000-1999)
    DatabaseConnectionFailed = 1001,
    DatabaseQueryFailed = 1002,
    DatabaseTransactionFailed = 1003,
    DatabaseRollbackFailed = 1004,
    
    // 数据验证错误 (2000-2999)
    ValidationFailed = 2001,
    InvalidDataFormat = 2002,
    DataOutOfRange = 2003,
    DuplicateData = 2004,
    
    // 业务逻辑错误 (3000-3999)
    OperationFailed = 3001,
    ResourceNotFound = 3002,
    PermissionDenied = 3003,
    
    // 系统错误 (4000-4999)
    SystemError = 4001,
    MemoryError = 4002,
    FileIOError = 4003,
    
    // 未知错误
    UnknownError = 9999
};

/**
 * @brief 错误信息结构
 */
struct ErrorInfo {
    ErrorCode code;              // 错误代码
    ErrorLevel level;            // 错误级别
    QString message;             // 错误消息
    QString context;              // 上下文信息（函数名、操作类型等）
    QString details;             // 详细信息（SQL错误、异常信息等）
    QDateTime timestamp;         // 时间戳
    
    ErrorInfo() : code(ErrorCode::UnknownError), level(ErrorLevel::Error), timestamp(QDateTime::currentDateTime()) {}
    
    /**
     * @brief 获取完整的错误描述
     */
    QString getFullDescription() const {
        QString desc = QString("[%1] %2")
                      .arg(timestamp.toString("yyyy-MM-dd hh:mm:ss"))
                      .arg(message);
        if (!context.isEmpty()) {
            desc += QString("\n上下文: %1").arg(context);
        }
        if (!details.isEmpty()) {
            desc += QString("\n详情: %1").arg(details);
        }
        return desc;
    }
    
    /**
     * @brief 获取简短的错误描述（用于用户提示）
     */
    QString getShortDescription() const {
        return message;
    }
};

/**
 * @brief 统一错误处理工具类
 */
class ErrorHandler
{
public:
    /**
     * @brief 记录错误
     * @param code 错误代码
     * @param level 错误级别
     * @param message 错误消息
     * @param context 上下文信息（函数名、操作类型等）
     * @param details 详细信息（可选）
     * @return 错误信息对象
     */
    static ErrorInfo recordError(ErrorCode code, 
                                 ErrorLevel level,
                                 const QString &message,
                                 const QString &context = "",
                                 const QString &details = "");
    
    /**
     * @brief 记录数据库错误
     * @param code 错误代码
     * @param message 错误消息
     * @param context 上下文信息
     * @param sqlError SQL错误信息（可选）
     * @return 错误信息对象
     */
    static ErrorInfo recordDatabaseError(ErrorCode code,
                                         const QString &message,
                                         const QString &context,
                                         const QString &sqlError = "");
    
    /**
     * @brief 记录验证错误
     * @param message 错误消息
     * @param context 上下文信息
     * @param fieldName 字段名称（可选）
     * @return 错误信息对象
     */
    static ErrorInfo recordValidationError(const QString &message,
                                           const QString &context,
                                           const QString &fieldName = "");
    
    /**
     * @brief 格式化错误信息（用于日志输出）
     * @param error 错误信息
     * @return 格式化后的字符串
     */
    static QString formatError(const ErrorInfo &error);
    
    /**
     * @brief 格式化错误信息（用于用户提示）
     * @param error 错误信息
     * @return 用户友好的错误提示
     */
    static QString formatUserMessage(const ErrorInfo &error);
    
    /**
     * @brief 获取错误代码的文本描述
     * @param code 错误代码
     * @return 错误代码描述
     */
    static QString getErrorCodeDescription(ErrorCode code);
    
    /**
     * @brief 获取错误级别的文本描述
     * @param level 错误级别
     * @return 错误级别描述
     */
    static QString getErrorLevelDescription(ErrorLevel level);
    
private:
    ErrorHandler() = default;
    ~ErrorHandler() = default;
};

#endif // ERRORHANDLER_H

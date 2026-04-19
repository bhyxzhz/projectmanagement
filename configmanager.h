#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

/**
 * @file configmanager.h
 * @brief 配置管理器 - 实现配置外部化，符合千万级产品标准化要求
 * 
 * 功能：
 * 1. 统一管理所有配置项（数据库路径、缓存大小、性能参数等）
 * 2. 支持从配置文件读取（QSettings INI格式）
 * 3. 支持环境变量覆盖
 * 4. 提供默认值（从appconstants.h）
 * 5. 支持运行时修改配置
 * 6. 自动保存配置到用户目录
 * 
 * 符合千万级产品标准化要求：
 * - 配置外部化，便于部署和维护
 * - 支持不同环境配置（开发/测试/生产）
 * - 支持性能调优（可根据数据量调整参数）
 * - 配置持久化，重启后保持设置
 */

/**
 * @brief 配置管理器（单例模式）
 * 
 * 管理所有应用程序配置，支持从配置文件、环境变量读取，
 * 并提供合理的默认值。
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     */
    static ConfigManager& getInstance();

    /**
     * @brief 初始化配置管理器
     * @return 是否初始化成功
     */
    bool initialize();

    /**
     * @brief 获取数据库文件路径
     */
    QString getDatabasePath() const;

    /**
     * @brief 设置数据库文件路径
     */
    void setDatabasePath(const QString& path);

    /**
     * @brief 获取分页大小
     */
    int getPageSize() const;

    /**
     * @brief 设置分页大小
     */
    void setPageSize(int size);

    /**
     * @brief 获取缓存大小
     */
    int getCacheSize() const;

    /**
     * @brief 设置缓存大小
     */
    void setCacheSize(int size);

    /**
     * @brief 获取缓存过期时间（毫秒）
     */
    int getCacheExpireMs() const;

    /**
     * @brief 设置缓存过期时间（毫秒）
     */
    void setCacheExpireMs(int ms);

    /**
     * @brief 获取数据库缓存大小（KB，负数表示KB）
     */
    int getDatabaseCacheSizeKB() const;

    /**
     * @brief 设置数据库缓存大小（KB）
     */
    void setDatabaseCacheSizeKB(int kb);

    /**
     * @brief 获取数据库内存映射大小（字节）
     */
    qint64 getDatabaseMmapSizeBytes() const;

    /**
     * @brief 设置数据库内存映射大小（字节）
     */
    void setDatabaseMmapSizeBytes(qint64 bytes);

    /**
     * @brief 获取数据库页面大小（字节）
     */
    int getDatabasePageSizeBytes() const;

    /**
     * @brief 设置数据库页面大小（字节）
     */
    void setDatabasePageSizeBytes(int bytes);

    /**
     * @brief 获取通用配置值
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值
     */
    QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const;

    /**
     * @brief 设置通用配置值
     * @param key 配置键
     * @param value 配置值
     */
    void setValue(const QString& key, const QVariant& value);

    /**
     * @brief 保存配置到文件
     */
    void save();

    /**
     * @brief 重新加载配置
     */
    void reload();

    /**
     * @brief 重置为默认值
     */
    void resetToDefaults();

signals:
    /**
     * @brief 配置值改变信号
     */
    void configChanged(const QString& key, const QVariant& value);

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief 从环境变量读取配置
     */
    void loadFromEnvironment();

    /**
     * @brief 从配置文件读取配置
     */
    void loadFromFile();

    /**
     * @brief 应用默认值
     */
    void applyDefaults();

    /**
     * @brief 获取配置文件路径
     */
    QString getConfigFilePath() const;

    QSettings* m_settings;
    QString m_configFilePath;
    static ConfigManager* instance;

    // 配置项缓存（提高访问性能）
    mutable QString m_cachedDatabasePath;
    mutable int m_cachedPageSize;
    mutable int m_cachedCacheSize;
    mutable int m_cachedCacheExpireMs;
    mutable bool m_cacheValid;
};

#endif // CONFIGMANAGER_H

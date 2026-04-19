#include "configmanager.h"
#include "appconstants.h"
#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

ConfigManager* ConfigManager::instance = nullptr;

ConfigManager& ConfigManager::getInstance()
{
    if (!instance) {
        instance = new ConfigManager();
    }
    return *instance;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr)
    , m_cachedPageSize(-1)
    , m_cachedCacheSize(-1)
    , m_cachedCacheExpireMs(-1)
    , m_cacheValid(false)
{
}

ConfigManager::~ConfigManager()
{
    if (m_settings) {
        save();
        delete m_settings;
    }
}

bool ConfigManager::initialize()
{
    // 获取配置文件路径
    m_configFilePath = getConfigFilePath();

    // 确保配置目录存在
    QFileInfo configFileInfo(m_configFilePath);
    QDir configDir = configFileInfo.absoluteDir();
    if (!configDir.exists()) {
        if (!configDir.mkpath(".")) {
            qDebug() << "警告：无法创建配置目录:" << configDir.absolutePath();
        }
    }

    // 初始化QSettings
    // 注意：Qt 6中setIniCodec已被移除，QSettings默认使用UTF-8编码
    m_settings = new QSettings(m_configFilePath, QSettings::IniFormat, this);

    // 加载配置：1) 应用默认值 2) 从文件加载 3) 从环境变量覆盖
    applyDefaults();
    loadFromFile();
    loadFromEnvironment();

    // 标记缓存无效，强制重新读取
    m_cacheValid = false;

    qDebug() << "配置管理器初始化完成，配置文件:" << m_configFilePath;
    return true;
}

QString ConfigManager::getConfigFilePath() const
{
    // 优先使用用户配置目录
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        // 如果无法获取用户配置目录，使用应用程序目录
        configDir = QCoreApplication::applicationDirPath();
    }

    // 确保目录存在
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return dir.absoluteFilePath("projectmanagement.ini");
}

void ConfigManager::applyDefaults()
{
    // 应用默认值（从appconstants.h）
    if (!m_settings->contains("Database/Path")) {
        // 默认数据库路径：用户数据目录
        QString defaultDbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (defaultDbPath.isEmpty()) {
            defaultDbPath = QCoreApplication::applicationDirPath();
        }
        QDir dbDir(defaultDbPath);
        if (!dbDir.exists()) {
            dbDir.mkpath(".");
        }
        m_settings->setValue("Database/Path", dbDir.absoluteFilePath("projects.db"));
    }

    if (!m_settings->contains("Pagination/PageSize")) {
        m_settings->setValue("Pagination/PageSize", PaginationConstants::DEFAULT_PAGE_SIZE);
    }

    if (!m_settings->contains("Pagination/CacheSize")) {
        m_settings->setValue("Pagination/CacheSize", PaginationConstants::DEFAULT_CACHE_SIZE);
    }

    if (!m_settings->contains("Database/CacheExpireMs")) {
        m_settings->setValue("Database/CacheExpireMs", DatabaseConstants::CACHE_EXPIRE_MS);
    }

    if (!m_settings->contains("Database/CacheSizeKB")) {
        m_settings->setValue("Database/CacheSizeKB", DatabaseOptimizationConstants::CACHE_SIZE_KB);
    }

    if (!m_settings->contains("Database/MmapSizeBytes")) {
        m_settings->setValue("Database/MmapSizeBytes", DatabaseOptimizationConstants::MMAP_SIZE_BYTES);
    }

    if (!m_settings->contains("Database/PageSizeBytes")) {
        m_settings->setValue("Database/PageSizeBytes", DatabaseOptimizationConstants::PAGE_SIZE_BYTES);
    }
}

void ConfigManager::loadFromFile()
{
    // QSettings会自动从文件读取
    // 这里可以添加额外的文件读取逻辑（如JSON格式）
    m_settings->sync();
}

void ConfigManager::loadFromEnvironment()
{
    // 从环境变量读取配置（环境变量优先级最高）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 数据库路径
    if (env.contains("PM_DATABASE_PATH")) {
        QString dbPath = env.value("PM_DATABASE_PATH");
        m_settings->setValue("Database/Path", dbPath);
        qDebug() << "从环境变量读取数据库路径:" << dbPath;
    }

    // 分页大小
    if (env.contains("PM_PAGE_SIZE")) {
        bool ok;
        int pageSize = env.value("PM_PAGE_SIZE").toInt(&ok);
        if (ok && pageSize > 0) {
            m_settings->setValue("Pagination/PageSize", pageSize);
            qDebug() << "从环境变量读取分页大小:" << pageSize;
        }
    }

    // 缓存大小
    if (env.contains("PM_CACHE_SIZE")) {
        bool ok;
        int cacheSize = env.value("PM_CACHE_SIZE").toInt(&ok);
        if (ok && cacheSize > 0) {
            m_settings->setValue("Pagination/CacheSize", cacheSize);
            qDebug() << "从环境变量读取缓存大小:" << cacheSize;
        }
    }

    // 缓存过期时间
    if (env.contains("PM_CACHE_EXPIRE_MS")) {
        bool ok;
        int expireMs = env.value("PM_CACHE_EXPIRE_MS").toInt(&ok);
        if (ok && expireMs > 0) {
            m_settings->setValue("Database/CacheExpireMs", expireMs);
            qDebug() << "从环境变量读取缓存过期时间:" << expireMs << "ms";
        }
    }
}

QString ConfigManager::getDatabasePath() const
{
    if (!m_cacheValid || m_cachedDatabasePath.isEmpty()) {
        m_cachedDatabasePath = m_settings->value("Database/Path", "").toString();
        if (m_cachedDatabasePath.isEmpty()) {
            // 如果配置中没有，使用默认路径
            QString defaultDbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (defaultDbPath.isEmpty()) {
                defaultDbPath = QCoreApplication::applicationDirPath();
            }
            QDir dbDir(defaultDbPath);
            if (!dbDir.exists()) {
                dbDir.mkpath(".");
            }
            m_cachedDatabasePath = dbDir.absoluteFilePath("projects.db");
        }
        m_cacheValid = true;
    }
    return m_cachedDatabasePath;
}

void ConfigManager::setDatabasePath(const QString& path)
{
    m_settings->setValue("Database/Path", path);
    m_cachedDatabasePath = path;
    emit configChanged("Database/Path", path);
}

int ConfigManager::getPageSize() const
{
    if (!m_cacheValid || m_cachedPageSize < 0) {
        m_cachedPageSize = m_settings->value("Pagination/PageSize", PaginationConstants::DEFAULT_PAGE_SIZE).toInt();
        m_cacheValid = true;
    }
    return m_cachedPageSize;
}

void ConfigManager::setPageSize(int size)
{
    int clampedSize = qBound(PaginationConstants::MIN_PAGE_SIZE, size, PaginationConstants::MAX_PAGE_SIZE);
    m_settings->setValue("Pagination/PageSize", clampedSize);
    m_cachedPageSize = clampedSize;
    emit configChanged("Pagination/PageSize", clampedSize);
}

int ConfigManager::getCacheSize() const
{
    if (!m_cacheValid || m_cachedCacheSize < 0) {
        m_cachedCacheSize = m_settings->value("Pagination/CacheSize", PaginationConstants::DEFAULT_CACHE_SIZE).toInt();
        m_cacheValid = true;
    }
    return m_cachedCacheSize;
}

void ConfigManager::setCacheSize(int size)
{
    m_settings->setValue("Pagination/CacheSize", size);
    m_cachedCacheSize = size;
    emit configChanged("Pagination/CacheSize", size);
}

int ConfigManager::getCacheExpireMs() const
{
    if (!m_cacheValid || m_cachedCacheExpireMs < 0) {
        m_cachedCacheExpireMs = m_settings->value("Database/CacheExpireMs", DatabaseConstants::CACHE_EXPIRE_MS).toInt();
        m_cacheValid = true;
    }
    return m_cachedCacheExpireMs;
}

void ConfigManager::setCacheExpireMs(int ms)
{
    m_settings->setValue("Database/CacheExpireMs", ms);
    m_cachedCacheExpireMs = ms;
    emit configChanged("Database/CacheExpireMs", ms);
}

int ConfigManager::getDatabaseCacheSizeKB() const
{
    return m_settings->value("Database/CacheSizeKB", DatabaseOptimizationConstants::CACHE_SIZE_KB).toInt();
}

void ConfigManager::setDatabaseCacheSizeKB(int kb)
{
    m_settings->setValue("Database/CacheSizeKB", kb);
    emit configChanged("Database/CacheSizeKB", kb);
}

qint64 ConfigManager::getDatabaseMmapSizeBytes() const
{
    return m_settings->value("Database/MmapSizeBytes", DatabaseOptimizationConstants::MMAP_SIZE_BYTES).toLongLong();
}

void ConfigManager::setDatabaseMmapSizeBytes(qint64 bytes)
{
    m_settings->setValue("Database/MmapSizeBytes", bytes);
    emit configChanged("Database/MmapSizeBytes", bytes);
}

int ConfigManager::getDatabasePageSizeBytes() const
{
    return m_settings->value("Database/PageSizeBytes", DatabaseOptimizationConstants::PAGE_SIZE_BYTES).toInt();
}

void ConfigManager::setDatabasePageSizeBytes(int bytes)
{
    m_settings->setValue("Database/PageSizeBytes", bytes);
    emit configChanged("Database/PageSizeBytes", bytes);
}

QVariant ConfigManager::getValue(const QString& key, const QVariant& defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void ConfigManager::setValue(const QString& key, const QVariant& value)
{
    m_settings->setValue(key, value);
    emit configChanged(key, value);
}

void ConfigManager::save()
{
    if (m_settings) {
        m_settings->sync();
        qDebug() << "配置已保存到:" << m_configFilePath;
    }
}

void ConfigManager::reload()
{
    if (m_settings) {
        m_settings->sync();
        m_cacheValid = false;  // 使缓存失效
        qDebug() << "配置已重新加载";
    }
}

void ConfigManager::resetToDefaults()
{
    if (m_settings) {
        m_settings->clear();
        applyDefaults();
        m_cacheValid = false;
        save();
        qDebug() << "配置已重置为默认值";
    }
}

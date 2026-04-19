#include "databasemanager.h"
#include "appconstants.h"
#include "errorhandler.h"
#include "configmanager.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QRandomGenerator>
#include <QDate>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QTextStream>
#include <QStringConverter>
#include <QThread>
#include <QElapsedTimer>
#include <QCryptographicHash>

DatabaseManager* DatabaseManager::instance = nullptr;

DatabaseManager& DatabaseManager::getInstance()
{
    if (!instance) {
        instance = new DatabaseManager();
    }
    return *instance;
}

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
    , m_maintenanceThread(nullptr)
    , m_initialized(false)
{
    // 关键优化：不在构造函数中初始化数据库，改为懒加载
    // 这样可以避免阻塞UI线程，让界面先显示出来
    // 数据库初始化将在第一次使用时或通过显式调用initializeDatabase()进行
}

DatabaseManager::~DatabaseManager()
{
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::initializeDatabase()
{
    // 如果已经初始化，直接返回
    if (m_initialized && db.isOpen()) {
        return true;
    }
    
    qDebug() << "开始初始化数据库...";
    QElapsedTimer initTimer;
    initTimer.start();
    
    // 设置数据库连接
    db = QSqlDatabase::addDatabase("QSQLITE");

    // 从配置管理器获取数据库路径（符合千万级产品标准化要求）
    ConfigManager& config = ConfigManager::getInstance();
    if (!config.initialize()) {
        qDebug() << "警告：配置管理器初始化失败，使用默认数据库路径";
    }
    QString dbPath = config.getDatabasePath();
    
    // 关键优化：确保数据库文件所在目录存在
    QFileInfo dbFileInfo(dbPath);
    QDir dbDir = dbFileInfo.absoluteDir();
    if (!dbDir.exists()) {
        qDebug() << "数据库目录不存在，尝试创建:" << dbDir.absolutePath();
        if (!dbDir.mkpath(".")) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseConnectionFailed,
                QString("无法创建数据库目录: %1").arg(dbDir.absolutePath()),
                "DatabaseManager::initializeDatabase()",
                "目录创建失败"
            );
            lastError = error.getShortDescription();
            qDebug() << "错误:" << lastError;
            return false;
        }
        qDebug() << "数据库目录创建成功:" << dbDir.absolutePath();
    }
    
    // 检查目录是否可写
    if (!QFileInfo(dbDir.absolutePath()).isWritable()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseConnectionFailed,
            QString("数据库目录不可写: %1").arg(dbDir.absolutePath()),
            "DatabaseManager::initializeDatabase()",
            "权限不足"
        );
        lastError = error.getShortDescription();
        qDebug() << "错误:" << lastError;
        return false;
    }
    
    db.setDatabaseName(dbPath);
    qDebug() << "数据库路径（从配置读取）:" << dbPath;
    qDebug() << "数据库目录:" << dbDir.absolutePath();
    qDebug() << "数据库文件是否存在:" << dbFileInfo.exists();

    // 关键优化：打开数据库前先设置PRAGMA，可能加快打开速度
    // 注意：必须在open()之前设置，但QSqlDatabase可能不支持，所以我们在open()后立即设置
    
    // 添加重试机制，避免临时锁定问题
    int retryCount = 0;
    const int maxRetries = 3;
    bool dbOpened = false;
    
    while (retryCount < maxRetries && !dbOpened) {
        dbOpened = db.open();
        if (!dbOpened) {
            retryCount++;
            if (retryCount < maxRetries) {
                qDebug() << "数据库打开失败，重试" << retryCount << "/" << maxRetries 
                         << "错误:" << db.lastError().text();
                QThread::msleep(100 * retryCount);  // 递增延迟
                // 重新设置数据库连接（某些情况下可能需要）
                db = QSqlDatabase::addDatabase("QSQLITE");
                db.setDatabaseName(dbPath);
            }
        }
    }
    
    if (!dbOpened) {
        QString errorDetails = QString("路径: %1, 错误: %2").arg(dbPath, db.lastError().text());
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseConnectionFailed,
            "无法打开数据库",
            "DatabaseManager::initializeDatabase()",
            errorDetails
        );
        lastError = error.getShortDescription();
        qDebug() << "数据库打开失败（重试" << maxRetries << "次后仍失败）:" << errorDetails;
        return false;
    }
    qDebug() << "数据库打开耗时:" << initTimer.elapsed() << "ms";

    // 设置主数据库连接的PRAGMA优化（包括busy_timeout，避免锁定错误）
    {
        QSqlQuery pragmaQ(db);
        pragmaQ.exec("PRAGMA journal_mode = WAL");
        pragmaQ.exec("PRAGMA synchronous = NORMAL");
        pragmaQ.exec("PRAGMA busy_timeout = 30000");  // 30秒超时，等待锁释放
        pragmaQ.exec("PRAGMA temp_store = MEMORY");
    }

    qint64 beforeTables = initTimer.elapsed();
    if (!createTables()) {
        return false;
    }
    qDebug() << "创建表耗时:" << (initTimer.elapsed() - beforeTables) << "ms";

    // 数据库初始化完成（UI不应被阻塞）
    // 重要：索引创建/PRAGMA优化在千万级数据场景可能非常耗时，放到后台线程执行，避免“未响应”
    // 关键优化：大幅延迟启动后台维护线程，让UI先显示并可以使用
    // 索引创建对于千万级数据可能很耗时，延迟到用户可以使用界面后再执行
    // 延迟5秒，确保用户界面完全加载并可以使用后再执行索引创建
    const QString openedDbPath = db.databaseName();
    QTimer::singleShot(5000, this, [this, openedDbPath]() {
        m_maintenanceThread = QThread::create([openedDbPath]() {
        const QString connName = QString("pm_maint_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        {
            QSqlDatabase maintDb = QSqlDatabase::addDatabase("QSQLITE", connName);
            maintDb.setDatabaseName(openedDbPath);
            if (!maintDb.open()) {
                qDebug() << "后台维护：无法打开数据库:" << maintDb.lastError().text();
                return;
            }

            // 轻量PRAGMA（通常很快）：提升并发与读性能
            {
                QSqlQuery pragmaQ(maintDb);
                pragmaQ.exec("PRAGMA journal_mode = WAL");
                pragmaQ.exec("PRAGMA synchronous = NORMAL");
                pragmaQ.exec("PRAGMA temp_store = MEMORY");
            }

            // 索引创建（可能耗时）：仅后台执行，避免阻塞UI
            // 关键优化：先检查索引是否已存在，避免重复创建（对于千万级数据，检查比创建快得多）
            {
                QElapsedTimer indexTimer;
                indexTimer.start();
                
                QSqlQuery checkQuery(maintDb);
                checkQuery.prepare("SELECT name FROM sqlite_master WHERE type='index' AND name=?");
                
                QSqlQuery createQuery(maintDb);
                const QStringList indexQueries = {
                    "CREATE INDEX IF NOT EXISTS idx_projects_id ON projects(project_id)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_name ON projects(project_name)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_manager ON projects(manager)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_status ON projects(status)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_start_date ON projects(start_date)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_end_date ON projects(end_date)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_budget ON projects(budget)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_created_at_id ON projects(created_at DESC, project_id DESC)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_status_created ON projects(status, created_at DESC)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_manager_status ON projects(manager, status)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_date_range ON projects(start_date, end_date)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_cover_list ON projects(created_at DESC, project_id DESC, project_name, manager, status)",
                    "CREATE INDEX IF NOT EXISTS idx_projects_name_upper ON projects(UPPER(project_name))",
                    "CREATE INDEX IF NOT EXISTS idx_projects_manager_upper ON projects(UPPER(manager))"
                };
                
                const QStringList indexNames = {
                    "idx_projects_id",
                    "idx_projects_name",
                    "idx_projects_manager",
                    "idx_projects_status",
                    "idx_projects_start_date",
                    "idx_projects_end_date",
                    "idx_projects_budget",
                    "idx_projects_created_at_id",
                    "idx_projects_status_created",
                    "idx_projects_manager_status",
                    "idx_projects_date_range",
                    "idx_projects_cover_list",
                    "idx_projects_name_upper",
                    "idx_projects_manager_upper"
                };
                
                int createdCount = 0;
                int skippedCount = 0;
                
                for (int i = 0; i < indexQueries.size(); ++i) {
                    // 检查索引是否已存在
                    checkQuery.bindValue(0, indexNames[i]);
                    bool indexExists = false;
                    if (checkQuery.exec() && checkQuery.next()) {
                        indexExists = true;
                    }
                    checkQuery.finish();
                    
                    if (indexExists) {
                        skippedCount++;
                        qDebug() << "后台维护：索引已存在，跳过创建:" << indexNames[i];
                    } else {
                        // 索引不存在，创建它
                        if (createQuery.exec(indexQueries[i])) {
                            createdCount++;
                            qDebug() << "后台维护：索引创建成功:" << indexNames[i];
                        } else {
                            qDebug() << "后台维护：索引创建失败:" << createQuery.lastError().text() << "SQL:" << indexQueries[i];
                        }
                        createQuery.finish();
                    }
                }
                
                qint64 indexTime = indexTimer.elapsed();
                qDebug() << "后台维护：索引创建完成，创建:" << createdCount << "个，跳过:" << skippedCount << "个，总耗时:" << indexTime << "ms";
            }

            // 轻量优化（可能因SQLite版本不同而失败）：失败不影响运行
            {
                QSqlQuery optQ(maintDb);
                optQ.exec("PRAGMA optimize");
                optQ.exec("ANALYZE");
            }

            maintDb.close();
        }
        QSqlDatabase::removeDatabase(connName);
        qDebug() << "后台维护：索引/优化任务完成";
    });
    QObject::connect(m_maintenanceThread, &QThread::finished, m_maintenanceThread, &QObject::deleteLater);
    QObject::connect(m_maintenanceThread, &QThread::finished, this, [this]() {
        m_maintenanceThread = nullptr;  // 线程完成后清空指针
    });
    m_maintenanceThread->start();
    });
    
    // 注意：后台维护线程延迟500ms启动，让UI先显示

    m_initialized = true;
    return true;
}

void DatabaseManager::waitForMaintenanceThread(int timeoutMs)
{
    if (m_maintenanceThread && m_maintenanceThread->isRunning()) {
        qDebug() << "等待后台维护线程完成（最多" << timeoutMs << "毫秒）...";
        if (m_maintenanceThread->wait(timeoutMs)) {
            qDebug() << "后台维护线程已完成";
        } else {
            qDebug() << "警告：等待后台维护线程超时（" << timeoutMs << "毫秒）";
        }
    }
}

bool DatabaseManager::isConnected() const
{
    // 如果未初始化，尝试初始化（懒加载）
    if (!m_initialized) {
        const_cast<DatabaseManager*>(this)->initializeDatabase();
    }
    return db.isOpen();
}

bool DatabaseManager::createTables()
{
    QString createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS projects (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            project_id TEXT UNIQUE NOT NULL,
            project_name TEXT NOT NULL,
            manager TEXT NOT NULL,
            start_date TEXT NOT NULL,
            end_date TEXT NOT NULL,
            budget REAL NOT NULL,
            status TEXT NOT NULL,
            description TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";

    QSqlQuery query(db);
    if (!query.exec(createTableSQL)) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "创建数据库表失败",
            "DatabaseManager::createTables()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 创建系统日志表
    QString createLogTableSQL = R"(
        CREATE TABLE IF NOT EXISTS system_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            operation_type TEXT NOT NULL,
            operation_content TEXT NOT NULL,
            project_id TEXT,
            result TEXT NOT NULL
        )
    )";

    if (!query.exec(createLogTableSQL)) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "创建系统日志表失败",
            "DatabaseManager::createTables()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 为系统日志表创建索引，提高查询性能
    QString createLogIndexSQL = R"(
        CREATE INDEX IF NOT EXISTS idx_system_logs_timestamp ON system_logs(timestamp DESC)
    )";

    query.exec(createLogIndexSQL);

    // 创建用户表
    QString createUserTableSQL = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            role INTEGER NOT NULL DEFAULT 1,
            enabled INTEGER NOT NULL DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            last_login DATETIME
        )
    )";

    if (!query.exec(createUserTableSQL)) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "创建用户表失败",
            "DatabaseManager::createTables()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 为用户表创建索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)");

    // 初始化默认用户（如果不存在）
    QSqlQuery checkUserQuery(db);
    checkUserQuery.prepare("SELECT COUNT(*) FROM users");
    if (checkUserQuery.exec() && checkUserQuery.next()) {
        int userCount = checkUserQuery.value(0).toInt();
        if (userCount == 0) {
            // 创建默认管理员和普通用户
            QCryptographicHash adminHash(QCryptographicHash::Sha256);
            adminHash.addData(QString("admin123").toUtf8());
            QString adminPasswordHash = adminHash.result().toHex();

            QCryptographicHash userHash(QCryptographicHash::Sha256);
            userHash.addData(QString("user123").toUtf8());
            QString userPasswordHash = userHash.result().toHex();

            QSqlQuery insertUserQuery(db);
            insertUserQuery.prepare("INSERT INTO users (username, password_hash, role, enabled) VALUES (?, ?, ?, ?)");
            
            // 插入管理员
            insertUserQuery.bindValue(0, "admin");
            insertUserQuery.bindValue(1, adminPasswordHash);
            insertUserQuery.bindValue(2, 0);  // 0=管理员
            insertUserQuery.bindValue(3, 1);  // 1=启用
            insertUserQuery.exec();
            
            // 插入普通用户
            insertUserQuery.bindValue(0, "user");
            insertUserQuery.bindValue(1, userPasswordHash);
            insertUserQuery.bindValue(2, 1);  // 1=普通用户
            insertUserQuery.bindValue(3, 1);  // 1=启用
            insertUserQuery.exec();
            
            qDebug() << "默认用户创建完成: admin/admin123 (管理员), user/user123 (普通用户)";
        }
    }

    // 关键优化：延迟数据库迁移和FTS表创建，避免启动阻塞
    // 这些操作在后台异步执行，不影响启动速度
    // 延迟10秒，确保用户界面完全加载并可以使用后再执行这些耗时操作
    // 重要：保存数据库路径，延迟任务中使用独立连接，避免主连接被关闭时出错
    const QString dbPath = db.databaseName();
    QTimer::singleShot(10000, this, [this, dbPath]() {
        // 检查主连接是否打开，如果关闭则使用独立连接
        if (!db.isOpen()) {
            qDebug() << "主数据库连接已关闭，使用独立连接执行延迟任务";
            // 使用独立连接执行迁移和FTS创建
            const QString connName = QString("pm_delayed_%1").arg(QDateTime::currentMSecsSinceEpoch());
            QSqlDatabase delayedDb = QSqlDatabase::addDatabase("QSQLITE", connName);
            delayedDb.setDatabaseName(dbPath);
            
            if (delayedDb.open()) {
                // 使用独立连接执行迁移
                qDebug() << "开始数据库迁移（使用独立连接）...";
                QElapsedTimer migrationTimer;
                migrationTimer.start();
                
                QSqlQuery query(delayedDb);
                query.prepare("PRAGMA table_info(projects)");
                if (query.exec()) {
                    bool hasCreatedAt = false;
                    bool hasUpdatedAt = false;
                    while (query.next()) {
                        QString columnName = query.value(1).toString();
                        if (columnName == "created_at") hasCreatedAt = true;
                        else if (columnName == "updated_at") hasUpdatedAt = true;
                    }
                    if (!hasCreatedAt) {
                        query.exec("ALTER TABLE projects ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP");
                    }
                    if (!hasUpdatedAt) {
                        query.exec("ALTER TABLE projects ADD COLUMN updated_at DATETIME DEFAULT CURRENT_TIMESTAMP");
                    }
                }
                
                // 检查users表是否存在enabled字段
                query.prepare("PRAGMA table_info(users)");
                if (query.exec()) {
                    bool hasEnabled = false;
                    while (query.next()) {
                        QString columnName = query.value(1).toString();
                        if (columnName == "enabled") {
                            hasEnabled = true;
                            break;
                        }
                    }
                    if (!hasEnabled) {
                        qDebug() << "添加 users.enabled 字段（延迟任务）";
                        query.exec("ALTER TABLE users ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1");
                        query.exec("UPDATE users SET enabled = 1 WHERE enabled IS NULL");
                    }
                }
                
                qDebug() << "数据库迁移完成，耗时:" << migrationTimer.elapsed() << "ms";
                
                // 使用独立连接创建FTS表
                qDebug() << "开始创建FTS表（使用独立连接）...";
                QElapsedTimer ftsTimer;
                ftsTimer.start();
                
                // 检查FTS表是否存在
                QSqlQuery checkQuery(delayedDb);
                checkQuery.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='projects_fts'");
                if (!checkQuery.exec() || !checkQuery.next()) {
                    // FTS表不存在，创建它
                    QString ftsSql = R"(
                        CREATE VIRTUAL TABLE IF NOT EXISTS projects_fts USING fts5(
                            project_id, project_name, manager, status, description,
                            content='projects', content_rowid='id'
                        )
                    )";
                    if (query.exec(ftsSql)) {
                        // 创建触发器
                        QStringList triggerSql = {
                            R"(CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ai AFTER INSERT ON projects BEGIN INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description) VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description); END)",
                            R"(CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ad AFTER DELETE ON projects BEGIN DELETE FROM projects_fts WHERE rowid = old.id; END)",
                            R"(CREATE TRIGGER IF NOT EXISTS trg_projects_fts_au AFTER UPDATE ON projects BEGIN DELETE FROM projects_fts WHERE rowid = old.id; INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description) VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description); END)"
                        };
                        for (const QString &sql : triggerSql) {
                            query.exec(sql);
                        }
                        qDebug() << "FTS表创建完成（使用独立连接），耗时:" << ftsTimer.elapsed() << "ms";
                    }
                } else {
                    qDebug() << "FTS表已存在，跳过创建";
                }
                
                delayedDb.close();
                QSqlDatabase::removeDatabase(connName);
            } else {
                qDebug() << "无法打开独立连接执行延迟任务:" << delayedDb.lastError().text();
            }
        } else {
            // 主连接仍然打开，直接使用
            qDebug() << "开始数据库迁移...";
            QElapsedTimer migrationTimer;
            migrationTimer.start();
            migrateDatabase();
            qDebug() << "数据库迁移完成，耗时:" << migrationTimer.elapsed() << "ms";
            
            qDebug() << "开始创建FTS表...";
            QElapsedTimer ftsTimer;
            ftsTimer.start();
            createFTSTable();
            qDebug() << "FTS表创建完成（延迟执行），耗时:" << ftsTimer.elapsed() << "ms";
        }
    });

    return true;
}

void DatabaseManager::migrateDatabase()
{
    QSqlQuery query(db);

    // 检查是否存在 created_at 字段
    query.prepare("PRAGMA table_info(projects)");
    if (query.exec()) {
        bool hasCreatedAt = false;
        bool hasUpdatedAt = false;

        while (query.next()) {
            QString columnName = query.value(1).toString();
            if (columnName == "created_at") {
                hasCreatedAt = true;
            } else if (columnName == "updated_at") {
                hasUpdatedAt = true;
            }
        }

        // 添加缺失的字段
        if (!hasCreatedAt) {
            qDebug() << "添加 created_at 字段";
            query.exec("ALTER TABLE projects ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP");
        }

        if (!hasUpdatedAt) {
            qDebug() << "添加 updated_at 字段";
            query.exec("ALTER TABLE projects ADD COLUMN updated_at DATETIME DEFAULT CURRENT_TIMESTAMP");
        }
    }

    // 检查users表是否存在enabled字段
    query.prepare("PRAGMA table_info(users)");
    if (query.exec()) {
        bool hasEnabled = false;
        while (query.next()) {
            QString columnName = query.value(1).toString();
            if (columnName == "enabled") {
                hasEnabled = true;
                break;
            }
        }

        // 添加enabled字段（如果不存在）
        if (!hasEnabled) {
            qDebug() << "添加 users.enabled 字段";
            query.exec("ALTER TABLE users ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1");
            // 更新现有用户为启用状态
            query.exec("UPDATE users SET enabled = 1 WHERE enabled IS NULL");
        }
    }
}


bool DatabaseManager::addProject(const QStringList& projectData)
{
    if (projectData.size() < 8) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            "项目数据不完整，需要8个字段",
            "DatabaseManager::addProject()",
            "projectData"
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 关键优化：使用事务确保原子性，提高性能
    QSqlQuery beginQuery(db);
    if (!beginQuery.exec("BEGIN IMMEDIATE")) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "无法开始事务",
            "DatabaseManager::addProject()",
            beginQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 优化：使用索引快速检查项目ID是否已存在（project_id有唯一索引）
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT id FROM projects WHERE project_id = ? LIMIT 1");
    checkQuery.bindValue(0, projectData[0]);
    checkQuery.setForwardOnly(true);

    if (checkQuery.exec() && checkQuery.next()) {
        // 项目ID已存在，回滚事务
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::DuplicateData,
            ErrorLevel::Warning,
            QString("项目ID已存在: %1").arg(projectData[0]),
            "DatabaseManager::addProject()",
            QString("项目ID: %1").arg(projectData[0])
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 插入主表（使用本地时间）
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT INTO projects (project_id, project_name, manager, start_date, end_date, budget, status, description, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    for (int i = 0; i < 8; ++i) {
        query.bindValue(i, projectData[i]);
    }
    query.bindValue(8, currentTime);  // created_at
    query.bindValue(9, currentTime);  // updated_at

    if (!query.exec()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "添加项目失败",
            "DatabaseManager::addProject()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }
    
    // 关键优化：FTS表通过触发器自动更新，无需手动插入
    // 触发器 trg_projects_fts_ai 会在INSERT后自动更新FTS表
    // 这样可以避免手动插入FTS表，提高性能
    // 注意：如果触发器不存在，FTS搜索可能不包含新记录，但不影响主表操作

    // 提交事务
    if (!db.commit()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseTransactionFailed,
            "提交事务失败",
            "DatabaseManager::addProject()",
            db.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    return true;
}

bool DatabaseManager::updateProject(int row, const QStringList& projectData)
{
    if (projectData.size() < 8) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            "项目数据不完整，需要8个字段",
            "DatabaseManager::updateProject()",
            "projectData"
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 关键优化：使用事务确保原子性
    QSqlQuery beginQuery(db);
    if (!beginQuery.exec("BEGIN IMMEDIATE")) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "无法开始事务",
            "DatabaseManager::updateProject()",
            beginQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 优化：使用索引快速获取项目ID（使用LIMIT 1 OFFSET，但需要确保有索引支持）
    QSqlQuery selectQuery(db);
    selectQuery.prepare("SELECT project_id FROM projects LIMIT 1 OFFSET ?");
    selectQuery.setForwardOnly(true);  // 性能优化
    selectQuery.bindValue(0, row);

    if (!selectQuery.exec() || !selectQuery.next()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::ResourceNotFound,
            ErrorLevel::Warning,
            QString("找不到要更新的项目（行号: %1）").arg(row),
            "DatabaseManager::updateProject()",
            QString("行号: %1").arg(row)
        );
        lastError = error.getShortDescription();
        return false;
    }

    QString projectId = selectQuery.value(0).toString();

    // 优化：直接使用project_id更新（有索引支持，比使用row快）
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"(
        UPDATE projects
        SET project_name = ?, manager = ?, start_date = ?, end_date = ?,
            budget = ?, status = ?, description = ?, updated_at = ?
        WHERE project_id = ?
    )");

    for (int i = 1; i < 8; ++i) {
        updateQuery.bindValue(i - 1, projectData[i]);
    }
    updateQuery.bindValue(7, currentTime);  // updated_at
    updateQuery.bindValue(8, projectId);

    if (!updateQuery.exec()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("更新项目失败（行号: %1）").arg(row),
            "DatabaseManager::updateProject()",
            updateQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 提交事务
    if (!db.commit()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseTransactionFailed,
            "提交事务失败",
            "DatabaseManager::updateProject()",
            db.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    return true;
}

bool DatabaseManager::deleteProject(int row)
{
    QSqlQuery selectQuery(db);
    selectQuery.prepare("SELECT project_id FROM projects LIMIT 1 OFFSET ?");
    selectQuery.setForwardOnly(true);  // 性能优化
    selectQuery.bindValue(0, row);

    if (!selectQuery.exec() || !selectQuery.next()) {
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::ResourceNotFound,
            ErrorLevel::Warning,
            QString("找不到要删除的项目（行号: %1）").arg(row),
            "DatabaseManager::deleteProject()",
            QString("行号: %1").arg(row)
        );
        lastError = error.getShortDescription();
        return false;
    }

    QString projectId = selectQuery.value(0).toString();

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare("DELETE FROM projects WHERE project_id = ?");
    deleteQuery.bindValue(0, projectId);

    if (!deleteQuery.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("删除项目失败（行号: %1）").arg(row),
            "DatabaseManager::deleteProject()",
            deleteQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    return true;
}

bool DatabaseManager::updateProjectById(const QString& projectId, const QStringList& projectData)
{
    if (projectData.size() < 8) {
        ErrorInfo error = ErrorHandler::recordValidationError(
            "项目数据不完整，需要8个字段",
            "DatabaseManager::updateProjectById()",
            "projectData"
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 关键优化：使用事务确保原子性
    QSqlQuery beginQuery(db);
    if (!beginQuery.exec("BEGIN IMMEDIATE")) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "无法开始事务",
            "DatabaseManager::updateProjectById()",
            beginQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 优化：直接使用project_id更新（有唯一索引支持，非常快）
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"(
        UPDATE projects
        SET project_name = ?, manager = ?, start_date = ?, end_date = ?,
            budget = ?, status = ?, description = ?, updated_at = ?
        WHERE project_id = ?
    )");

    // 绑定更新数据（跳过项目ID，因为它是WHERE条件）
    for (int i = 1; i < 8; ++i) {
        updateQuery.bindValue(i - 1, projectData[i]);
    }
    updateQuery.bindValue(7, currentTime);  // updated_at
    updateQuery.bindValue(8, projectId);

    if (!updateQuery.exec()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("更新项目失败（项目ID: %1）").arg(projectId),
            "DatabaseManager::updateProjectById()",
            updateQuery.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 检查是否真的更新了记录
    if (updateQuery.numRowsAffected() == 0) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::ResourceNotFound,
            ErrorLevel::Warning,
            QString("项目不存在或数据未变化（项目ID: %1）").arg(projectId),
            "DatabaseManager::updateProjectById()",
            QString("项目ID: %1").arg(projectId)
        );
        lastError = error.getShortDescription();
        return false;
    }
    
    // 关键优化：FTS表通过触发器自动更新，无需手动更新
    // 触发器 trg_projects_fts_au 会在UPDATE后自动更新FTS表
    // 这样可以避免手动更新FTS表，提高性能

    // 提交事务
    if (!db.commit()) {
        QSqlQuery rollbackQuery(db);
        rollbackQuery.exec("ROLLBACK");
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseTransactionFailed,
            "提交事务失败",
            "DatabaseManager::updateProjectById()",
            db.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    return true;
}

bool DatabaseManager::deleteProjectById(const QString& projectId)
{
    // 使用重试机制和BEGIN IMMEDIATE获得独占锁，避免database is locked错误
    int retryCount = 0;
    const int maxRetries = 10;  // 增加重试次数
    bool success = false;
    
    while (retryCount < maxRetries && !success) {
        // 使用BEGIN IMMEDIATE立即获得写锁
        QSqlQuery beginQuery(db);
        if (!beginQuery.exec("BEGIN IMMEDIATE")) {
            retryCount++;
            if (retryCount < maxRetries) {
                // 优化：递增延迟，最多等待2秒
                // 延迟时间：200ms, 400ms, 600ms, 800ms, 1000ms, 1200ms, 1400ms, 1600ms, 1800ms, 2000ms
                int delayMs = qMin(200 * retryCount, 2000);
                QThread::msleep(delayMs);
                qDebug() << "删除操作：事务开始失败，重试" << retryCount << "/" << maxRetries 
                         << "延迟" << delayMs << "ms，错误:" << beginQuery.lastError().text();
                continue;
            } else {
                ErrorInfo error = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseQueryFailed,
                    QString("删除项目失败（项目ID: %1，无法开始事务，重试%2次后仍失败）").arg(projectId).arg(maxRetries),
                    "DatabaseManager::deleteProjectById()",
                    beginQuery.lastError().text()
                );
                lastError = error.getShortDescription();
                return false;
            }
        }
        
        // 在删除前先获取ID，用于同步删除FTS表
        QSqlQuery selectQuery(db);
        selectQuery.prepare("SELECT id FROM projects WHERE project_id = ?");
        selectQuery.bindValue(0, projectId);
        
        qint64 rowId = -1;
        if (selectQuery.exec() && selectQuery.next()) {
            rowId = selectQuery.value(0).toLongLong();
        }
        
        QSqlQuery deleteQuery(db);
        deleteQuery.prepare("DELETE FROM projects WHERE project_id = ?");
        deleteQuery.bindValue(0, projectId);

        if (deleteQuery.exec()) {
            // 检查是否真的删除了记录
            if (deleteQuery.numRowsAffected() == 0) {
                QSqlQuery rollbackQuery(db);
                rollbackQuery.exec("ROLLBACK");
                ErrorInfo error = ErrorHandler::recordError(
                    ErrorCode::ResourceNotFound,
                    ErrorLevel::Warning,
                    QString("项目不存在或已被删除（项目ID: %1）").arg(projectId),
                    "DatabaseManager::deleteProjectById()",
                    QString("项目ID: %1").arg(projectId)
                );
                lastError = error.getShortDescription();
                return false;
            }
            
            // 关键优化：FTS表通过触发器自动删除，无需手动删除
            // 触发器 trg_projects_fts_ad 会在DELETE后自动删除FTS表中的记录
            // 这样可以避免手动删除FTS表，提高性能
            
            // 提交事务（使用commit()方法，不是exec()）
            if (db.commit()) {
                success = true;
                qDebug() << "删除操作：成功删除项目" << projectId;
            } else {
                QSqlQuery rollbackQuery(db);
                rollbackQuery.exec("ROLLBACK");
                retryCount++;
                if (retryCount < maxRetries) {
                    int delayMs = qMin(200 * retryCount, 2000);
                    QThread::msleep(delayMs);
                    qDebug() << "删除操作：提交失败，重试" << retryCount << "/" << maxRetries 
                             << "延迟" << delayMs << "ms，错误:" << db.lastError().text();
                } else {
                    ErrorInfo error = ErrorHandler::recordDatabaseError(
                        ErrorCode::DatabaseQueryFailed,
                        QString("删除项目失败（项目ID: %1，提交失败，重试%2次后仍失败）").arg(projectId).arg(maxRetries),
                        "DatabaseManager::deleteProjectById()",
                        db.lastError().text()
                    );
                    lastError = error.getShortDescription();
                    return false;
                }
            }
        } else {
            QSqlQuery rollbackQuery(db);
            rollbackQuery.exec("ROLLBACK");
            retryCount++;
            if (retryCount < maxRetries) {
                int delayMs = qMin(200 * retryCount, 2000);
                QThread::msleep(delayMs);
                qDebug() << "删除操作：执行失败，重试" << retryCount << "/" << maxRetries 
                         << "延迟" << delayMs << "ms，错误:" << deleteQuery.lastError().text();
            } else {
                ErrorInfo error = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseQueryFailed,
                    QString("删除项目失败（项目ID: %1，执行失败，重试%2次后仍失败）").arg(projectId).arg(maxRetries),
                    "DatabaseManager::deleteProjectById()",
                    deleteQuery.lastError().text()
                );
                lastError = error.getShortDescription();
                return false;
            }
        }
    }
    
    if (!success) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("删除项目失败（项目ID: %1，重试%2次后仍失败）").arg(projectId).arg(maxRetries),
            "DatabaseManager::deleteProjectById()",
            "数据库锁定或事务失败"
        );
        lastError = error.getShortDescription();
        return false;
    }
    
    return true;
}

bool DatabaseManager::loadProjects(QStandardItemModel* model)
{
    if (!model) {
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::SystemError,
            ErrorLevel::Error,
            "模型指针为空",
            "DatabaseManager::loadProjects()",
            ""
        );
        lastError = error.getShortDescription();
        return false;
    }

    model->clear();
    model->setHorizontalHeaderLabels({
        "项目ID", "项目名称", "项目经理", "开始日期",
        "结束日期", "预算(万元)", "状态", "描述"
    });

    // 检查总记录数
    QSqlQuery countQuery("SELECT COUNT(*) FROM projects", db);
    int totalRecords = 0;
    if (countQuery.exec() && countQuery.next()) {
        totalRecords = countQuery.value(0).toInt();
    }

    // 如果记录数超过阈值，只加载前N条，并提示用户使用分页模式
    if (totalRecords > DatabaseConstants::LARGE_DATA_THRESHOLD) {
        qDebug() << "检测到大量数据(" << totalRecords << "条记录)，建议使用分页模式";

        QSqlQuery query(db);
        query.prepare(QString("SELECT project_id, project_name, manager, start_date, end_date, budget, status, description FROM projects ORDER BY created_at DESC, project_id DESC LIMIT %1")
                      .arg(DatabaseConstants::LARGE_DATA_THRESHOLD));
        query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存

        if (!query.exec()) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseQueryFailed,
                "加载项目数据失败",
                "DatabaseManager::loadProjects()",
                query.lastError().text()
            );
            lastError = error.getShortDescription();
            return false;
        }

        while (query.next()) {
            QList<QStandardItem*> rowItems;
            for (int i = 0; i < 8; ++i) {
                rowItems << new QStandardItem(query.value(i).toString());
            }
            model->appendRow(rowItems);
        }

        // 在模型末尾添加提示行（提醒用户使用分页模式）
        QList<QStandardItem*> tipRow;
        tipRow << new QStandardItem("提示")
               << new QStandardItem(QString("显示前%1条记录，共%2条记录")
                                    .arg(DatabaseConstants::LARGE_DATA_THRESHOLD)
                                    .arg(totalRecords))
               << new QStandardItem("建议使用分页模式查看所有数据")
               << new QStandardItem("")
               << new QStandardItem("")
               << new QStandardItem("")
               << new QStandardItem("")
               << new QStandardItem("");
        model->appendRow(tipRow);

        return true;
    }

    // 记录数较少时，正常加载所有数据
    QSqlQuery query(db);
    query.prepare("SELECT project_id, project_name, manager, start_date, end_date, budget, status, description FROM projects ORDER BY created_at DESC, project_id DESC");
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存

    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "加载项目数据失败（全部数据）",
            "DatabaseManager::loadProjects()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }

    while (query.next()) {
        QList<QStandardItem*> rowItems;
        for (int i = 0; i < 8; ++i) {
            rowItems << new QStandardItem(query.value(i).toString());
        }
        model->appendRow(rowItems);
    }

    return true;
}

QStandardItemModel* DatabaseManager::searchProjects(const QString& keyword, const QString& filterType)
{
    QStandardItemModel* searchModel = new QStandardItemModel();
    searchModel->setHorizontalHeaderLabels({
        "项目ID", "项目名称", "项目经理", "开始日期",
        "结束日期", "预算(万元)", "状态", "描述"
    });

    QString sql = "SELECT project_id, project_name, manager, start_date, end_date, budget, status, description FROM projects WHERE ";
    QStringList conditions;

    if (filterType == "all" || filterType == "name") {
        conditions << "project_name LIKE ?";
    }
    if (filterType == "all" || filterType == "manager") {
        conditions << "manager LIKE ?";
    }
    if (filterType == "all" || filterType == "status") {
        conditions << "status LIKE ?";
    }
    if (filterType == "all" || filterType == "id") {
        conditions << "project_id LIKE ?";
    }

    sql += conditions.join(" OR ");
    sql += " ORDER BY project_id";

    QSqlQuery query(db);
    query.prepare(sql);
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存

    QString searchPattern = "%" + keyword + "%";
    for (int i = 0; i < conditions.size(); ++i) {
        query.bindValue(i, searchPattern);
    }

    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("搜索项目失败（关键词: %1, 类型: %2）").arg(keyword, filterType),
            "DatabaseManager::searchProjects()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return searchModel;
    }

    while (query.next()) {
        QList<QStandardItem*> rowItems;
        for (int i = 0; i < 8; ++i) {
            rowItems << new QStandardItem(query.value(i).toString());
        }
        searchModel->appendRow(rowItems);
    }

    return searchModel;
}

int DatabaseManager::getProjectCount()
{
    QSqlQuery query(db);
    // 千万级数据优化：避免 COUNT(*) 扫描全表导致卡顿
    // 使用 MAX(id) 作为快速近似（id 为 INTEGER PRIMARY KEY AUTOINCREMENT 时近似总量）
    query.prepare("SELECT MAX(id) FROM projects");
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

double DatabaseManager::getTotalBudget()
{
    QSqlQuery query(db);
    query.prepare("SELECT SUM(budget) FROM projects");
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存
    if (query.exec() && query.next()) {
        return query.value(0).toDouble();
    }
    return 0.0;
}

QStringList DatabaseManager::getProjectStatusStats()
{
    QStringList stats;
    QSqlQuery query(db);
    query.prepare("SELECT status, COUNT(*) FROM projects GROUP BY status");
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存

    if (query.exec()) {
        while (query.next()) {
        QString status = query.value(0).toString();
        int count = query.value(1).toInt();
            stats << QString("%1: %2个").arg(status).arg(count);
        }
    }

    return stats;
}

/**
 * @brief 创建数据库索引
 * 
 * 索引优化策略（针对千万级数据）：
 * 1. 单列索引：为常用查询字段创建索引，提高WHERE条件查询速度
 * 2. 复合索引：为多字段组合查询创建索引，支持排序和过滤
 * 3. 覆盖索引：包含查询所需的所有字段，避免回表查询
 * 4. 函数索引：支持大小写不敏感搜索（需要SQLite 3.9.0+）
 * 
 * 性能影响：
 * - 查询速度：提升10-1000倍（取决于数据量）
 * - 插入速度：略微下降（需要维护索引）
 * - 存储空间：增加约20-30%
 */
bool DatabaseManager::createIndexes()
{
    QStringList indexQueries = {
        // 单列索引 - 基础查询优化（提高WHERE条件查询速度）
        "CREATE INDEX IF NOT EXISTS idx_projects_id ON projects(project_id)",
        "CREATE INDEX IF NOT EXISTS idx_projects_name ON projects(project_name)",
        "CREATE INDEX IF NOT EXISTS idx_projects_manager ON projects(manager)",
        "CREATE INDEX IF NOT EXISTS idx_projects_status ON projects(status)",
        "CREATE INDEX IF NOT EXISTS idx_projects_start_date ON projects(start_date)",
        "CREATE INDEX IF NOT EXISTS idx_projects_end_date ON projects(end_date)",
        "CREATE INDEX IF NOT EXISTS idx_projects_budget ON projects(budget)",
        
        // 复合索引 - 提高排序和过滤查询性能（千万级数据关键优化）
        // 支持 ORDER BY created_at DESC, project_id DESC 的快速排序
        "CREATE INDEX IF NOT EXISTS idx_projects_created_at_id ON projects(created_at DESC, project_id DESC)",
        // 支持按状态过滤并按创建时间排序
        "CREATE INDEX IF NOT EXISTS idx_projects_status_created ON projects(status, created_at DESC)",
        // 支持按经理和状态组合查询
        "CREATE INDEX IF NOT EXISTS idx_projects_manager_status ON projects(manager, status)",
        // 支持日期范围查询
        "CREATE INDEX IF NOT EXISTS idx_projects_date_range ON projects(start_date, end_date)",
        
        // 覆盖索引 - 减少回表查询（优化SELECT查询性能）
        // 包含查询所需的所有字段，查询时无需访问主表
        "CREATE INDEX IF NOT EXISTS idx_projects_cover_list ON projects(created_at DESC, project_id DESC, project_name, manager, status)",
        
        // 函数索引 - 用于大小写不敏感搜索（需要SQLite 3.9.0+）
        "CREATE INDEX IF NOT EXISTS idx_projects_name_upper ON projects(UPPER(project_name))",
        "CREATE INDEX IF NOT EXISTS idx_projects_manager_upper ON projects(UPPER(manager))"
    };

    QSqlQuery query(db);
    for (const QString &sql : indexQueries) {
        if (!query.exec(sql)) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseQueryFailed,
                "创建索引失败",
                "DatabaseManager::createIndexes()",
                query.lastError().text()
            );
            // 继续执行其他索引创建，不立即返回
            // 某些索引可能因为SQLite版本问题而失败（如函数索引需要较新版本）
            qDebug() << ErrorHandler::formatError(error);
        } else {
            qDebug() << "索引创建成功:" << sql;
        }
    }

    return true;
}

bool DatabaseManager::optimizeDatabase()
{
    if (!db.isOpen()) {
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::DatabaseConnectionFailed,
            ErrorLevel::Error,
            "数据库未连接",
            "DatabaseManager::optimizeDatabase()",
            ""
        );
        lastError = error.getShortDescription();
        return false;
    }

    QSqlQuery query(db);

    // 首先提交任何未完成的事务
    query.exec("COMMIT");

    /**
     * @brief 设置SQLite优化参数 - 针对千万级数据优化
     * 
     * 优化策略：
     * 1. WAL模式：提高并发读写性能
     * 2. 大缓存：减少磁盘I/O
     * 3. 内存映射：提高大数据量访问速度
     * 4. 多线程：利用多核CPU提高性能
     */
    // 从配置管理器读取优化参数（符合千万级产品标准化要求）
    ConfigManager& config = ConfigManager::getInstance();
    int cacheSizeKB = config.getDatabaseCacheSizeKB();
    qint64 mmapSizeBytes = config.getDatabaseMmapSizeBytes();
    int pageSizeBytes = config.getDatabasePageSizeBytes();

    QStringList optimizeQueries = {
        "PRAGMA journal_mode = WAL",           // 使用WAL模式提高并发性能
        "PRAGMA synchronous = NORMAL",         // 平衡性能和安全性
        QString("PRAGMA cache_size = %1").arg(cacheSizeKB),      // 从配置读取缓存大小
        "PRAGMA temp_store = MEMORY",          // 临时表存储在内存中
        QString("PRAGMA mmap_size = %1").arg(mmapSizeBytes),    // 从配置读取内存映射大小
        QString("PRAGMA page_size = %1").arg(pageSizeBytes),     // 从配置读取页面大小
        "PRAGMA auto_vacuum = INCREMENTAL",    // 增量VACUUM，减少数据库文件大小
        "PRAGMA optimize",                     // 自动优化查询计划（SQLite 3.32.0+）
        QString("PRAGMA threads = %1").arg(DatabaseOptimizationConstants::THREAD_COUNT)
    };

    for (const QString &sql : optimizeQueries) {
        if (!query.exec(sql)) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseQueryFailed,
                "数据库优化失败",
                "DatabaseManager::optimizeDatabase()",
                query.lastError().text()
            );
            // 继续执行其他优化，不立即返回
            qDebug() << ErrorHandler::formatError(error);
        } else {
            qDebug() << "数据库优化成功:" << sql;
        }
    }

    return true;
}

bool DatabaseManager::vacuumDatabase()
{
    QSqlQuery query(db);
    query.prepare("VACUUM");
    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "数据库压缩失败",
            "DatabaseManager::vacuumDatabase()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }
    return true;
}

bool DatabaseManager::analyzeDatabase()
{
    QSqlQuery query(db);
    query.prepare("ANALYZE");
    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "数据库分析失败",
            "DatabaseManager::analyzeDatabase()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }
    return true;
}

/**
 * @brief 批量插入项目数据
 *
 * 性能优化策略：
 * 1. 事务管理：使用事务将多个INSERT合并为一个原子操作，大幅提高性能
 * 2. 预编译语句：prepare()一次，执行多次，避免SQL解析开销
 * 3. 参数绑定：使用bindValue()防止SQL注入，同时提高性能
 *
 * 性能对比：
 * - 单条插入：每条记录都需要事务开销，性能差
 * - 批量插入：一次事务处理多条记录，性能提升10-100倍
 *
 * @param projectsData 项目数据列表
 * @param manageTransaction 是否管理事务（true=自动管理，false=外部管理）
 * @return 是否成功
 */
bool DatabaseManager::batchInsertProjects(const QList<QStringList>& projectsData, bool manageTransaction)
{
    if (projectsData.isEmpty()) {
        return true;
    }

    bool transactionStarted = false;
    if (manageTransaction) {
        if (!db.transaction()) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseTransactionFailed,
                "无法开始数据库事务",
                "DatabaseManager::batchInsertProjects()",
                db.lastError().text()
            );
            lastError = error.getShortDescription();
            return false;
        }
        transactionStarted = true;
    }

    // 性能优化：预编译语句，一次准备，多次执行
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery query(db);
    query.prepare(R"(
        INSERT INTO projects (project_id, project_name, manager, start_date, end_date, budget, status, description, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    // 批量插入：在同一个事务中执行所有插入操作
    for (const QStringList &projectData : projectsData) {
        if (projectData.size() >= 8) {
            // 绑定参数（使用预编译语句，提高性能并防止SQL注入）
            for (int i = 0; i < 8; ++i) {
                query.bindValue(i, projectData[i]);
            }
            query.bindValue(8, currentTime);  // created_at
            query.bindValue(9, currentTime);  // updated_at

            if (!query.exec()) {
                ErrorInfo error = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseQueryFailed,
                    QString("批量插入失败（第%1条记录）").arg(projectsData.indexOf(projectData) + 1),
                    "DatabaseManager::batchInsertProjects()",
                    query.lastError().text()
                );
                lastError = error.getShortDescription();
                if (transactionStarted) {
                    if (!db.rollback()) {
                        ErrorHandler::recordDatabaseError(
                            ErrorCode::DatabaseRollbackFailed,
                            "批量插入失败后回滚也失败",
                            "DatabaseManager::batchInsertProjects()",
                            db.lastError().text()
                        );
                    }
                }
                return false;
            }
        }
    }

    // 提交事务（如果由本函数管理）
    if (transactionStarted) {
        if (!db.commit()) {
            ErrorInfo commitError = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseTransactionFailed,
                "提交事务失败",
                "DatabaseManager::batchInsertProjects()",
                db.lastError().text()
            );
            lastError = commitError.getShortDescription();
            
            // 提交失败时回滚，保证数据一致性（千万级数据关键安全措施）
            if (!db.rollback()) {
                ErrorInfo rollbackError = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseRollbackFailed,
                    "提交失败后回滚也失败（严重错误）",
                    "DatabaseManager::batchInsertProjects()",
                    db.lastError().text()
                );
                lastError = rollbackError.getShortDescription();
            }
            return false;
        }
    }
    return true;
}

bool DatabaseManager::batchUpdateProjects(const QList<QPair<int, QStringList>>& updatesData)
{
    if (updatesData.isEmpty()) {
        return true;
    }

    db.transaction();

    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QSqlQuery query(db);
    query.prepare(R"(
        UPDATE projects
        SET project_name = ?, manager = ?, start_date = ?, end_date = ?,
            budget = ?, status = ?, description = ?, updated_at = ?
        WHERE id = ?
    )");

    for (const auto &update : updatesData) {
        int id = update.first;
        const QStringList &data = update.second;

        if (data.size() >= 7) {
            for (int i = 0; i < 7; ++i) {
                query.bindValue(i, data[i]);
            }
            query.bindValue(7, currentTime);  // updated_at
            query.bindValue(8, id);

            if (!query.exec()) {
                ErrorInfo error = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseQueryFailed,
                    QString("批量更新失败（ID: %1）").arg(id),
                    "DatabaseManager::batchUpdateProjects()",
                    query.lastError().text()
                );
                lastError = error.getShortDescription();
                if (!db.rollback()) {
                    ErrorInfo rollbackError = ErrorHandler::recordDatabaseError(
                        ErrorCode::DatabaseRollbackFailed,
                        "批量更新失败后回滚也失败（严重错误）",
                        "DatabaseManager::batchUpdateProjects()",
                        db.lastError().text()
                    );
                    lastError = rollbackError.getShortDescription();
                }
                return false;
            }
        }
    }

    // 提交事务，失败时回滚
    if (!db.commit()) {
        ErrorInfo commitError = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseTransactionFailed,
            "批量更新事务提交失败",
            "DatabaseManager::batchUpdateProjects()",
            db.lastError().text()
        );
        lastError = commitError.getShortDescription();
        if (!db.rollback()) {
            ErrorInfo rollbackError = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseRollbackFailed,
                "批量更新提交失败后回滚也失败（严重错误）",
                "DatabaseManager::batchUpdateProjects()",
                db.lastError().text()
            );
            lastError = rollbackError.getShortDescription();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::batchDeleteProjects(const QList<int>& rowIds)
{
    if (rowIds.isEmpty()) {
        return true;
    }

    db.transaction();

    QSqlQuery query(db);
    query.prepare("DELETE FROM projects WHERE id = ?");

    for (int id : rowIds) {
        query.bindValue(0, id);
        if (!query.exec()) {
            ErrorInfo error = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseQueryFailed,
                QString("批量删除失败（ID: %1）").arg(id),
                "DatabaseManager::batchDeleteProjects()",
                query.lastError().text()
            );
            lastError = error.getShortDescription();
            if (!db.rollback()) {
                ErrorInfo rollbackError = ErrorHandler::recordDatabaseError(
                    ErrorCode::DatabaseRollbackFailed,
                    "批量删除失败后回滚也失败（严重错误）",
                    "DatabaseManager::batchDeleteProjects()",
                    db.lastError().text()
                );
                lastError = rollbackError.getShortDescription();
            }
            return false;
        }
    }

    // 提交事务，失败时回滚
    if (!db.commit()) {
        ErrorInfo commitError = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseTransactionFailed,
            "批量删除事务提交失败",
            "DatabaseManager::batchDeleteProjects()",
            db.lastError().text()
        );
        lastError = commitError.getShortDescription();
        if (!db.rollback()) {
            ErrorInfo rollbackError = ErrorHandler::recordDatabaseError(
                ErrorCode::DatabaseRollbackFailed,
                "批量删除提交失败后回滚也失败（严重错误）",
                "DatabaseManager::batchDeleteProjects()",
                db.lastError().text()
            );
            lastError = rollbackError.getShortDescription();
        }
        return false;
    }
    
    return true;
}

// ==================== FTS全文搜索实现 ====================

/**
 * @brief 创建FTS全文搜索表
 * 
 * FTS（Full-Text Search）全文搜索优化：
 * 1. FTS5优先：使用FTS5虚拟表（SQLite 3.9.0+），性能最佳
 * 2. FTS4回退：如果FTS5不可用，自动回退到FTS4
 * 3. 内容同步：使用content选项，FTS表与主表自动同步
 * 
 * 性能优势：
 * - 全文搜索速度比LIKE查询快10-100倍
 * - 支持分词、短语搜索、相关性排序
 * - 索引优化，适合大数据量搜索
 * 
 * @return 是否创建成功
 */
bool DatabaseManager::createFTSTable()
{
    // 创建FTS5虚拟表（SQLite 3.9.0+支持FTS5）
    // 如果FTS5不可用，回退到FTS4（兼容旧版本SQLite）
    QStringList ftsQueries = {
        // 尝试创建FTS5表
        R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS projects_fts USING fts5(
                project_id,
                project_name,
                manager,
                status,
                description,
                content='projects',
                content_rowid='id'
            )
        )",
        // 如果FTS5失败，尝试FTS4（兼容旧版本SQLite）
        R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS projects_fts USING fts4(
                project_id,
                project_name,
                manager,
                status,
                description,
                content='projects',
                content_rowid='id'
            )
        )"
    };
    
    QSqlQuery query(db);
    
    // 先尝试FTS5
    if (query.exec(ftsQueries[0])) {
        qDebug() << "FTS5全文搜索表创建成功";
        // 创建触发器：增量维护FTS（避免启动时全量sync导致千万级数据卡死）
        QStringList triggerSql = {
            // 插入触发器
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ai
                AFTER INSERT ON projects
                BEGIN
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )",
            // 删除触发器
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ad
                AFTER DELETE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                END
            )",
            // 更新触发器（删除旧row再插入新row）
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_au
                AFTER UPDATE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )"
        };
        for (const QString &sql : triggerSql) {
            if (!query.exec(sql)) {
                qDebug() << "FTS触发器创建失败（不影响程序运行）:" << query.lastError().text();
            }
        }
        return true;
    }
    
    // 如果FTS5失败，尝试FTS4
    if (query.exec(ftsQueries[1])) {
        qDebug() << "FTS4全文搜索表创建成功（FTS5不可用）";
        // 创建触发器：增量维护FTS
        QStringList triggerSql = {
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ai
                AFTER INSERT ON projects
                BEGIN
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )",
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_ad
                AFTER DELETE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                END
            )",
            R"(
                CREATE TRIGGER IF NOT EXISTS trg_projects_fts_au
                AFTER UPDATE ON projects
                BEGIN
                    DELETE FROM projects_fts WHERE rowid = old.id;
                    INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
                    VALUES (new.id, new.project_id, new.project_name, new.manager, new.status, new.description);
                END
            )"
        };
        for (const QString &sql : triggerSql) {
            if (!query.exec(sql)) {
                qDebug() << "FTS触发器创建失败（不影响程序运行）:" << query.lastError().text();
            }
        }
        return true;
    }
    
    // 如果都失败，记录错误但不阻止程序运行
    ErrorInfo error = ErrorHandler::recordDatabaseError(
        ErrorCode::DatabaseQueryFailed,
        "创建FTS表失败（将使用传统LIKE搜索）",
        "DatabaseManager::createFTSTable()",
        query.lastError().text()
    );
    lastError = error.getShortDescription();
    qDebug() << ErrorHandler::formatError(error);
    return false;
}

/**
 * @brief 同步FTS全文搜索表数据
 * 
 * 同步策略：
 * 1. 检查FTS表是否存在
 * 2. 清空FTS表（避免重复数据）
 * 3. 从主表批量同步数据到FTS表
 * 
 * 注意：虽然FTS5支持content选项自动同步，但初始化时需要手动同步
 * 
 * @return 是否同步成功
 */
bool DatabaseManager::syncFTSData()
{
    // 检查FTS表是否存在
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='projects_fts'");
    checkQuery.setForwardOnly(true);  // 性能优化：只向前查询
    
    if (!checkQuery.exec() || !checkQuery.next()) {
        // FTS表不存在，不需要同步
        return true;
    }
    
    // 清空FTS表并重新填充
    QSqlQuery query(db);
    
    // 删除FTS表中的所有数据（为重新同步做准备）
    query.prepare("DELETE FROM projects_fts");
    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "清空FTS表失败",
            "DatabaseManager::syncFTSData()",
            query.lastError().text()
        );
        qDebug() << ErrorHandler::formatError(error);
        return false;
    }
    
    // 从主表批量同步数据到FTS表（一次性同步，性能优于逐条插入）
    query.prepare(R"(
        INSERT INTO projects_fts(rowid, project_id, project_name, manager, status, description)
        SELECT id, project_id, project_name, manager, status, description
        FROM projects
    )");
    query.setForwardOnly(true);  // 性能优化：只向前查询（千万级数据关键优化）
    
    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "同步FTS数据失败",
            "DatabaseManager::syncFTSData()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        return false;
    }
    
    qDebug() << "FTS数据同步完成";
    return true;
}

bool DatabaseManager::exportToCSVStream(const QString& filePath, std::function<void(int, int)> progressCallback)
{
    // 使用ExportService实现流式导出
    // 这个方法是为了实现IDatabaseAccessor接口
    // 实际导出功能由ExportService::exportFromDatabase实现
    
    if (!db.isOpen()) {
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::DatabaseConnectionFailed,
            ErrorLevel::Error,
            "数据库未连接",
            "DatabaseManager::exportToCSVStream()",
            ""
        );
        lastError = error.getShortDescription();
        return false;
    }

    // 委托给ExportService实现（通过导出服务层的exportFromDatabase方法）
    // 这里我们直接使用数据库连接和查询来实现
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ErrorInfo error = ErrorHandler::recordError(
            ErrorCode::FileIOError,
            ErrorLevel::Error,
            QString("无法创建文件: %1").arg(file.errorString()),
            "DatabaseManager::exportToCSVStream()",
            filePath
        );
        lastError = error.getShortDescription();
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    // 写入BOM头，确保Excel正确识别UTF-8编码
    out << "\xEF\xBB\xBF";
    
    // 写入表头
    QStringList headers = {"项目ID", "项目名称", "项目经理", "开始日期",
                           "结束日期", "预算(万元)", "状态", "描述"};
    out << headers.join(",") << "\n";

    // 获取总记录数
    QSqlQuery countQuery(db);
    countQuery.prepare("SELECT COUNT(*) FROM projects");
    countQuery.setForwardOnly(true);
    int totalRecords = 0;
    if (countQuery.exec() && countQuery.next()) {
        totalRecords = countQuery.value(0).toInt();
    }

    // 流式导出数据
    QSqlQuery query(db);
    query.prepare(R"(
        SELECT project_id, project_name, manager, start_date, 
               end_date, budget, status, description
        FROM projects
        ORDER BY created_at DESC, project_id DESC
    )");
    query.setForwardOnly(true);  // 只向前查询，节省内存

    int exportedRecords = 0;
    if (query.exec()) {
        while (query.next()) {
            QStringList rowData;
            for (int i = 0; i < 8; ++i) {
                QString field = query.value(i).toString();
                // CSV转义：如果包含逗号、引号或换行符，需要用引号包围并转义引号
                if (field.contains(',') || field.contains('"') || field.contains('\n')) {
                    field = "\"" + field.replace("\"", "\"\"") + "\"";
                }
                rowData << field;
            }
            out << rowData.join(",") << "\n";
            
            exportedRecords++;
            if (progressCallback) {
                progressCallback(exportedRecords, totalRecords);
            }
        }
    } else {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            "导出数据查询失败",
            "DatabaseManager::exportToCSVStream()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        file.close();
        return false;
    }

    file.close();
    qDebug() << QString("CSV导出完成：%1 条记录已导出到 %2").arg(exportedRecords).arg(filePath);
    return true;
}

QStandardItemModel* DatabaseManager::searchProjectsFTS(const QString& keyword, const QString& filterType)
{
    // 检查FTS表是否存在
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name='projects_fts'");
    checkQuery.setForwardOnly(true);  // 性能优化：只向前查询
    
    if (!checkQuery.exec() || !checkQuery.next()) {
        // FTS表不存在，回退到传统搜索
        qDebug() << "FTS表不存在，使用传统LIKE搜索";
        return searchProjects(keyword, filterType);
    }
    
    QStandardItemModel* searchModel = new QStandardItemModel();
    searchModel->setHorizontalHeaderLabels({
        "项目ID", "项目名称", "项目经理", "开始日期",
        "结束日期", "预算(万元)", "状态", "描述"
    });
    
    // 构建FTS搜索查询
    QString sql;
    
    // 根据过滤类型选择搜索列
    QStringList searchColumns;
    if (filterType == "all" || filterType == "id") {
        searchColumns << "project_id";
    }
    if (filterType == "all" || filterType == "name") {
        searchColumns << "project_name";
    }
    if (filterType == "all" || filterType == "manager") {
        searchColumns << "manager";
    }
    if (filterType == "all" || filterType == "status") {
        searchColumns << "status";
    }
    if (filterType == "all") {
        searchColumns << "description";
    }
    
    if (searchColumns.isEmpty()) {
        searchColumns << "project_name";  // 默认搜索项目名称
    }
    
    // 构建FTS搜索条件
    QString ftsKeyword = keyword;
    
    // 如果指定了列，使用 列名:关键词 格式
    if (filterType != "all" && !searchColumns.isEmpty()) {
        ftsKeyword = searchColumns.first() + ":" + keyword;
    }
    
    // 如果包含空格，用引号包围（短语搜索）
    if (keyword.contains(' ') && !keyword.startsWith('"')) {
        ftsKeyword = "\"" + keyword + "\"";
    }
    
    // 使用FTS MATCH语法进行全文搜索
    sql = QString(R"(
        SELECT p.project_id, p.project_name, p.manager, p.start_date, 
               p.end_date, p.budget, p.status, p.description
        FROM projects p
        INNER JOIN projects_fts fts ON p.id = fts.rowid
        WHERE fts MATCH ?
        ORDER BY p.created_at DESC, p.project_id DESC
    )");
    
    QSqlQuery query(db);
    query.prepare(sql);
    query.setForwardOnly(true);  // 性能优化：只向前查询，节省内存（千万级数据关键优化）
    query.bindValue(0, ftsKeyword);
    
    if (!query.exec()) {
        ErrorInfo error = ErrorHandler::recordDatabaseError(
            ErrorCode::DatabaseQueryFailed,
            QString("FTS搜索失败（关键词: %1, 类型: %2）").arg(keyword, filterType),
            "DatabaseManager::searchProjectsFTS()",
            query.lastError().text()
        );
        lastError = error.getShortDescription();
        // 回退到传统搜索
        return searchProjects(keyword, filterType);
    }
    
    while (query.next()) {
        QList<QStandardItem*> rowItems;
        for (int i = 0; i < 8; ++i) {
            rowItems << new QStandardItem(query.value(i).toString());
        }
        searchModel->appendRow(rowItems);
    }
    
    return searchModel;
}

bool DatabaseManager::logOperation(const QString& operationType, const QString& operationContent, 
                                   const QString& projectId, const QString& result)
{
    if (!db.isOpen()) {
        qDebug() << "数据库未连接，无法记录日志";
        return false;
    }

    QSqlQuery query(db);
    // 使用本地时间而不是UTC时间
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    query.prepare("INSERT INTO system_logs (timestamp, operation_type, operation_content, project_id, result) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.bindValue(0, currentTime);
    query.bindValue(1, operationType);
    query.bindValue(2, operationContent);
    query.bindValue(3, projectId.isEmpty() ? QVariant() : projectId);
    query.bindValue(4, result);

    if (!query.exec()) {
        qDebug() << "记录系统日志失败:" << query.lastError().text();
        return false;
    }

    return true;
}

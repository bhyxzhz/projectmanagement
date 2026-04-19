#ifndef APPCONSTANTS_H
#define APPCONSTANTS_H

#include <QtGlobal>  // 提供 qint64 等 Qt 基础类型

/**
 * @file appconstants.h
 * @brief 应用程序常量定义
 * 
 * 统一管理所有魔法数字和配置常量，提高代码可维护性
 */

// ==================== 数据生成相关常量 ====================
namespace DataGenerationConstants {
    const int FIXED_TEST_DATA_COUNT = 1000000;        // 固定生成测试数据数量（100万条）
    const int DEFAULT_TEST_DATA_COUNT = 100000;      // 默认测试数据生成数量（10万条）
    const int LARGE_TEST_DATA_COUNT = 1000000;      // 大数据量测试数据生成数量（100万条）
    const int MAX_TEST_DATA_COUNT = 10000000;        // 最大测试数据生成数量（1000万条）
    const int BATCH_SIZE = 50000;                     // 批量插入批次大小（每批50000条，减少事务开销，提升性能）
    const int VALUES_PER_INSERT = 500;                // 每次INSERT语句包含的记录数（多值INSERT，500条×8字段=4000参数，在SQLite限制内）
    const int PROGRESS_UPDATE_INTERVAL = 50000;      // 进度更新间隔（每5万条更新一次，减少UI更新开销）
}

// ==================== 分页相关常量 ====================
namespace PaginationConstants {
    const int DEFAULT_PAGE_SIZE = 3000;              // 默认每页显示记录数
    const int DEFAULT_CACHE_SIZE = 20;               // 默认缓存页数
    const int MIN_PAGE_SIZE = 10;                     // 最小页面大小
    const int MAX_PAGE_SIZE = 20000;                 // 最大页面大小
    const int LOAD_TIMER_INTERVAL_MS = 50;           // 加载定时器延迟（毫秒）
}

// ==================== 数据库相关常量 ====================
namespace DatabaseConstants {
    const int LARGE_DATA_THRESHOLD = 10000;          // 大数据量阈值（超过此值自动启用分页）
    const int CACHE_EXPIRE_MS = 30000;               // 缓存过期时间（30秒）
    const int MAX_CACHE_SIZE = 100;                   // 最大缓存条目数
    const int MAX_ID_GENERATION_ATTEMPTS = 1000;     // 最大ID生成尝试次数
}

// ==================== 导出相关常量 ====================
namespace ExportConstants {
    const int MAX_RECORDS_PER_FILE = 30000;          // 每个CSV文件最大记录数
    const int PROGRESS_UPDATE_FREQUENCY = 1000;       // 进度更新频率（每N条更新一次）
}

// ==================== UI相关常量 ====================
namespace UIConstants {
    const int DEFAULT_WINDOW_WIDTH = 1400;           // 默认窗口宽度
    const int DEFAULT_WINDOW_HEIGHT = 900;            // 默认窗口高度
    const int COLUMN_HEADER_HEIGHT = 25;              // 列标题高度
    const int HIGHLIGHT_DURATION_MS = 3000;          // 高亮显示持续时间（3秒）
    const int RESIZE_DELAY_MS = 50;                   // 窗口调整延迟（毫秒）
    const int DATA_LOAD_DELAY_MS = 100;               // 数据加载延迟（毫秒）
}

// ==================== 线程相关常量 ====================
namespace ThreadConstants {
    const int THREAD_WAIT_TIMEOUT_MS = 3000;          // 线程等待超时时间（3秒）
    const int THREAD_FORCE_TERMINATE_WAIT_MS = 2000; // 强制终止等待时间（2秒）
}

// ==================== 性能监控相关常量 ====================
namespace PerformanceConstants {
    const int MAX_LOG_ENTRIES = 100;                  // 最大日志条目数
    const int REFRESH_INTERVAL_MS = 2000;             // 刷新间隔（2秒）
}

// ==================== 项目验证相关常量 ====================
namespace ProjectValidationConstants {
    const int MIN_BUDGET = 10;                        // 最小预算（万元）
    const int MAX_BUDGET = 10000;                      // 最大预算（万元）
    const int MAX_PROJECT_NAME_LENGTH = 200;           // 项目名称最大长度
    const int MAX_MANAGER_NAME_LENGTH = 100;           // 项目经理姓名最大长度
    const int MAX_DESCRIPTION_LENGTH = 1000;           // 项目描述最大长度
}

// ==================== 数据库优化参数 ====================
namespace DatabaseOptimizationConstants {
    const int CACHE_SIZE_KB = -64000;                 // 缓存大小（64MB，负数表示KB）
    const qint64 MMAP_SIZE_BYTES = 1073741824;        // 内存映射大小（1GB）
    const int PAGE_SIZE_BYTES = 4096;                 // 页面大小（4KB）
    const int THREAD_COUNT = 4;                        // 线程数
}

#endif // APPCONSTANTS_H

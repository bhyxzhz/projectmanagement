# 高性能项目信息管理系统 (Qt/C++)

[![License](https://img.shields.io/badge/License-Proprietary-red.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-blue.svg)]()
[![Qt](https://img.shields.io/badge/Qt-6.0+-green.svg)]()

**一款专为千万级数据量设计的高性能企业级项目信息管理系统。**

基于 Qt6 框架开发，采用分页加载、智能缓存、FTS5 全文搜索、多线程数据生成与异步报表服务，确保在 1000 万条记录下依然保持流畅的用户体验。系统包含完整的权限管理、系统日志、CSV/PDF 报表导出及实时性能监控面板。

---

## 📖 目录

- [✨ 核心功能](#-核心功能)
- [🛠️ 技术架构与优化](#️-技术架构与优化)
- [📁 项目结构](#-项目结构)
- [🔧 构建与运行](#-构建与运行)
    - [环境要求](#环境要求)
    - [编译步骤](#编译步骤)
- [📘 使用手册](#-使用手册)
    - [默认账户](#默认账户)
    - [界面预览](#界面预览)
    - [数据生成与性能测试](#数据生成与性能测试)
- [📦 资源文件说明](#-资源文件说明)
- [📄 开源协议](#-开源协议)

---

## ✨ 核心功能

### 1. 高性能数据展示
- **智能分页**：支持 3000/5000/10000/20000 每页切换，底层使用 `LIMIT/OFFSET` 复合索引优化。
- **虚拟滚动预留接口**：预留 `VirtualScrollingModel` 类，支持超大数据量下的窗口级渲染优化。
- **缓存命中率统计**：LRU 缓存机制，避免重复 SQL 查询，切换页面毫秒级响应。

### 2. 海量数据搜索
- **FTS5 全文索引**：利用 SQLite FTS5 扩展实现项目名称、描述、经理的快速模糊搜索，性能远超传统 `LIKE` 语句。
- **智能回退**：若环境不支持 FTS5，自动降级为传统多列搜索。

### 3. 数据生成与压力测试
- **一键生成 100 万测试数据**：内置多线程异步生成器，**生成过程中主界面完全不卡顿**。
- **批量插入优化**：使用 `VALUES_PER_INSERT=500` 的多值插入语法，结合事务、关闭同步 (`PRAGMA synchronous=OFF`)，生成速度可达 **8-12 万条/秒**。
- **断点续传**：支持生成过程中断恢复。

### 4. 报表与导出
- **可视化报表**：内置饼图、水平柱状图（支持滚动）、趋势折线图，支持导出为 **PDF / Excel (CSV) / PNG**。
- **流式 CSV 导出**：支持千万级数据流式导出，自动分文件存储，避免内存溢出。

### 5. 权限与安全
- **双角色系统**：管理员 (`admin`) 与普通用户 (`user`)。
- **操作鉴权**：删除项目、清空数据、查看日志、管理用户等敏感操作需管理员密码二次验证。
- **日志审计**：所有增删改查操作记录至 `system_logs` 表，支持按时间、操作人检索。

### 6. 实时性能监控
- 监控总查询次数、缓存命中率、平均耗时、内存占用、数据库文件大小。
- 可视化图表展示各类型查询耗时分布。

---

## 🛠️ 技术架构与优化

本项目严格遵循 **SOLID 原则** 与 **千万级产品标准化** 要求。

| 层级 | 技术栈 / 设计模式 | 说明 |
| :--- | :--- | :--- |
| **界面层** | Qt6 Widgets, QSS | 自定义高亮、防抖搜索、状态栏实时刷新 |
| **服务层** | `ProjectService`, `SearchService`, `ExportService` | **依赖倒置原则 (DIP)**：依赖 `IDatabaseAccessor` 接口，便于单元测试与扩展 |
| **数据层** | SQLite 3 (WAL 模式) | **针对千万级数据的核心调优**：<br>• **复合索引覆盖**：`idx_projects_cover_list` 避免回表<br>• **WAL 与 内存映射**：`PRAGMA mmap_size=1GB`<br>• **独立线程连接**：防止 UI 线程阻塞数据库锁 |
| **配置管理** | `ConfigManager` (单例) + INI 文件 | 支持环境变量覆盖 (`PM_*`)，实现配置外部化 |
| **错误处理** | 统一 `ErrorHandler` | 全链路错误码追踪与用户友好提示转换 |

### 🔥 千万级数据优化关键点
1. **分页计数优化**：避免 `SELECT COUNT(*)` 全表扫描导致的卡顿，首次展示使用 `MAX(id)` 估算，后台异步计算准确值。
2. **数据生成锁优化**：生成数据时临时删除 FTS 触发器，生成完毕后再重建并增量同步，速度提升 **10 倍以上**。
3. **UI 防抖**：搜索框输入、窗口缩放均加入 50-150ms 防抖定时器。

---

## 📁 项目结构

```text
.
├── main.cpp                    # 程序入口，登录循环逻辑
├── mainwindow.cpp/h            # 主界面控制器
├── loginwindow.cpp/h           # 登录鉴权界面
│
├── appconstants.h              # ⚙️ 全局常量配置 (分页大小、批次限制等)
├── configmanager.cpp/h         # 📝 配置管理器 (读写 INI，环境变量覆盖)
├── errorhandler.cpp/h          # 🚨 统一错误处理
│
├── databasemanager.cpp/h       # 🗄️ 数据库底层实现 (实现 IDatabaseAccessor)
├── simplepaginationmodel.cpp/h # 📄 高性能分页模型 (带 LRU 缓存)
├── queryqueue.cpp/h            # ⏳ 查询队列 (去重、异步)
│
├── projectservice.cpp/h        # 📊 项目业务逻辑服务
├── searchservice.cpp/h         # 🔍 搜索服务 (FTS/传统切换)
├── exportservice.cpp/h         # 📤 导出服务 (流式 CSV)
├── reportservice.cpp/h         # 📈 报表数据聚合服务
│
├── reportwindow.cpp/h          # 📊 报表展示窗口 (含图表绘制)
├── systemlogwindow.cpp/h       # 📋 系统日志审计窗口
├── usermanagementwindow.cpp/h  # 👥 用户管理窗口
├── permissionmanager.cpp/h     # 🔐 权限管理器 (SHA256)
│
├── images.qrc                  # 🖼️ 资源集合文件
└── images/                     # 📁 图标资源目录 (logo.ico, add001.ico 等)

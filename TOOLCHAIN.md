# 项目工具链说明

本文档列出本项目中使用的所有开发工具、库和运行时依赖，按类别分节说明各自的功能与在项目中的作用。

---

## 一、编译器与构建工具

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **g++** | 15.x (Ubuntu) | `apt install g++` | GNU C++ 编译器。本项目使用 C++17 标准编译所有源代码，是项目的核心编译工具。 |
| **CMake** | 4.x | `apt install cmake` | 跨平台构建系统生成器。通过 `CMakeLists.txt` 描述项目结构、依赖关系和编译选项，生成 Makefile 后由 make 执行构建。 |
| **Make** | 4.x | `apt install make` | 经典构建工具。读取 CMake 生成的 Makefile，按依赖顺序调用 g++ 完成编译和链接。 |
| **pkg-config** | — | `apt install pkg-config` | 编译参数查询工具。在 CMake 构建过程中，用于自动查找已安装库的头文件路径和链接选项（如 mysqlclient）。 |
| **Git** | — | 系统预装 | 分布式版本控制系统。用于管理本项目源码，以及从 GitHub 克隆 isolate 等外部依赖的源码。 |

---

## 二、基础运行环境

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **WSL2** | 内核 6.6.x | Windows 组件 | Windows Subsystem for Linux 2。提供完整的 Linux 内核支持，包括 cgroups v2，是 isolate 沙箱能够正常运行的前提条件。 |
| **Ubuntu** | 26.04 | WSL2 内置 | 操作系统发行版。项目所用的 apt 包管理器、文件系统、用户权限管理均基于此系统。 |

---

## 三、JSON 解析库

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **nlohmann-json** | 3.11.3 | `apt install nlohmann-json3-dev` | "JSON for Modern C++" — 一个 header-only 的 C++ JSON 库。用于解析 RabbitMQ 消息中的 JSON 格式提交数据，以及加载 config.json 配置文件。特点是语法简洁，支持类似 STL 容器的操作方式。 |

---

## 四、消息队列（RabbitMQ）

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **RabbitMQ Server** | — | `apt install rabbitmq-server` | AMQP 0-9-1 协议的消息中间件服务端。负责接收前端提交的判题请求消息，并以队列形式分发给判题 Worker 消费。需要作为后台服务运行。 |
| **rabbitmq-c** | 0.15.0 | `apt install librabbitmq-dev` | RabbitMQ 的 C 语言客户端库。提供 `amqp_*` 系列 C API，用于连接 RabbitMQ、声明队列、消费消息、ACK/NACK 确认。本项目通过 PIMPL 模式将其封装为 C++ RAII 风格的 `RabbitMQClient` 类。 |

---

## 五、数据库（MySQL）

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **MySQL Server** | 8.x | `apt install mysql-server` | 关系型数据库服务端。存储判题系统中的题目、提交记录和判题结果。需要作为后台服务运行。 |
| **libmysqlclient** | 8.4.x | `apt install default-libmysqlclient-dev` | MySQL 的 C 语言客户端库。提供 `mysql_*` 系列 C API（连接、查询、prepared statement 等）。本项目通过 PIMPL 模式将其封装为 C++ RAII 风格的 `MySQLClient` 类，使用 prepared statement 防止 SQL 注入。 |

---

## 六、日志库

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **spdlog** | 1.15.3 | `apt install libspdlog-dev` | 高性能 C++ 日志库。支持多线程、多 sink（控制台/文件/滚动文件）、自定义格式。本项目用于输出 Worker 运行状态、判题结果、错误信息等日志，通过 `spdlog::info()` / `spdlog::error()` 等宏使用。 |

---

## 七、安全沙箱

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **isolate** | master 分支 | 源码编译 `github.com/ioi/isolate` | IOI（国际信息学奥林匹克）官方沙箱程序。基于 Linux cgroups 和命名空间实现：限制 CPU 时间、内存、进程数、文件系统访问等。本项目调用 `isolate --run` 在受控环境中编译和运行用户提交的代码，并通过解析其生成的 `meta` 文件获取 exit code、运行时间、内存用量等指标。需要 root 权限执行 `isolate --init` 一次性初始化。 |

---

## 八、测试依赖

| 名称 | 版本 | 来源 | 功能描述 |
|------|------|------|----------|
| **Python 3** | 3.x | 系统预装 | Python 解释器。用于运行测试消息发布脚本。 |
| **pika** | 最新 | `pip install pika` | Python 的 RabbitMQ 客户端库（AMQP 0-9-1）。测试脚本 `test_publisher.py` 使用它将 5 条模拟判题消息发送到 RabbitMQ 队列，触发 Worker 处理。 |
| **mysql-connector-python** | 最新 | `pip install mysql-connector-python` | Python 的 MySQL 客户端库。测试脚本使用它连接数据库、插入初始提交记录、查询判题结果以验证正确性。 |

---

## 九、工具链总览图

```
用户代码提交 (前端)
        │
        ▼
┌─────────────────┐
│  RabbitMQ Server │ ◄── rabbitmq-c (C 客户端) ──► RabbitMQClient (C++ 封装)
└────────┬────────┘
        │ JSON 消息
        ▼
┌─────────────────┐
│   Judge Worker   │ ◄── nlohmann-json (JSON 解析)
│   (main.cpp)     │ ◄── spdlog (日志)
└──┬──────┬───────┘
   │      │
   ▼      ▼
┌──────┐ ┌──────────┐
│isolate│ │MySQL     │
│沙箱  │ │Server    │
└──┬───┘ └────┬─────┘
   │          │
   ▼          ▼
 编译运行    libmysqlclient (C API)
 用户代码       │
   │          ▼
   ▼      MySQLClient (C++ 封装)
 meta 文件
 (资源统计)
```

---

*注：本文件中所有 apt 包版本均为编写时的可用版本。不同系统环境下，`apt install` 获取的具体版本号可能略有差异。*

# FlowStudy-Judge 判题沙箱服务实现计划

## Context

本项目是一个在线判题系统的后端判题沙箱服务。技术栈：C++17 + CMake + RabbitMQ(rabbitmq-c) + MySQL + isolate 沙箱。项目根目录 `/home/dhc/FlowStudy-Judge`，当前只有 AGENTS.md 和 `.git`。

核心流程：RabbitMQ 消费消息 → isolate 沙箱编译运行 → 输出比对 → MySQL 写回结果 → ACK。

## 依赖安装

```bash
sudo apt install -y g++ cmake make pkg-config git \
    nlohmann-json3-dev librabbitmq-dev default-libmysqlclient-dev libspdlog-dev

# isolate 从源码构建
cd /tmp && git clone https://github.com/ioi/isolate.git && cd isolate
make -j$(nproc) && sudo make install PREFIX=/usr/local
sudo /usr/local/bin/isolate --init
```

所有包已验证可用：librabbitmq-dev(0.15.0)、libspdlog-dev(1.15.3)、nlohmann-json3-dev(3.11.3)。

## 项目结构

```
FlowStudy-Judge/
├── CMakeLists.txt              # 顶层 CMake
├── config.example.json         # 配置模板
├── scripts/
│   ├── init_isolate.sh         # isolate 初始化脚本
│   ├── setup_db.sql            # 数据库建表 DDL
│   └── test_publisher.py       # 测试用 RabbitMQ 消息发布脚本
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                # 入口
│   ├── config/
│   │   ├── config.h            # AppConfig 结构体 + load_config()
│   │   └── config.cpp
│   ├── mq/
│   │   ├── rabbitmq_client.h   # RabbitMQ RAII 封装(基于 rabbitmq-c)
│   │   └── rabbitmq_client.cpp
│   ├── db/
│   │   ├── mysql_client.h      # MySQL RAII 封装(基于 mysql C API)
│   │   └── mysql_client.cpp
│   ├── judge/
│   │   ├── status.h            # JudgeStatus 枚举 + 字符串转换
│   │   ├── status.cpp
│   │   ├── sandbox.h           # isolate 封装(init/compile/run/cleanup)
│   │   ├── sandbox.cpp
│   │   ├── judge_engine.h      # 判题核心逻辑(编译→运行→比对)
│   │   └── judge_engine.cpp
│   └── worker/
│       ├── worker.h            # 主循环(消费→判题→写DB→ACK)
│       └── worker.cpp
├── test/
│   ├── CMakeLists.txt
│   ├── test_runner.cpp         # 简单测试框架入口
│   ├── integration_test.cpp    # 集成测试
│   └── test_codes/             # 5 种判题结果的测试代码
│       ├── ac.cpp              # Accepted: a+b
│       ├── wa.cpp              # Wrong Answer: a*b
│       ├── re.cpp              # Runtime Error: 除零
│       ├── tle.cpp             # TLE: 死循环
│       └── ce.cpp              # Compilation Error: 语法错误
└── README.md
```

## 模块设计

### 1. Config (`src/config/config.h`)
- `RabbitMQConfig`, `MySQLConfig`, `IsolateConfig`, `JudgeConfig`, `AppConfig` 结构体
- `std::optional<AppConfig> load_config(const std::string& filepath)` 从 JSON 加载

### 2. Status (`src/judge/status.h`)
- `enum class JudgeStatus`: Pending, Compiling, Running, Accepted, WrongAnswer, TimeLimitExceeded, MemoryLimitExceeded, RuntimeError, CompilationError, SystemError
- `to_string()`, `to_description()`, `status_from_string()`

### 3. Sandbox (`src/judge/sandbox.h`)
- `Sandbox(int box_id, const IsolateConfig&)` — 每个沙箱对应一个 isolate box
- `init()` / `cleanup()` — 盒子初始化与清理
- `compile(const string& source_code)` → SandboxResult
- `run(int time_limit_ms, int memory_limit_kb, const string& stdin_input)` → SandboxResult
- `SandboxResult`: exit_code, signal_num, time_used_ms, memory_used_kb, stdout_output, stderr_output, timed_out
- 通过 `isolate --run` 执行，解析 meta 文件获取资源使用数据

### 4. JudgeEngine (`src/judge/judge_engine.h`)
- `static parse_message(const string& json)` → SubmissionMessage
- `judge(const SubmissionMessage&)` → JudgeResultInternal
- 内部 `BoxPool` 管理并发沙箱(互斥锁+条件变量)
- `compare_output()` 做空白规范化比对
- 流程：编译 → 逐个用例运行 → 比对输出 → 返回结果

### 5. RabbitMQClient (`src/mq/rabbitmq_client.h`)
- PIMPL 封装 rabbitmq-c 的 C API
- `connect()` / `consume(callback)` / `ack(tag)` / `nack_requeue(tag)`

### 6. MySQLClient (`src/db/mysql_client.h`)
- PIMPL 封装 MySQL C API (libmysqlclient)
- `connect()` / `update_submission(JudgeResult)` / `ping()`
- 使用 prepared statements 防 SQL 注入

### 7. Worker (`src/worker/worker.h`)
- `run()` 连接 MQ + DB，进入消费循环
- `on_message(tag, body)` 解析→判题→写DB→ACK
- 信号处理：SIGINT/SIGTERM 优雅关闭

## 数据库 Schema

```sql
CREATE TABLE submissions (
    submission_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id      BIGINT UNSIGNED NOT NULL,
    language        VARCHAR(20) NOT NULL DEFAULT 'cpp',
    code            MEDIUMTEXT NOT NULL,
    time_limit_ms   INT UNSIGNED NOT NULL DEFAULT 1000,
    memory_limit_kb INT UNSIGNED NOT NULL DEFAULT 262144,
    status          VARCHAR(30) NOT NULL DEFAULT 'Pending',
    time_used_ms    INT UNSIGNED NOT NULL DEFAULT 0,
    memory_used_kb  INT UNSIGNED NOT NULL DEFAULT 0,
    error_message   TEXT,
    compiler_output TEXT,
    failed_testcase INT NOT NULL DEFAULT -1,
    created_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    judged_at       DATETIME,
    INDEX idx_problem (problem_id),
    INDEX idx_status (status)
);
```

## 消息格式

```json
{
  "submission_id": 12345,
  "problem_id": 100,
  "language": "cpp",
  "code": "#include <iostream>...",
  "time_limit": 1000,
  "memory_limit": 262144,
  "testcases": [
    {"input": "1 2", "expected_output": "3"}
  ]
}
```

## 配置文件格式

```json
{
  "rabbitmq": {
    "hostname": "127.0.0.1", "port": 5672, "vhost": "/",
    "username": "judge", "password": "judge_pass",
    "queue_name": "submission_queue"
  },
  "mysql": {
    "hostname": "127.0.0.1", "port": 3306,
    "username": "judge", "password": "judge_pass",
    "database": "online_judge"
  },
  "isolate": {
    "binary_path": "/usr/local/bin/isolate",
    "box_id_start": 1, "num_boxes": 5
  },
  "judge": { "concurrency": 1 }
}
```

## 测试方案

用 Python 脚本 (`scripts/test_publisher.py`) 发送 5 条 RabbitMQ 消息，覆盖：

| # | 代码 | 题目 | 期望结果 |
|---|------|------|----------|
| 1 | ac.cpp (a+b) | a+b | Accepted |
| 2 | wa.cpp (a*b) | a+b | WrongAnswer |
| 3 | re.cpp (除零) | a+b | RuntimeError |
| 4 | tle.cpp (死循环) | a+b (time_limit=500ms) | TimeLimitExceeded |
| 5 | ce.cpp (语法错误) | a+b | CompilationError |

测试题目统一为 a+b，含 3 个测试用例：`(1 2, 3)`, `(5 7, 12)`, `(-3 8, 5)`。

## 实现顺序

1. 安装依赖 (g++, cmake, rabbitmq-c, mysql, spdlog, nlohmann-json, isolate)
2. `config/` — 配置加载
3. `judge/status.h+cpp` — 状态枚举
4. CMakeLists.txt 骨架
5. `judge/sandbox.h+cpp` — isolate 封装
6. `judge/judge_engine.h+cpp` — 判题核心
7. `db/mysql_client.h+cpp` — 数据库操作
8. `mq/rabbitmq_client.h+cpp` — 消息队列
9. `worker/worker.h+cpp` — 主循环
10. `main.cpp` — 入口
11. `scripts/setup_db.sql`, `scripts/test_publisher.py`, `test/test_codes/*.cpp`
12. `test/` — 测试框架
13. `README.md` — 项目文档

## 验证方式

1. `cmake -B build && cmake --build build` 编译通过
2. 手动发送一条 AC 消息，验证 Worker 能完成完整流程
3. 运行 `python3 scripts/test_publisher.py` 发送 5 条测试消息
4. 查询 MySQL 确认 5 条记录的 status 符合预期

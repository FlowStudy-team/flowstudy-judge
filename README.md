# FlowStudy-Judge

在线判题系统（Online Judge）后端判题沙箱服务。基于 C++17、RabbitMQ、MySQL 和 IOI Isolate 沙箱构建。

## 架构概览

```
┌──────────┐     RabbitMQ      ┌──────────────────┐     MySQL      ┌──────────┐
│  Web 前端 │ ──── JSON ──────► │  Judge Worker    │ ──── UPDATE ──► │  MySQL   │
│  (外部)   │                   │  (judge_worker)  │                │  Server  │
└──────────┘                   └───────┬──────────┘                └──────────┘
                                       │
                                isolate │ --run
                                       ▼
                               ┌───────────────┐
                               │  Isolate 沙箱  │
                               │  (cgroups)    │
                               └───────────────┘
```

**数据流：**

1. Web 前端将代码提交写入 MySQL（状态置为 `Pending`），同时发送 JSON 消息到 RabbitMQ 队列
2. Judge Worker 从 RabbitMQ 消费消息，解析得到提交 ID、代码、测试用例等信息
3. 调用 `isolate` 沙箱编译代码（g++ -std=c++17 -O2 -Wall）
4. 对每个测试用例，在沙箱中运行编译后的程序，捕获 stdout/stderr
5. 将实际输出与预期输出做空白规范化比对
6. 将判题结果（状态、时间、内存、错误信息）更新回 MySQL `submissions` 表
7. ACK 确认消息，继续下一条

## 项目结构

```
FlowStudy-Judge/
├── CMakeLists.txt                  # 顶层 CMake 构建文件
├── config.example.json             # 配置文件模板
├── README.md
├── TOOLCHAIN.md                    # 工具链说明
├── IMPLEMENTATION_PLAN.md          # 实现计划
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                    # 程序入口
│   ├── config/
│   │   ├── config.h                # 配置结构体定义
│   │   └── config.cpp              # JSON 配置加载
│   ├── mq/
│   │   ├── rabbitmq_client.h       # RabbitMQ 客户端封装
│   │   └── rabbitmq_client.cpp
│   ├── db/
│   │   ├── mysql_client.h          # MySQL 客户端封装
│   │   └── mysql_client.cpp
│   ├── judge/
│   │   ├── status.h                # 判题状态枚举
│   │   ├── status.cpp
│   │   ├── sandbox.h               # Isolate 沙箱封装
│   │   ├── sandbox.cpp
│   │   ├── judge_engine.h          # 判题核心引擎
│   │   └── judge_engine.cpp
│   └── worker/
│       ├── worker.h                # 主循环（消费→判题→写DB→ACK）
│       └── worker.cpp
├── test/
│   ├── CMakeLists.txt
│   ├── test_runner.cpp             # 单元测试入口
│   ├── integration_test.cpp        # 模块测试
│   └── test_codes/                 # 测试用代码
│       ├── ac.cpp                  # a+b 正确解
│       ├── wa.cpp                  # a*b 错误解
│       ├── re.cpp                  # 除零运行时错误
│       ├── tle.cpp                 # 死循环超时
│       └── ce.cpp                  # 语法错误
└── scripts/
    ├── setup_db.sql                # 数据库建表 DDL
    ├── init_isolate.sh             # Isolate 沙箱初始化脚本
    └── test_publisher.py           # 端到端测试脚本
```

## 环境与依赖

| 类别 | 组件 | 版本 | 用途 |
|------|------|------|------|
| 编译器 | g++ | 15.x | C++17 编译 |
| 构建 | CMake + Make | 4.x | 构建系统 |
| JSON | nlohmann-json | 3.11.3 | 消息解析与配置加载 |
| 消息队列 | RabbitMQ + rabbitmq-c | 0.15.0 | 接收提交消息 |
| 数据库 | MySQL + libmysqlclient | 8.4.x | 存储判题结果 |
| 日志 | spdlog | 1.15.3 | 运行日志 |
| 沙箱 | isolate | v2.6 | 安全编译与执行 |

## 快速开始

### 1. 安装系统依赖

```bash
sudo apt update
sudo apt install -y g++ cmake make pkg-config git \
    nlohmann-json3-dev librabbitmq-dev default-libmysqlclient-dev \
    libspdlog-dev libsystemd-dev libseccomp-dev libcap-dev asciidoc-base
```

### 2. 安装 isolate 沙箱

```bash
cd /tmp
git clone https://github.com/ioi/isolate.git
cd isolate
make -j$(nproc)
sudo make install PREFIX=/usr/local

# 创建 isolate 系统用户（如尚未创建）
sudo useradd -r -s /bin/false isolate
echo "isolate:100000:65536" | sudo tee -a /etc/subuid
echo "isolate:100000:65536" | sudo tee -a /etc/subgid

# 初始化沙箱
sudo /usr/local/bin/isolate --init
```

### 3. 构建项目

```bash
cd /home/dhc/FlowStudy-Judge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 4. 配置

```bash
cp config.example.json config.json
# 编辑 config.json，填入你的 RabbitMQ 和 MySQL 连接信息
```

### 5. 初始化数据库

```bash
mysql -u root -p < scripts/setup_db.sql
```

### 6. 初始化判题沙箱

```bash
sudo ./scripts/init_isolate.sh 5 1
# 创建 5 个 isolate box（ID 1~5）
```

### 7. 启动 Worker

```bash
./build/src/judge_worker config.json
```

### 8. 运行测试

```bash
# 单元测试
./build/test/test_runner

# 端到端测试（需要 RabbitMQ + MySQL 运行中）
pip install pika mysql-connector-python
python3 scripts/test_publisher.py config.json
```

## 配置说明

配置文件使用 JSON 格式，路径默认为 `./config.json`，可通过命令行参数指定。

```json
{
    "rabbitmq": {
        "hostname": "127.0.0.1",    // RabbitMQ 服务器地址
        "port": 5672,               // 端口
        "vhost": "/",               // 虚拟主机
        "username": "judge",        // 用户名
        "password": "judge_pass",   // 密码
        "queue_name": "submission_queue"  // 消费队列名
    },
    "mysql": {
        "hostname": "127.0.0.1",    // MySQL 服务器地址
        "port": 3306,               // 端口
        "username": "judge",        // 用户名
        "password": "judge_pass",   // 密码
        "database": "online_judge"  // 数据库名
    },
    "isolate": {
        "binary_path": "/usr/local/bin/isolate",  // isolate 可执行文件路径
        "box_id_start": 1,                        // 起始 box ID
        "num_boxes": 5                            // box 数量（并发度）
    },
    "judge": {
        "concurrency": 1           // 并发度（当前为单线程，预留字段）
    }
}
```

## 数据库 Schema

### 表：`submissions`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `submission_id` | BIGINT UNSIGNED PK AUTO_INCREMENT | — | 提交 ID |
| `problem_id` | BIGINT UNSIGNED NOT NULL | — | 题目 ID |
| `language` | VARCHAR(20) | `'cpp'` | 编程语言 |
| `code` | MEDIUMTEXT NOT NULL | — | 源代码 |
| `time_limit_ms` | INT UNSIGNED | 1000 | 时间限制（毫秒） |
| `memory_limit_kb` | INT UNSIGNED | 262144 | 内存限制（KB） |
| `status` | VARCHAR(30) | `'Pending'` | 判题状态 |
| `time_used_ms` | INT UNSIGNED | 0 | 实际运行时间（毫秒） |
| `memory_used_kb` | INT UNSIGNED | 0 | 实际内存使用（KB） |
| `error_message` | TEXT | NULL | 错误信息 |
| `compiler_output` | TEXT | NULL | 编译器输出 |
| `failed_testcase` | INT | -1 | 失败测试用例索引（-1 表示无） |
| `created_at` | DATETIME | NOW() | 创建时间 |
| `judged_at` | DATETIME | NULL | 判题完成时间 |

## 判题状态码

| 状态 | 数据库值 | 含义 |
|------|----------|------|
| `Pending` | `Pending` | 等待判题 |
| `Compiling` | `Compiling` | 编译中（内部使用） |
| `Running` | `Running` | 运行中（内部使用） |
| `Accepted` | `Accepted` | 通过全部测试用例 |
| `WrongAnswer` | `WrongAnswer` | 输出与预期不符 |
| `TimeLimitExceeded` | `TimeLimitExceeded` | 运行超时 |
| `MemoryLimitExceeded` | `MemoryLimitExceeded` | 内存超限 |
| `RuntimeError` | `RuntimeError` | 运行时错误（非零退出 / 信号） |
| `CompilationError` | `CompilationError` | 编译失败 |
| `SystemError` | `SystemError` | 系统内部错误 |

**状态转换流：**

```
Pending → Compiling → Running → Accepted / WrongAnswer / TLE / MLE / RE
                        ↘ CompilationError
任一步骤异常 → SystemError
```

## 消息格式

RabbitMQ 消息为 JSON 格式：

```json
{
    "submission_id": 12345,
    "problem_id": 100,
    "language": "cpp",
    "code": "#include <iostream>\nint main() {\n    int a, b;\n    std::cin >> a >> b;\n    std::cout << a + b << std::endl;\n    return 0;\n}",
    "time_limit": 1000,
    "memory_limit": 262144,
    "testcases": [
        {"input": "1 2", "expected_output": "3"},
        {"input": "5 7", "expected_output": "12"}
    ]
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `submission_id` | uint64 | 是 | 对应 submissions 表的主键 |
| `problem_id` | uint64 | 是 | 题目 ID |
| `language` | string | 否 | 编程语言，默认 `"cpp"` |
| `code` | string | 是 | 源代码全文 |
| `time_limit` | int | 否 | 时间限制（毫秒），默认 1000 |
| `memory_limit` | int | 否 | 内存限制（KB），默认 262144 |
| `testcases` | array | 是 | 测试用例列表 |
| `testcases[].input` | string | 是 | 标准输入内容 |
| `testcases[].expected_output` | string | 是 | 预期标准输出 |

## 输出比对规则

程序输出与预期输出按以下规则比对：

1. 去除每行末尾空白字符（空格、制表符、回车）
2. 去除末尾连续空行
3. 逐行精确比较

这允许程序输出末尾多余的空白字符或空行而不被判为 Wrong Answer。

## 测试

### 单元测试

```bash
./build/test/test_runner
```

覆盖：状态枚举转换、消息 JSON 解析、输出比对算法、配置加载。

### 端到端测试

端到端测试覆盖 5 种判题结果：

| submission_id | 代码 | 行为 | 期望结果 |
|:---:|------|------|----------|
| 1 | `test/test_codes/ac.cpp` | 正确计算 a+b | `Accepted` |
| 2 | `test/test_codes/wa.cpp` | 乘法替代加法 | `WrongAnswer` |
| 3 | `test/test_codes/re.cpp` | 除零崩溃 | `RuntimeError` |
| 4 | `test/test_codes/tle.cpp` | 死循环（time_limit=500ms） | `TimeLimitExceeded` |
| 5 | `test/test_codes/ce.cpp` | 语法错误 | `CompilationError` |

运行方式：

```bash
# 1. 确保 RabbitMQ 和 MySQL 服务运行中
# 2. 确保 judge_worker 正在运行
./build/src/judge_worker config.json &

# 3. 发送测试消息
pip install pika mysql-connector-python
python3 scripts/test_publisher.py config.json

# 4. 观察输出，预期 5 条全部 PASS
```

---

## 待定接口说明

本节记录项目中自行决定的数据格式和接口设计。当前项目属于更大系统的一部分，以下内容在前后端交互协议最终确定后可能需要修改。

### 1. RabbitMQ 消息 JSON 结构

当前消息字段（`submission_id`、`problem_id`、`language`、`code`、`time_limit`、`memory_limit`、`testcases[]`）基于合理假设设计。后续如前端需要传递额外字段（如 `contest_id`、`user_id`、`compiler_flags`、`special_judge` 标志等），需同步修改 `JudgeEngine::parse_message()` 和 `SubmissionMessage` 结构体。

### 2. 数据库表 `submissions` 结构

当前表结构包含判题所需的最小字段集。如有新增需求（如记录每次提交的 IP、用户 ID、比赛 ID、编译器版本等），需要追加列。

### 3. 判题状态字符串

当前 10 种状态值直接以可读字符串形式存储在 `status` 列（如 `"Accepted"`、`"WrongAnswer"`）。如前端需要不同的枚举名称或数字编码，修改 `to_string()` / `status_from_string()` 即可。

### 4. 测试用例格式

每个测试用例包含 `input` 和 `expected_output` 两个字符串字段。后续如需支持文件 IO 类题目（`.in` / `.out` 文件），或 Special Judge（由单独程序判定对错），需要扩展消息格式和 `JudgeEngine::judge()` 逻辑。

### 5. 沙箱配置

当前仅支持 isolate 一种沙箱。`Sandbox` 类的接口（`init()` / `compile()` / `run()` / `cleanup()`）可抽象为基类以支持替代沙箱实现（如 Docker、seccomp-only 模式）。

### 6. 编程语言支持

当前仅支持 `"cpp"`。扩展其他语言（Python、Java、C）需要：
- 在 `Sandbox::compile()` 中根据语言选择对应的编译/解释命令
- 在 `Sandbox::run()` 中选择对应的运行方式
- 在消息和数据库中保持 `language` 字段传递

---

## 模块说明

### Config（配置加载）

通过 `load_config()` 从 JSON 文件加载 `AppConfig` 结构体。所有字段均有默认值，缺失字段不报错。

### Status（状态枚举）

`JudgeStatus` 枚举定义 10 种判题状态，提供 `to_string()` / `status_from_string()` / `to_description()` 转换函数。

### Sandbox（沙箱封装）

封装 `isolate` 命令行工具：

- `init()` — `isolate --init --box-id=N`
- `cleanup()` — `isolate --cleanup --box-id=N`
- `compile(source)` — 写入 `solution.cpp`，调用 `isolate --run -- g++` 编译
- `run(time, mem, stdin)` — 写入 `input.txt`，调用 `isolate --run -- ./solution` 执行
- 自动解析 isolate 生成的 `meta` 文件获取运行指标

### JudgeEngine（判题引擎）

- `parse_message(json)` — 将 JSON 消息解析为 `SubmissionMessage` 结构
- `judge(submission)` — 执行编译→逐个用例运行→输出比对→返回结果
- `compare_output()` — 空白规范化比对
- 内置 `BoxPool` 管理沙箱并发（互斥锁 + 条件变量）

### RabbitMQClient（消息队列客户端）

PIMPL 模式封装 `rabbitmq-c` 的 C API：

- `connect()` — 建立连接、打开通道、声明队列、设置 QoS
- `consume(callback)` — 阻塞消费循环，每次调用回调
- `ack(tag)` / `nack_requeue(tag)` — 消息确认与拒绝

### MySQLClient（数据库客户端）

PIMPL 模式封装 `libmysqlclient` 的 C API：

- `connect()` — 建立连接并预编译 UPDATE 语句
- `update_submission(result)` — 使用 prepared statement 更新判题结果
- `ping()` — 保持连接活跃（空闲超时后自动重连）

### Worker（主工作循环）

- `run()` — 连接 MQ + DB（失败重试），进入消费循环
- `on_message()` — 解析→判题→写DB→ACK/NACK
- `shutdown()` — 响应 SIGINT/SIGTERM 优雅退出

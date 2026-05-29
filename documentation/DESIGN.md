# 项目架构与设计思想

## 一、整体架构

FlowStudy-Judge 是一个在线判题系统的后端判题沙箱服务。它不直接与前端交互，而是作为后台 Worker 从 RabbitMQ 消费消息、调用 isolate 沙箱执行判题、将结果写回 MySQL。

```
┌──────────┐   RabbitMQ    ┌────────────────┐   MySQL   ┌──────────┐
│  Web 前端 │ ─── JSON ───► │  Judge Worker   │ ── SQL ──► │  MySQL   │
└──────────┘               └──────┬─────────┘           └──────────┘
                                  │
                            isolate --run
                                  │
                          ┌───────▼───────┐
                          │ Isolate 沙箱   │
                          │ (编译 + 运行)  │
                          └───────────────┘
```

### 核心流程

1. Worker 从 RabbitMQ 的 `submission_queue` 队列消费一条 JSON 消息
2. 解析消息得到提交 ID、源代码、测试用例等信息
3. 调用 isolate 沙箱编译源代码（在受控环境中执行 g++）
4. 对每个测试用例，在沙箱中运行编译产物，捕获 stdout/stderr
5. 将实际输出与预期输出进行空白规范化比对
6. 将判题结果（状态、时间、内存、错误信息）更新回 MySQL `submissions` 表
7. ACK 确认该消息，继续消费下一条

## 二、设计原则

### 2.1 单一职责

每个模块只负责一件事：

| 模块 | 职责 | 不关心的事 |
|------|------|-----------|
| Config | 从 JSON 文件加载配置 | 配置怎么被使用 |
| Status | 判题状态枚举与字符串互转 | 状态怎么被判定 |
| Sandbox | 调用 isolate 命令行工具 | 判题逻辑 |
| JudgeEngine | 判题流程编排（编译→运行→比对） | 消息来源、结果去向 |
| RabbitMQClient | 连接、消费、ACK/NACK | 消息内容 |
| MySQLClient | 连接、执行 prepared statement | SQL 语义 |
| Worker | 模块组装、主循环、信号处理 | 每个模块的内部实现 |
| main | 加载配置、初始化日志、启动 Worker | 业务逻辑 |

### 2.2 依赖注入（构造函数注入）

每个模块通过构造函数接收其依赖的配置或其他模块，不使用全局单例：

```cpp
Worker worker(cfg);              // cfg 来自外部
RabbitMQClient mq(cfg.rabbitmq); // 只接收自己需要的配置
MySQLClient db(cfg.mysql);       // 不读取完整 AppConfig
```

### 2.3 PIMPL 模式

对于封装 C 库的模块（RabbitMQClient、MySQLClient），使用 PIMPL（Pointer to IMPLementation）模式将 C 头文件和类型隔离在 `.cpp` 文件内部，避免污染 C++ 公开接口。

```cpp
// rabbitmq_client.h — 对使用者透明
class RabbitMQClient {
    struct Impl;
    std::unique_ptr<Impl> pimpl_;  // 内部持有 amqp_connection_state_t 等 C 类型
};
```

### 2.4 资源池（BoxPool）

JudgeEngine 内部维护一个沙箱资源池（`BoxPool`），使用互斥锁 + 条件变量管理并发访问：

```
acquire() → 从可用队列取出 box_id → 使用 → release() → 归还到可用队列
```

当前版本并发度为 1，但 BoxPool 接口设计支持多线程扩展。

### 2.5 错误处理策略

- **配置错误**：`load_config()` 返回 `std::nullopt`，main 函数退出
- **消息解析错误**：Worker 记录日志后 ACK 该消息（丢弃），避免死循环
- **编译错误**：记录为 `CompilationError`，写入编译器输出
- **运行时错误**：记录为 `RuntimeError`/`TLE`/`MLE`/`WrongAnswer`
- **连接失败**：Worker 使用指数退避重试（2s → 4s → 8s → ... → 30s）
- **数据库写入失败**：NACK 消息（requeue），等待重试

## 三、模块设计详解

### 3.1 Sandbox（沙箱封装）

封装 `isolate` 命令行工具的所有交互：

```
Sandbox
├── init()                    → isolate --init --box-id=N
├── cleanup()                 → isolate --cleanup --box-id=N
├── compile(source_code)      → 写入 source → isolate --run g++ → 返回编译结果 + meta
├── run(time, mem, stdin)     → 写入 input → isolate --run ./binary → 返回运行结果 + meta
└── 内部方法
    ├── write_file(filename)  → 写入沙箱 box 目录
    ├── read_file(filename)   → 从沙箱 box 目录读取
    ├── execute(args, meta)   → 构建 isolate 命令、执行、解析 meta
    └── parse_meta(path)      → 解析 isolate 生成的 meta 文件
```

**关键设计决策：**
- `--meta` 参数使用宿主机绝对路径（`/var/local/lib/isolate/{id}/box/meta.txt`），因为 meta 文件由 isolate 进程自身在宿主机上写入
- `--stdin`、`--stdout`、`--stderr` 使用相对文件名（如 `input.txt`），因为这些文件在沙箱内部由被沙箱化的进程打开
- 编译和运行都需要设置 `--env=PATH=/usr/bin:/bin`，因为沙箱内环境变量为空白，g++ 无法找到 `ld` 等子工具

### 3.2 JudgeEngine（判题引擎）

核心判题流程：

```
judge(submission)
├── acquire_box()
├── compile(code)
│   ├── 成功 → 继续
│   └── 失败 → 返回 CompilationError + compiler_output
├── for each testcase:
│   ├── run(time_limit, memory_limit, input)
│   ├── 检查超时 → 返回 TimeLimitExceeded
│   ├── 检查内存 → 返回 MemoryLimitExceeded
│   ├── 检查退出码/信号 → 返回 RuntimeError
│   ├── compare_output(expected, actual) → 不匹配 → 返回 WrongAnswer
│   └── 继续下一用例
├── 全部通过 → 返回 Accepted
└── release_box()
```

### 3.3 输出比对算法

`compare_output()` 实现宽松的空白比对：

1. 将 output 按行分割
2. 去除每行末尾空白字符（空格、制表符、回车）
3. 去除末尾连续空行
4. 逐行精确比较（包括行首空白）

这样设计是因为多数 OJ 题目允许多余的行末空白和末尾空行。

### 3.4 Worker 主循环

```
worker.run()
├── 连接 MySQL（重试直到成功）
├── 连接 RabbitMQ（重试直到成功）
├── 进入消费循环:
│   └── on_message(tag, body):
│       ├── parse_message(body) → 解析失败则 ACK（丢弃）
│       ├── judge(submission)
│       ├── db.ping() → 保持连接活跃
│       ├── db.update_submission(result) → 失败则 NACK（requeue）
│       └── mq.ack(tag)
└── 收到 SIGINT/SIGTERM → shutdown() → 退出循环
```

## 四、数据流

### 消息格式 → 数据库

```
RabbitMQ JSON
│
├─ submission_id  ──────────────────►  UPDATE submissions WHERE submission_id = ?
├─ code           ─► 编译 → 运行 ──►  无直接对应（不存储代码）
├─ time_limit     ─► isolate --wall-time
├─ memory_limit   ─► 比对 max-rss（预留，当前无 cgroups）
├─ testcases[]    ─► for each: run → compare
│
└─ 判题结果 → JudgeResult
   ├─ status            → 数据库 status
   ├─ time_used_ms      → 数据库 time_used_ms
   ├─ memory_used_kb    → 数据库 memory_used_kb
   ├─ error_message     → 数据库 error_message
   ├─ compiler_output   → 数据库 compiler_output
   └─ failed_testcase_index → 数据库 failed_testcase
```

## 五、安全设计

1. **沙箱隔离**：用户代码在 isolate 沙箱中运行，无权访问宿主机文件系统
2. **资源限制**：通过 `--wall-time` 限制运行时间；编译时间限制 30 秒
3. **SQL 注入防护**：MySQL 使用 prepared statement，参数绑定而非字符串拼接
4. **内存安全**：C++17 智能指针管理生命周期，无裸指针；PIMPL 使用 `std::unique_ptr`
5. **消息可靠性**：仅在数据库写入成功后 ACK；写入失败则 NACK + requeue

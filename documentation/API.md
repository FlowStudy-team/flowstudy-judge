# API 文档

本文档描述各模块的公开接口（函数签名、参数、返回值、行为）。

---

## 1. Config 模块

**文件**: `src/config/config.h`, `src/config/config.cpp`

### 结构体

```cpp
struct RabbitMQConfig {
    std::string hostname;      // 默认 "127.0.0.1"
    int         port;          // 默认 5672
    std::string vhost;         // 默认 "/"
    std::string username;      // 默认 "judge"
    std::string password;      // 默认 "judge_pass"
    std::string queue_name;    // 默认 "submission_queue"
};

struct MySQLConfig {
    std::string hostname;      // 默认 "127.0.0.1"
    int         port;          // 默认 3306
    std::string username;      // 默认 "judge"
    std::string password;      // 默认 "judge_pass"
    std::string database;      // 默认 "online_judge"
};

struct IsolateConfig {
    std::string binary_path;   // 默认 "/usr/local/bin/isolate"
    int         box_id_start;  // 默认 1
    int         num_boxes;     // 默认 5
};

struct JudgeConfig {
    int concurrency;           // 默认 1（预留字段）
};

struct AppConfig {
    RabbitMQConfig  rabbitmq;
    MySQLConfig     mysql;
    IsolateConfig   isolate;
    JudgeConfig     judge;
};
```

### 函数

```cpp
std::optional<AppConfig> load_config(const std::string& filepath);
```

- **参数**: JSON 配置文件路径
- **返回**: 成功返回 `AppConfig`，文件不存在或 JSON 解析失败返回 `std::nullopt`
- **行为**: 读取 JSON 文件，逐字段解析。未出现的字段使用默认值
- **异常**: 无，所有异常在内部捕获并转为 `nullopt` 返回

---

## 2. Status 模块

**文件**: `src/judge/status.h`, `src/judge/status.cpp`

### 枚举

```cpp
enum class JudgeStatus {
    Pending,              // 等待判题
    Compiling,            // 编译中（内部使用）
    Running,              // 运行中（内部使用）
    Accepted,             // 通过全部测试用例
    WrongAnswer,          // 输出与预期不符
    TimeLimitExceeded,    // 运行超时
    MemoryLimitExceeded,  // 内存超限
    RuntimeError,         // 运行时错误或非零退出
    CompilationError,     // 编译失败
    SystemError,          // 系统内部错误
};
```

### 函数

```cpp
// 枚举 → 数据库存储字符串（如 "Accepted"）
const char* to_string(JudgeStatus status);

// 枚举 → 人类可读描述（如 "All test cases passed"）
const char* to_description(JudgeStatus status);

// 数据库字符串 → 枚举（未知字符串返回 SystemError）
JudgeStatus status_from_string(const std::string& s);
```

---

## 3. Sandbox 模块

**文件**: `src/judge/sandbox.h`, `src/judge/sandbox.cpp`

### 结构体

```cpp
struct SandboxResult {
    int         exit_code;        // 进程退出码（0=成功）
    int         signal_num;       // 终止信号编号（0=无信号）
    int         time_used_ms;     // 运行时间（毫秒）
    int         memory_used_kb;   // 内存用量（KB）
    std::string stdout_output;    // 标准输出内容
    std::string stderr_output;    // 标准错误输出内容
    bool        timed_out;        // 是否超时
    std::string status;           // isolate 原始状态码（"OK", "TO", "XX"等）
};
```

### 类

```cpp
class Sandbox {
public:
    Sandbox(int box_id, const IsolateConfig& cfg);
    ~Sandbox();

    // 初始化沙箱盒子（isolate --init --box-id=N）
    bool init();

    // 清理沙箱盒子（isolate --cleanup --box-id=N）
    bool cleanup();

    // 在沙箱中编译 C++ 源代码
    // - 将 source_code 写入 box/solution.cpp
    // - 调用 isolate --run -- /usr/bin/g++ 编译
    // - 返回 SandboxResult，exit_code=0 表示编译成功
    SandboxResult compile(const std::string& source_code);

    // 在沙箱中运行已编译的程序
    // - time_limit_ms: 时间限制（毫秒），wall-time 设为 2x
    // - memory_limit_kb: 内存限制（KB，当前为预留参数）
    // - stdin_input: 标准输入内容
    // - 返回 SandboxResult，含 stdout/stderr/exit_code/time_used 等
    SandboxResult run(int time_limit_ms, int memory_limit_kb,
                      const std::string& stdin_input);

    int box_id() const;
};
```

### isolate 命令参考

**编译命令**:
```bash
isolate --box-id=N --env=PATH=/usr/bin:/bin --processes=50 --wall-time=30 \
        --meta=/var/local/lib/isolate/N/box/compile_meta.txt \
        --stderr=error.txt \
        --run -- /usr/bin/g++ -std=c++17 -O2 -Wall -o solution solution.cpp
```

**运行命令**:
```bash
isolate --box-id=N --env=PATH=/usr/bin:/bin --processes=1 \
        --wall-time=<2x time_limit> \
        --meta=/var/local/lib/isolate/N/box/run_meta.txt \
        --stdin=input.txt --stdout=output.txt --stderr=error.txt \
        --run -- ./solution
```

**Meta 文件格式**（key:value 行）:
```
time:0.269
time-wall:0.273
max-rss:75876
csw-voluntary:10
csw-forced:7
exitcode:0
```

---

## 4. JudgeEngine 模块

**文件**: `src/judge/judge_engine.h`, `src/judge/judge_engine.cpp`

### 结构体

```cpp
struct TestCase {
    std::string input;            // 标准输入
    std::string expected_output;  // 预期输出
};

struct SubmissionMessage {
    uint64_t submission_id;                  // 提交 ID
    uint64_t problem_id;                     // 题目 ID
    std::string language;                    // 语言（"cpp"）
    std::string code;                        // 源代码
    int time_limit_ms;                       // 时间限制（毫秒）
    int memory_limit_kb;                     // 内存限制（KB）
    std::vector<TestCase> testcases;         // 测试用例列表
};

struct JudgeResultInternal {
    JudgeStatus status;                      // 判题结果状态
    int time_used_ms;                        // 最大运行时间（毫秒）
    int memory_used_kb;                      // 最大内存使用（KB）
    std::string error_message;               // 错误信息
    std::string compiler_output;             // 编译器输出
    int failed_testcase_index;               // 失败用例索引（-1=无）
};
```

### 类

```cpp
class JudgeEngine {
public:
    // 构造函数：初始化 isolate_config 和沙箱资源池
    JudgeEngine(const IsolateConfig& isolate_cfg, int max_boxes);
    ~JudgeEngine();

    // 解析 JSON 消息字符串
    // - 参数: RabbitMQ 消息体（JSON 字符串）
    // - 返回: 成功返回 SubmissionMessage，JSON 无效或缺少必填字段返回 nullopt
    static std::optional<SubmissionMessage> parse_message(
        const std::string& json_str);

    // 执行完整判题流程
    // - 参数: 已解析的提交消息
    // - 返回: 判题结果（含状态、时间、内存、错误详情）
    // - 流程: 获取沙箱 → 编译 → 逐个用例运行比对 → 释放沙箱
    JudgeResultInternal judge(const SubmissionMessage& submission);

    // 输出比对（公开，供测试使用）
    // - 对两段字符串做空白规范化后逐行比较
    static bool compare_output(const std::string& expected,
                               const std::string& actual);

private:
    class BoxPool;  // 内部实现：沙箱资源池
    BoxPool box_pool_;
};
```

---

## 5. MySQLClient 模块

**文件**: `src/db/mysql_client.h`, `src/db/mysql_client.cpp`

### 结构体

```cpp
struct JudgeResult {
    uint64_t submission_id;
    std::string status;            // 判题状态字符串（如 "Accepted"）
    int time_used_ms;
    int memory_used_kb;
    std::string error_message;
    std::string compiler_output;
    int failed_testcase;           // -1 表示无失败用例
};
```

### 类

```cpp
class MySQLClient {
public:
    explicit MySQLClient(const MySQLConfig& cfg);
    ~MySQLClient();

    // 建立连接并预编译 UPDATE 语句
    bool connect();

    // 更新提交记录的判题结果
    // - 使用 prepared statement，防止 SQL 注入
    // - UPDATE submissions SET status=?, time_used_ms=?, ... WHERE submission_id=?
    bool update_submission(const JudgeResult& result);

    // 保持连接活跃
    // - 发送 ping，失败则自动重连
    bool ping();
};
```

---

## 6. RabbitMQClient 模块

**文件**: `src/mq/rabbitmq_client.h`, `src/mq/rabbitmq_client.cpp`

### 类

```cpp
class RabbitMQClient {
public:
    // 回调类型: (delivery_tag, message_body)
    using MessageCallback = std::function<void(uint64_t, const std::string&)>;

    explicit RabbitMQClient(const RabbitMQConfig& cfg);
    ~RabbitMQClient();

    // 建立连接，打开信道，声明持久队列，设置 QoS(prefetch=1)
    bool connect();

    // 阻塞消费循环
    // - 每收到一条消息调用 callback(delivery_tag, body)
    // - 使用 1 秒超时轮询以支持优雅关闭
    // - 返回 false 表示连接致命错误
    bool consume(const MessageCallback& callback);

    // 确认单条消息
    bool ack(uint64_t delivery_tag);

    // 拒绝消息并重新入队
    bool nack_requeue(uint64_t delivery_tag);

    bool is_connected() const;
};
```

**AMQP 配置**:
- QoS prefetch = 1（公平分发，每个消费者每次只取一条）
- 队列声明为 durable（持久化）
- 消息 delivery_mode = 2（持久化）
- 消费者手动 ACK（no_ack = false）

---

## 7. Worker 模块

**文件**: `src/worker/worker.h`, `src/worker/worker.cpp`

### 类

```cpp
class Worker {
public:
    explicit Worker(const AppConfig& cfg);
    ~Worker();

    // 启动主循环（阻塞）
    // - 连接 MySQL（重试直到成功或 shutdown）
    // - 连接 RabbitMQ（重试直到成功或 shutdown）
    // - 进入消费循环，每收到消息调用 on_message()
    // - 收到 SIGINT/SIGTERM 后优雅退出
    int run();

    // 请求优雅关闭
    void shutdown();

private:
    void on_message(uint64_t delivery_tag, const std::string& body);
};
```

### `on_message()` 处理流程

```
1. parse_message(body)
   └─ 失败 → ACK（丢弃畸形消息）→ 返回

2. judge_engine_->judge(submission)
   └─ 编译 → 逐个用例运行 → 比对

3. db_->ping()
   └─ 保持 MySQL 连接活跃

4. db_->update_submission(result)
   └─ 失败 → NACK requeue（消息重新入队）→ 返回

5. mq_->ack(delivery_tag)
   └─ 成功 → ACK 确认
```

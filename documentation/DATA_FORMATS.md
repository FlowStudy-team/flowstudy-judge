# 数据格式规范

本文档描述项目中所有涉及数据交换的格式定义。

---

## 1. RabbitMQ 消息格式

### 消息 JSON 结构

```json
{
    "submission_id": 12345,
    "problem_id": 100,
    "language": "cpp",
    "code": "#include <iostream>\nint main() {\n    int a, b;\n    std::cin >> a >> b;\n    std::cout << a + b << std::endl;\n    return 0;\n}",
    "time_limit": 1000,
    "memory_limit": 262144,
    "testcases": [
        {
            "input": "1 2",
            "expected_output": "3"
        },
        {
            "input": "5 7",
            "expected_output": "12"
        }
    ]
}
```

### 字段说明

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:--:|--------|------|
| `submission_id` | uint64 | 是 | — | 对应数据库 submissions 表的主键 |
| `problem_id` | uint64 | 是 | — | 题目 ID |
| `language` | string | 否 | `"cpp"` | 编程语言标识 |
| `code` | string | 是 | — | 源代码全文 |
| `time_limit` | int | 否 | 1000 | 时间限制（毫秒） |
| `memory_limit` | int | 否 | 262144 | 内存限制（KB） |
| `testcases` | array | 是 | — | 测试用例列表 |
| `testcases[].input` | string | 是 | — | 标准输入内容 |
| `testcases[].expected_output` | string | 是 | — | 预期标准输出 |

### AMQP 属性

- **exchange**: 空字符串（使用默认 exchange）
- **routing_key**: 队列名（`submission_queue`）
- **content_type**: `application/json`
- **delivery_mode**: 2（消息持久化）

---

## 2. 数据库表结构

### 表名: `submissions`

```sql
CREATE TABLE submissions (
    submission_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id      BIGINT UNSIGNED NOT NULL,
    language        VARCHAR(20)     NOT NULL DEFAULT 'cpp',
    code            MEDIUMTEXT      NOT NULL,
    time_limit_ms   INT UNSIGNED    NOT NULL DEFAULT 1000,
    memory_limit_kb INT UNSIGNED    NOT NULL DEFAULT 262144,
    status          VARCHAR(30)     NOT NULL DEFAULT 'Pending',
    time_used_ms    INT UNSIGNED    NOT NULL DEFAULT 0,
    memory_used_kb  INT UNSIGNED    NOT NULL DEFAULT 0,
    error_message   TEXT,
    compiler_output TEXT,
    failed_testcase INT             NOT NULL DEFAULT -1,
    created_at      DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    judged_at       DATETIME,

    INDEX idx_problem  (problem_id),
    INDEX idx_status   (status),
    INDEX idx_created  (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

### 字段详细说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `submission_id` | BIGINT UNSIGNED PK | 提交 ID，由前端 INSERT 时分配 |
| `problem_id` | BIGINT UNSIGNED | 题目 ID |
| `language` | VARCHAR(20) | 编程语言标识 |
| `code` | MEDIUMTEXT | 源代码全文（最大 16MB） |
| `time_limit_ms` | INT UNSIGNED | 时间限制（毫秒） |
| `memory_limit_kb` | INT UNSIGNED | 内存限制（KB） |
| `status` | VARCHAR(30) | 当前判题状态 |
| `time_used_ms` | INT UNSIGNED | 所有测试用例中的最大运行时间 |
| `memory_used_kb` | INT UNSIGNED | 所有测试用例中的最大内存使用 |
| `error_message` | TEXT | 错误描述（RE 信号、TLE 信息等） |
| `compiler_output` | TEXT | g++ 编译错误输出（CE 时使用） |
| `failed_testcase` | INT | 失败用例索引（0-based），-1 表示无 |
| `created_at` | DATETIME | 提交时间（前端写入） |
| `judged_at` | DATETIME | 判题完成时间（Worker 写入） |

### 判题状态值

| 状态值 | 含义 | 设置时机 |
|--------|------|----------|
| `Pending` | 等待判题 | 前端插入 |
| `Compiling` | 编译中 | Worker 内部使用（不写 DB） |
| `Running` | 运行中 | Worker 内部使用（不写 DB） |
| `Accepted` | 通过全部测试 | Worker：所有用例比对通过 |
| `WrongAnswer` | 答案错误 | Worker：输出与预期不匹配 |
| `TimeLimitExceeded` | 超时 | Worker：isolate 返回 status=TO |
| `MemoryLimitExceeded` | 内存超限 | Worker：max-rss > memory_limit |
| `RuntimeError` | 运行错误 | Worker：非零退出码或信号终止 |
| `CompilationError` | 编译失败 | Worker：g++ 返回非零退出码 |
| `SystemError` | 系统错误 | Worker：isolate 异常或未知错误 |

---

## 3. 配置文件格式

### `config.json`

```json
{
    "rabbitmq": {
        "hostname": "127.0.0.1",
        "port": 5672,
        "vhost": "/",
        "username": "judge",
        "password": "judge_pass",
        "queue_name": "submission_queue"
    },
    "mysql": {
        "hostname": "127.0.0.1",
        "port": 3306,
        "username": "judge",
        "password": "judge_pass",
        "database": "online_judge"
    },
    "isolate": {
        "binary_path": "/usr/local/bin/isolate",
        "box_id_start": 0,
        "num_boxes": 1
    },
    "judge": {
        "concurrency": 1
    }
}
```

所有字段均可选，缺失时使用结构体默认值。各配置项的含义参见 [API.md](API.md) 中 Config 模块的结构体定义。

---

## 4. MySQL UPDATE 语句

Worker 使用以下 prepared statement 更新判题结果：

```sql
UPDATE submissions SET
    status          = ?,    -- VARCHAR
    time_used_ms    = ?,    -- INT
    memory_used_kb  = ?,    -- INT
    error_message   = ?,    -- TEXT
    compiler_output = ?,    -- TEXT
    failed_testcase = ?,    -- INT
    judged_at       = NOW()
WHERE submission_id = ?     -- BIGINT UNSIGNED
```

参数按 bind 顺序：
1. status (string)
2. time_used_ms (int)
3. memory_used_kb (int)
4. error_message (string)
5. compiler_output (string)
6. failed_testcase (int)
7. submission_id (uint64)

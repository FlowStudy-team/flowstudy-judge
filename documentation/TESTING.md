# 测试过程记录

本文档详细记录了项目的测试流程、在测试过程中遇到的问题及逐步修复的过程。

---

## 一、测试架构

### 测试分层

```
┌─────────────────────────────────┐
│  端到端测试 (test_publisher)     │  ← 完整 RabbitMQ → Worker → MySQL 链路
├─────────────────────────────────┤
│  集成测试 (test_runner)          │  ← 模块组合测试（状态、比对、解析）
├─────────────────────────────────┤
│  沙箱单元测试 (sandbox_test)     │  ← isolate 命令行交互验证
└─────────────────────────────────┘
```

### 测试目标

端到端测试覆盖 5 种判题结果：

| # | 代码文件 | 行为 | 预期 status |
|---|----------|------|-------------|
| 1 | `test/test_codes/ac.cpp` | 正确计算 a+b | `Accepted` |
| 2 | `test/test_codes/wa.cpp` | 输出 a\*b 而非 a+b | `WrongAnswer` |
| 3 | `test/test_codes/re.cpp` | 除以零 | `RuntimeError` |
| 4 | `test/test_codes/tle.cpp` | 死循环（time_limit=500ms） | `TimeLimitExceeded` |
| 5 | `test/test_codes/ce.cpp` | 语法错误 | `CompilationError` |

---

## 二、测试环境准备

### 2.1 依赖安装

安装过程遇到以下问题：

**问题 1: isolate 编译缺少依赖**

```
fatal error: seccomp.h: No such file or directory
fatal error: sys/capability.h: No such file or directory
fatal error: systemd/sd-daemon.h: No such file or directory
make: a2x: No such file or directory
```

**修复**: 安装缺失的开发包
```bash
sudo apt install -y libsystemd-dev libseccomp-dev libcap-dev asciidoc-base
```

**问题 2: isolate --init 缺少专用用户**

```
User isolate not found in /etc/subuid
```

**修复**: 创建 isolate 系统用户并添加从属 UID/GID 映射
```bash
sudo useradd -r -s /bin/false isolate
echo "isolate:100000:65536" | sudo tee -a /etc/subuid
echo "isolate:100000:65536" | sudo tee -a /etc/subgid
```

### 2.2 RabbitMQ + MySQL 服务安装

开发库（librabbitmq-dev、libmysqlclient-dev）只提供编译时头文件和链接库，运行端到端测试还需要实际的服务端进程。

```bash
sudo apt install -y rabbitmq-server mysql-server
sudo service rabbitmq-server start
sudo service mysql start
```

---

## 三、单元测试

### 3.1 编译与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/test/test_runner
```

### 3.2 单元测试结果

```
Results: 34 passed, 0 failed
```

覆盖内容：
- 状态枚举双向转换（`to_string` + `status_from_string`）
- 输出比对算法（identical / trailing whitespace / blank lines / different / empty）
- JSON 消息解析（valid / invalid / missing field）
- 配置加载（nonexistent file / default values）

---

## 四、端到端测试：问题与修复

端到端测试是本次开发中最复杂的阶段。以下按时间顺序记录遇到的问题及逐步修复过程。

### 问题 1: 多个旧 Worker 进程并存

**现象**:
```
pgrep -f judge_worker
28177
28672
29934
```

多次启动 Worker 导致多个旧进程残留，它们都在消费同一队列、使用同一沙箱，互相冲突。

**修复**: 显式 kill 所有 Worker 进程
```bash
kill -9 28177 28672 29934
```

**教训**: 后台进程管理需要更规范的方式（PID 文件、进程名区分等）。

---

### 问题 2: isolate 报 "This box belongs to a different user"

**现象**:
```
This box belongs to a different user (uid 0)
[error] Failed to init sandbox 0
```

**原因**: `isolate --init` 由 root 执行，创建的 box 锁定在 uid 0。Worker 以 dhc (uid 1000) 运行时无法使用。

**修复步骤**:
```bash
# 1. root 清理 box
sudo /usr/local/bin/isolate --cleanup --box-id=0

# 2. dhc 重新初始化
isolate --init --box-id=0

# 3. 开放 box 目录写权限
chmod 777 /var/local/lib/isolate/0/box
```

---

### 问题 3: 数据库表不存在

**现象**:
```
mysql_stmt_prepare failed: Table 'online_judge.submissions' doesn't exist
```

**原因**: 只安装了 MySQL Server，未导入建表 DDL。

**修复**:
```bash
mysql -u judge -pjudge_pass online_judge < scripts/setup_db.sql
```

---

### 问题 4: Sandbox 析构函数调用 cleanup() 破坏沙箱

**现象**: 处理完第一条消息后，后续消息全部报 "Failed to write source file"。

**根因分析**:

`Sandbox` 的析构函数原来调用了 `cleanup()`:
```cpp
Sandbox::~Sandbox() {
    cleanup();  // BUG! 这会执行 isolate --cleanup，销毁沙箱
}
```

`JudgeEngine::judge()` 内部创建了临时的 `Sandbox tmp_sandbox(box_id, cfg)`。当函数返回时，`tmp_sandbox` 析构 → 调用 `cleanup()` → 整个 box 被删除 → 下一个提交无法使用。

**修复**: 移除析构函数中的 `cleanup()` 调用。沙箱的生命周期由 `BoxPool` 统一管理:
```cpp
Sandbox::~Sandbox() {
    // 不再自动调用 cleanup()
}
```

---

### 问题 5: WSL2 cgroups 不可用

**现象**:
```
Cannot open /run/isolate/cgroup: No such file or directory
```

**原因**: WSL2 的 cgroups 配置不完整，`isolate --cg` 无法创建 cgroup。

**修复**: 移除 isolate 命令中的 `--cg`、`--cg-timing`、`--cg-mem` 参数。

编译命令 `--cg` → 移除：
```cpp
// 修改前
"--cg",
"--processes=50",

// 修改后
"--processes=50",
```

运行命令 `--cg`、`--cg-timing`、`--cg-mem` → 移除：
```cpp
// 修改前
"--cg",
"--cg-timing",
"--cg-mem=" + std::to_string(static_cast<int>(mem_limit_mb)),

// 修改后
// 全部移除
```

**影响**: 无 cgroups 时无法精确限制内存，但 wall-time 限制仍然有效。内存使用通过 meta 文件的 `max-rss` 字段监控。

---

### 问题 6: 沙箱内找不到 ld 链接器

**现象**:
```
collect2: fatal error: cannot find 'ld'
compilation terminated.
```

**根因分析**: isolate 创建沙箱时，内部环境变量仅有 `LIBC_FATAL_STDERR_=1`，`PATH` 为空。g++ 的 `collect2` 子进程通过 `PATH` 查找 `ld`，但因 PATH 为空而失败。

**验证过程**:
```bash
# 查看沙箱内部环境
isolate --box-id=0 --run -- /usr/bin/env
# 输出: LIBC_FATAL_STDERR_=1
# PATH 完全不存在！
```

**修复**: 在 isolate 命令中添加 `--env=PATH=/usr/bin:/bin`：
```cpp
"--env=PATH=/usr/bin:/bin",
```

---

### 问题 7: --meta 参数位置错误，被传给 g++

**现象**:
```
g++: error: unrecognized command-line option '--meta=/var/local/lib/isolate/0/box/compile_meta.txt'
```

**根因分析**: `execute()` 方法在 args 遍历结束后才追加 `--meta`，此时已经过了 `--run --` 分隔符，`--meta` 被当作 g++ 的参数。

原来的命令构建：
```cpp
cmd << isolate_bin_;
for (const auto& a : args) {
    cmd << " " << a;          // 遍历到 -- /usr/bin/g++ ... 后结束
}
cmd << " --meta=" << ...;     // 此时追加在了 g++ 参数后面！
```

实际生成的命令：
```
isolate ... --run -- /usr/bin/g++ ... --meta=/path/meta.txt
                                        ^^^^^^^^^^^^^^^^^^^^^^^^
                                        这部分被 g++ 当作自己的参数
```

**修复**: 在 args 中检测到 `--run` 时插入 `--meta`，确保它在 `--` 之前（作为 isolate 的参数）：
```cpp
for (const auto& a : args) {
    if (!meta_added && a == "--run") {
        cmd << " --meta=" << box_path_ + meta_filename;
        meta_added = true;
    }
    cmd << " " << a;
}
```

实际生成的命令：
```
isolate ... --meta=/path/meta.txt --run -- /usr/bin/g++ ...
              ^^^^^^^^^^^^^^^^^^^ 在 -- 之前，是 isolate 的参数
```

---

### 问题 8: --stderr/--stdout 使用绝对路径导致文件无法写入

**现象**:
```
open("/var/local/lib/isolate/0/box/ce.txt"): No such file or directory
```

**根因分析**: 沙箱内的文件系统视图与宿主机不同。在沙箱内部，box 目录（`/var/local/lib/isolate/0/box/`）被映射为根目录 `/`。

- `--meta` 使用绝对路径 → isolate 本身在宿主机上写入 meta 文件 → 需要宿主机的绝对路径 ✓
- `--stderr`/`--stdout`/`--stdin` → 由沙箱内的进程打开 → 需要沙箱内的相对路径 ✓

**修复**: 对 `--stderr`、`--stdout`、`--stdin` 使用相对路径（仅文件名），对 `--meta` 使用宿主机绝对路径：

```cpp
// compile 命令
"--stderr=error.txt",                                    // 相对路径，沙箱内写入
"--meta=" + box_path_ + "compile_meta.txt",              // 绝对路径，宿主机写入

// run 命令  
"--stdin=input.txt",                                     // 相对路径，沙箱内读取
"--stdout=output.txt",                                   // 相对路径，沙箱内写入
"--stderr=error.txt",                                    // 相对路径，沙箱内写入
"--meta=" + box_path_ + "run_meta.txt",                  // 绝对路径，宿主机写入
```

验证测试程序 `sandbox_test` 确认了这两种路径的语义差异。

---

## 五、最终端到端测试结果

修复所有问题后，端到端测试全部通过：

```
=== Worker log ===
[1] Compiling...
[1] Running testcase 1/3
[1] Running testcase 2/3
[1] Running testcase 3/3
[1] Accepted (time: 3ms, memory: 3840KB)
[1] Done: Accepted

[2] Compiling...
[2] Running testcase 1/3
[2] Wrong answer on testcase 1
[2] Done: WrongAnswer

[3] Compiling...
[3] Running testcase 1/3
[3] Runtime error on testcase 1
[3] Done: RuntimeError

[4] Compiling...
[4] Running testcase 1/3
[4] Time limit exceeded on testcase 1
[4] Done: TimeLimitExceeded

[5] Compiling...
[5] Compilation error
[5] Done: CompilationError
```

### 数据库验证

```sql
SELECT submission_id, status, time_used_ms, memory_used_kb
FROM submissions WHERE submission_id BETWEEN 1 AND 5;

submission_id  status              time_used_ms  memory_used_kb
1              Accepted            3             3840
2              WrongAnswer         2             3840
3              RuntimeError        2             1668
4              TimeLimitExceeded   5100          1540
5              CompilationError    0             0
```

5/5 测试通过，符合预期。

---

## 六、问题总结与经验

| # | 问题类别 | 问题 | 根因 | 修复方式 |
|---|----------|------|------|----------|
| 1 | 环境 | isolate 编译缺少依赖 | 系统未安装 libseccomp-dev 等 | apt install 缺失包 |
| 2 | 环境 | isolate --init 缺少用户 | 未创建 isolate 用户和 subuid | useradd + /etc/subuid 配置 |
| 3 | 运行时 | 多 Worker 并存冲突 | 多次启动未清理旧进程 | kill 旧进程 |
| 4 | 运行时 | box 属于不同用户 | root 创建 box 但 Worker 以普通用户运行 | 以 Worker 用户重新 init |
| 5 | 代码 | 析构函数调用 cleanup() | Sandbox 生命周期管理不当 | 移除析构中的 cleanup，由 BoxPool 管理 |
| 6 | 环境 | cgroups 不可用 | WSL2 cgroups 配置不完整 | 移除 --cg 相关参数 |
| 7 | 环境 | 沙箱内 PATH 为空 | isolate 默认不设置环境变量 | 添加 --env=PATH=/usr/bin:/bin |
| 8 | 代码 | --meta 位置错误 | 参数拼接到 -- 分离符之后 | 在 --run 之前插入 --meta |
| 9 | 代码 | 文件路径作用域混淆 | 沙箱内外路径空间不同 | --meta 用宿主机绝对路径，--stdout/stderr 用相对路径 |

### 核心教训

1. **沙箱内外是两个文件系统空间**: isolate 的 `--meta` 在宿主机上执行，`--stdout`/`--stderr`/`--stdin` 在沙箱内执行，路径语义完全不同
2. **进程生命周期管理**: 后台服务的停止/重启需要可靠的 PID 管理机制
3. **资源池模式的正确实现**: 不应在临时对象中销毁共享资源
4. **WSL2 的局限性**: cgroups 支持不完整，需要降级处理

#include "judge/judge_engine.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// ==================== BoxPool ====================

JudgeEngine::BoxPool::BoxPool(const IsolateConfig& cfg, int count) {
    for (int i = 0; i < count; ++i) {
        int box_id = cfg.box_id_start + i;
        auto box = std::make_unique<Sandbox>(box_id, cfg);
        if (box->init()) {
            boxes_.push_back(std::move(box));
            available_.push(box_id);
        } else {
            spdlog::error("Failed to init box {}", box_id);
        }
    }
    spdlog::info("BoxPool: {}/{} boxes ready", available_.size(), count);
}

JudgeEngine::BoxPool::~BoxPool() {
    for (auto& box : boxes_) {
        box->cleanup();
    }
}

int JudgeEngine::BoxPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty(); });
    int id = available_.front();
    available_.pop();
    return id;
}

void JudgeEngine::BoxPool::release(int box_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    available_.push(box_id);
    cv_.notify_one();
}

// ==================== JudgeEngine ====================

JudgeEngine::JudgeEngine(const IsolateConfig& isolate_cfg, int max_boxes)
    : isolate_cfg_(isolate_cfg)
    , box_pool_(isolate_cfg, max_boxes)
{
}

JudgeEngine::~JudgeEngine() = default;

std::optional<SubmissionMessage> JudgeEngine::parse_message(
    const std::string& json_str) {
    try {
        nlohmann::json j = nlohmann::json::parse(json_str);
        SubmissionMessage msg;

        msg.submission_id = j.at("submission_id");
        msg.problem_id = j.at("problem_id");
        msg.language = j.value("language", "cpp");
        msg.code = j.at("code");
        msg.time_limit_ms = j.value("time_limit", 1000);
        msg.memory_limit_kb = j.value("memory_limit", 262144);

        for (const auto& tc : j.at("testcases")) {
            TestCase t;
            t.testcase_id = tc.value("testcase_id", 0);
            t.case_index = tc.value("case_index", static_cast<int>(msg.testcases.size()) + 1);
            t.input = tc.at("input");
            t.expected_output = tc.at("expected_output");
            msg.testcases.push_back(std::move(t));
        }

        return msg;

    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse submission message: {}", e.what());
        return std::nullopt;
    }
}

JudgeResultInternal JudgeEngine::judge(const SubmissionMessage& submission) {
    JudgeResultInternal result;

    int box_id = box_pool_.acquire();
    Sandbox sandbox(box_id, isolate_cfg_);
    // Note: the box is already initialized by BoxPool, we reuse the same box_id

    // Re-create sandbox wrapper for this box ID
    // Actually we need the box from BoxPool. Let's use the box_id to reference
    // the initialized box in BoxPool. The sandbox init/cleanup is handled by BoxPool.
    // We create a temporary sandbox for the compile/run operations.
    Sandbox tmp_sandbox(box_id, isolate_cfg_);

    // === Compile ===
    spdlog::info("[{}] Compiling...", submission.submission_id);
    auto compile_result = tmp_sandbox.compile(submission.code);

    if (compile_result.exit_code != 0) {
        result.status = JudgeStatus::CompilationError;
        result.compiler_output = compile_result.stderr_output;
        result.error_message = compile_result.stderr_output;
        spdlog::info("[{}] Compilation error", submission.submission_id);
        box_pool_.release(box_id);
        return result;
    }

    // === Run test cases ===
    int max_time_ms = 0;
    int max_memory_kb = 0;

    for (size_t i = 0; i < submission.testcases.size(); ++i) {
        const auto& tc = submission.testcases[i];
        JudgeCaseResultInternal case_result;
        case_result.testcase_id = tc.testcase_id;
        case_result.case_index = tc.case_index > 0 ? tc.case_index : static_cast<int>(i) + 1;
        case_result.expected_output = tc.expected_output;
        spdlog::info("[{}] Running testcase {}/{}", submission.submission_id,
                     i + 1, submission.testcases.size());

        auto run_result = tmp_sandbox.run(submission.time_limit_ms,
                                          submission.memory_limit_kb,
                                          tc.input);

        // Track max resource usage
        if (run_result.time_used_ms > max_time_ms) {
            max_time_ms = run_result.time_used_ms;
        }
        if (run_result.memory_used_kb > max_memory_kb) {
            max_memory_kb = run_result.memory_used_kb;
        }
        case_result.time_used_ms = run_result.time_used_ms;
        case_result.memory_used_kb = run_result.memory_used_kb;
        case_result.actual_output = run_result.stdout_output;

        // Check for timeout
        if (run_result.timed_out || run_result.status == "TO") {
            result.status = JudgeStatus::TimeLimitExceeded;
            result.time_used_ms = max_time_ms;
            result.memory_used_kb = max_memory_kb;
            result.failed_testcase_index = static_cast<int>(i);
            result.error_message = "Time limit exceeded on testcase " + std::to_string(i + 1);
            case_result.status = JudgeStatus::TimeLimitExceeded;
            case_result.error_message = result.error_message;
            result.case_results.push_back(std::move(case_result));
            spdlog::info("[{}] Time limit exceeded on testcase {}",
                         submission.submission_id, i + 1);
            box_pool_.release(box_id);
            return result;
        }

        // Check for memory limit
        if (run_result.memory_used_kb > submission.memory_limit_kb) {
            result.status = JudgeStatus::MemoryLimitExceeded;
            result.time_used_ms = max_time_ms;
            result.memory_used_kb = max_memory_kb;
            result.failed_testcase_index = static_cast<int>(i);
            result.error_message = "Memory limit exceeded on testcase " + std::to_string(i + 1)
                                   + " (" + std::to_string(run_result.memory_used_kb) + " KB)";
            case_result.status = JudgeStatus::MemoryLimitExceeded;
            case_result.error_message = result.error_message;
            result.case_results.push_back(std::move(case_result));
            spdlog::info("[{}] Memory limit exceeded", submission.submission_id);
            box_pool_.release(box_id);
            return result;
        }

        // Check for runtime error
        if (run_result.exit_code != 0 || run_result.signal_num != 0) {
            result.status = JudgeStatus::RuntimeError;
            result.time_used_ms = max_time_ms;
            result.memory_used_kb = max_memory_kb;
            result.failed_testcase_index = static_cast<int>(i);
            if (run_result.signal_num != 0) {
                result.error_message = "Program terminated by signal "
                                       + std::to_string(run_result.signal_num)
                                       + " on testcase " + std::to_string(i + 1);
            } else {
                result.error_message = "Non-zero exit code "
                                       + std::to_string(run_result.exit_code)
                                       + " on testcase " + std::to_string(i + 1) + "\n"
                                       + run_result.stderr_output;
            }
            case_result.status = JudgeStatus::RuntimeError;
            case_result.error_message = result.error_message;
            result.case_results.push_back(std::move(case_result));
            spdlog::info("[{}] Runtime error on testcase {}", submission.submission_id, i + 1);
            box_pool_.release(box_id);
            return result;
        }

        // Check output
        if (!compare_output(tc.expected_output, run_result.stdout_output)) {
            result.status = JudgeStatus::WrongAnswer;
            result.time_used_ms = max_time_ms;
            result.memory_used_kb = max_memory_kb;
            result.failed_testcase_index = static_cast<int>(i);
            result.error_message = "Wrong answer on testcase " + std::to_string(i + 1);
            case_result.status = JudgeStatus::WrongAnswer;
            case_result.error_message = result.error_message;
            result.case_results.push_back(std::move(case_result));
            spdlog::info("[{}] Wrong answer on testcase {}", submission.submission_id, i + 1);
            spdlog::debug("  Expected: '{}'", tc.expected_output);
            spdlog::debug("  Got:      '{}'", run_result.stdout_output);
            box_pool_.release(box_id);
            return result;
        }

        case_result.status = JudgeStatus::Accepted;
        result.case_results.push_back(std::move(case_result));
    }

    // All test cases passed
    result.status = JudgeStatus::Accepted;
    result.time_used_ms = max_time_ms;
    result.memory_used_kb = max_memory_kb;
    spdlog::info("[{}] Accepted (time: {}ms, memory: {}KB)",
                 submission.submission_id, max_time_ms, max_memory_kb);
    box_pool_.release(box_id);
    return result;
}

// ---------- output comparison ----------

static std::string normalize_line(const std::string& line) {
    // Trim trailing whitespace
    auto end = line.find_last_not_of(" \t\r");
    if (end == std::string::npos) return "";
    return line.substr(0, end + 1);
}

bool JudgeEngine::compare_output(const std::string& expected,
                                 const std::string& actual) {
    // Split both into lines
    auto split_lines = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> lines;
        std::istringstream ss(s);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(normalize_line(line));
        }
        // Remove trailing empty lines
        while (!lines.empty() && lines.back().empty()) {
            lines.pop_back();
        }
        return lines;
    };

    auto exp_lines = split_lines(expected);
    auto act_lines = split_lines(actual);

    if (exp_lines.size() != act_lines.size()) return false;

    for (size_t i = 0; i < exp_lines.size(); ++i) {
        if (exp_lines[i] != act_lines[i]) return false;
    }

    return true;
}

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "config/config.h"
#include "judge/sandbox.h"
#include "judge/status.h"

struct TestCase {
    uint64_t testcase_id = 0;
    int case_index = 0;
    std::string input;
    std::string expected_output;
};

struct SubmissionMessage {
    uint64_t submission_id = 0;
    uint64_t problem_id = 0;
    std::string language;
    std::string code;
    int time_limit_ms = 1000;
    int memory_limit_kb = 262144;
    std::vector<TestCase> testcases;
};

struct JudgeCaseResultInternal {
    uint64_t testcase_id = 0;
    int case_index = 0;
    JudgeStatus status = JudgeStatus::SystemError;
    int time_used_ms = 0;
    int memory_used_kb = 0;
    std::string actual_output;
    std::string expected_output;
    std::string error_message;
};

struct JudgeResultInternal {
    JudgeStatus status = JudgeStatus::SystemError;
    int time_used_ms = 0;
    int memory_used_kb = 0;
    std::string error_message;
    std::string compiler_output;
    int failed_testcase_index = -1;
    std::vector<JudgeCaseResultInternal> case_results;
};

class JudgeEngine {
public:
    JudgeEngine(const IsolateConfig& isolate_cfg, int max_boxes);
    ~JudgeEngine();

    JudgeEngine(const JudgeEngine&) = delete;
    JudgeEngine& operator=(const JudgeEngine&) = delete;

    static std::optional<SubmissionMessage> parse_message(
        const std::string& json_str);

    JudgeResultInternal judge(const SubmissionMessage& submission);

    static bool compare_output(const std::string& expected,
                               const std::string& actual);

private:
    class BoxPool {
    public:
        BoxPool(const IsolateConfig& cfg, int count);
        ~BoxPool();
        int acquire();
        void release(int box_id);
    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        std::queue<int> available_;
        std::vector<std::unique_ptr<Sandbox>> boxes_;
    };

    IsolateConfig isolate_cfg_;
    BoxPool box_pool_;
};

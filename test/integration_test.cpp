#include <cassert>
#include <sstream>
#include <string>

#include "config/config.h"
#include "judge/judge_engine.h"
#include "judge/status.h"

extern void check(const std::string& name, bool condition, const std::string& detail = "");

void test_status_conversion() {
    // to_string
    check("to_string(Accepted)", std::string(to_string(JudgeStatus::Accepted)) == "Accepted");
    check("to_string(WrongAnswer)", std::string(to_string(JudgeStatus::WrongAnswer)) == "WrongAnswer");
    check("to_string(TLE)", std::string(to_string(JudgeStatus::TimeLimitExceeded)) == "TimeLimitExceeded");
    check("to_string(MLE)", std::string(to_string(JudgeStatus::MemoryLimitExceeded)) == "MemoryLimitExceeded");
    check("to_string(RE)", std::string(to_string(JudgeStatus::RuntimeError)) == "RuntimeError");
    check("to_string(CE)", std::string(to_string(JudgeStatus::CompilationError)) == "CompilationError");
    check("to_string(SystemError)", std::string(to_string(JudgeStatus::SystemError)) == "SystemError");

    // status_from_string
    check("from_string -> Accepted", status_from_string("Accepted") == JudgeStatus::Accepted);
    check("from_string -> WrongAnswer", status_from_string("WrongAnswer") == JudgeStatus::WrongAnswer);
    check("from_string -> Unknown", status_from_string("NotAStatus") == JudgeStatus::SystemError);

    // to_description
    check("to_description", std::string(to_description(JudgeStatus::Accepted)) == "All test cases passed");
}

void test_compare_output() {
    // Identical output
    check("identical", JudgeEngine::compare_output("3\n", "3\n"));

    // Trailing whitespace on each line
    check("trailing ws", JudgeEngine::compare_output("3  \n12\n", "3\n12  \n"));

    // Trailing blank lines
    check("trailing blank", JudgeEngine::compare_output("3\n12\n", "3\n12\n\n\n"));

    // Different values
    check("different", !JudgeEngine::compare_output("3\n", "4\n"));

    // Different number of lines
    check("diff lines", !JudgeEngine::compare_output("3\n12\n", "3\n"));

    // Empty strings
    check("both empty", JudgeEngine::compare_output("", ""));
}

void test_parse_message() {
    std::string valid_json = R"({
        "submission_id": 12345,
        "problem_id": 100,
        "language": "cpp",
        "code": "#include <iostream>\nint main() { return 0; }",
        "time_limit": 1000,
        "memory_limit": 262144,
        "testcases": [
            {"input": "1 2", "expected_output": "3"},
            {"input": "5 7", "expected_output": "12"}
        ]
    })";

    auto result = JudgeEngine::parse_message(valid_json);
    check("parse valid msg", result.has_value());
    if (result) {
        auto& msg = *result;
        check("submission_id", msg.submission_id == 12345);
        check("problem_id", msg.problem_id == 100);
        check("language", msg.language == "cpp");
        check("testcases count", msg.testcases.size() == 2);
        check("tc[0] input", msg.testcases[0].input == "1 2");
        check("tc[1] expected", msg.testcases[1].expected_output == "12");
        check("defaults", msg.time_limit_ms == 1000 && msg.memory_limit_kb == 262144);
    }

    // Invalid JSON
    auto invalid = JudgeEngine::parse_message("not json");
    check("parse invalid msg", !invalid.has_value());

    // Missing required field
    std::string missing_code = R"({
        "submission_id": 1,
        "problem_id": 1,
        "testcases": []
    })";
    auto missing = JudgeEngine::parse_message(missing_code);
    check("parse missing code", !missing.has_value());
}

void test_config_loading() {
    // Test with non-existent file
    auto cfg = load_config("/nonexistent/config.json");
    check("load nonexistent", !cfg.has_value());

    // Test with defaults (real config not needed for unit test)
    // The load_config function should return AppConfig with defaults when fields are missing
    // We can't fully test without a file, but we can verify the struct compiles
    AppConfig default_cfg;
    check("default rabbitmq host", default_cfg.rabbitmq.hostname == "127.0.0.1");
    check("default rabbitmq port", default_cfg.rabbitmq.port == 5672);
    check("default mysql db", default_cfg.mysql.database == "online_judge");
    check("default isolate bin", default_cfg.isolate.binary_path == "/usr/local/bin/isolate");
    check("default boxes", default_cfg.isolate.num_boxes == 5);
    check("default concurrency", default_cfg.judge.concurrency == 1);
}

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/config.h"

struct JudgeCaseResult {
    uint64_t testcase_id = 0;
    int case_index = 0;
    std::string status;
    int time_used_ms = 0;
    int memory_used_kb = 0;
    std::string actual_output;
    std::string expected_output;
    std::string error_message;
};

struct JudgeResult {
    uint64_t submission_id = 0;
    uint64_t problem_id = 0;
    std::string status;
    int time_used_ms = 0;
    int memory_used_kb = 0;
    std::string error_message;
    std::string compiler_output;
    int failed_testcase = -1;
    std::vector<JudgeCaseResult> case_results;
};

class MySQLClient {
public:
    explicit MySQLClient(const MySQLConfig& cfg);
    ~MySQLClient();

    MySQLClient(const MySQLClient&) = delete;
    MySQLClient& operator=(const MySQLClient&) = delete;
    MySQLClient(MySQLClient&&) noexcept;
    MySQLClient& operator=(MySQLClient&&) noexcept;

    bool connect();
    bool update_submission(const JudgeResult& result);
    bool ping();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

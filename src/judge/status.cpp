#include "judge/status.h"

#include <unordered_map>

const char* to_string(JudgeStatus status) {
    switch (status) {
    case JudgeStatus::Pending:              return "PENDING";
    case JudgeStatus::Compiling:            return "COMPILING";
    case JudgeStatus::Running:              return "RUNNING";
    case JudgeStatus::Accepted:             return "ACCEPTED";
    case JudgeStatus::WrongAnswer:          return "WRONG_ANSWER";
    case JudgeStatus::TimeLimitExceeded:    return "TIME_LIMIT_EXCEEDED";
    case JudgeStatus::MemoryLimitExceeded:  return "MEMORY_LIMIT_EXCEEDED";
    case JudgeStatus::RuntimeError:         return "RUNTIME_ERROR";
    case JudgeStatus::CompilationError:     return "COMPILE_ERROR";
    case JudgeStatus::SystemError:          return "SYSTEM_ERROR";
    }
    return "Unknown";
}

const char* to_description(JudgeStatus status) {
    switch (status) {
    case JudgeStatus::Pending:              return "Waiting in queue";
    case JudgeStatus::Compiling:            return "Compilation in progress";
    case JudgeStatus::Running:              return "Execution in progress";
    case JudgeStatus::Accepted:             return "All test cases passed";
    case JudgeStatus::WrongAnswer:          return "Output does not match expected";
    case JudgeStatus::TimeLimitExceeded:    return "Time limit exceeded";
    case JudgeStatus::MemoryLimitExceeded:  return "Memory limit exceeded";
    case JudgeStatus::RuntimeError:         return "Runtime error or non-zero exit";
    case JudgeStatus::CompilationError:     return "Compilation failed";
    case JudgeStatus::SystemError:          return "Internal system error";
    }
    return "Unknown status";
}

JudgeStatus status_from_string(const std::string& s) {
    static const std::unordered_map<std::string, JudgeStatus> map = {
        {"PENDING",               JudgeStatus::Pending},
        {"COMPILING",             JudgeStatus::Compiling},
        {"RUNNING",               JudgeStatus::Running},
        {"ACCEPTED",              JudgeStatus::Accepted},
        {"WRONG_ANSWER",          JudgeStatus::WrongAnswer},
        {"TIME_LIMIT_EXCEEDED",   JudgeStatus::TimeLimitExceeded},
        {"MEMORY_LIMIT_EXCEEDED", JudgeStatus::MemoryLimitExceeded},
        {"RUNTIME_ERROR",         JudgeStatus::RuntimeError},
        {"COMPILE_ERROR",         JudgeStatus::CompilationError},
        {"SYSTEM_ERROR",          JudgeStatus::SystemError},
    };
    auto it = map.find(s);
    return (it != map.end()) ? it->second : JudgeStatus::SystemError;
}

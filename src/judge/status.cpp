#include "judge/status.h"

#include <unordered_map>

const char* to_string(JudgeStatus status) {
    switch (status) {
    case JudgeStatus::Pending:              return "Pending";
    case JudgeStatus::Compiling:            return "Compiling";
    case JudgeStatus::Running:              return "Running";
    case JudgeStatus::Accepted:             return "Accepted";
    case JudgeStatus::WrongAnswer:          return "WrongAnswer";
    case JudgeStatus::TimeLimitExceeded:    return "TimeLimitExceeded";
    case JudgeStatus::MemoryLimitExceeded:  return "MemoryLimitExceeded";
    case JudgeStatus::RuntimeError:         return "RuntimeError";
    case JudgeStatus::CompilationError:     return "CompilationError";
    case JudgeStatus::SystemError:          return "SystemError";
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
        {"Pending",             JudgeStatus::Pending},
        {"Compiling",           JudgeStatus::Compiling},
        {"Running",             JudgeStatus::Running},
        {"Accepted",            JudgeStatus::Accepted},
        {"WrongAnswer",         JudgeStatus::WrongAnswer},
        {"TimeLimitExceeded",   JudgeStatus::TimeLimitExceeded},
        {"MemoryLimitExceeded", JudgeStatus::MemoryLimitExceeded},
        {"RuntimeError",        JudgeStatus::RuntimeError},
        {"CompilationError",    JudgeStatus::CompilationError},
        {"SystemError",         JudgeStatus::SystemError},
    };
    auto it = map.find(s);
    return (it != map.end()) ? it->second : JudgeStatus::SystemError;
}

#pragma once

#include <string>

enum class JudgeStatus {
    Pending,
    Compiling,
    Running,
    Accepted,
    WrongAnswer,
    TimeLimitExceeded,
    MemoryLimitExceeded,
    RuntimeError,
    CompilationError,
    SystemError,
};

const char* to_string(JudgeStatus status);
const char* to_description(JudgeStatus status);
JudgeStatus status_from_string(const std::string& s);

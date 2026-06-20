#pragma once

#include <string>
#include <vector>
#include "config/config.h"

struct SandboxResult {
    int exit_code = 0;
    int signal_num = 0;
    int time_used_ms = 0;
    int memory_used_kb = 0;
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out = false;

    std::string status; // raw status from isolate meta file
};

class Sandbox {
public:
    Sandbox(int box_id, const IsolateConfig& cfg);
    ~Sandbox();

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    bool init();
    bool cleanup();

    SandboxResult compile(const std::string& language,
                          const std::string& source_code);
    SandboxResult run(const std::string& language,
                      int time_limit_ms,
                      int memory_limit_kb,
                      const std::string& stdin_input);

    int box_id() const { return box_id_; }

private:
    int box_id_;
    std::string isolate_bin_;
    std::string box_path_;
    std::string java_main_class_ = "Main";

    SandboxResult execute(const std::vector<std::string>& args,
                          const std::string& meta_filename);
    static SandboxResult parse_meta(const std::string& meta_path);
    static std::string normalize_language(const std::string& language);
    static std::string java_class_name(const std::string& source_code);
    bool write_file(const std::string& filename, const std::string& content);
    std::string read_file(const std::string& filename);
};

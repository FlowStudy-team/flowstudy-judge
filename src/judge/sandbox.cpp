#include "judge/sandbox.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

Sandbox::Sandbox(int box_id, const IsolateConfig& cfg)
    : box_id_(box_id)
    , isolate_bin_(cfg.binary_path)
    , box_path_("/var/local/lib/isolate/" + std::to_string(box_id) + "/box/")
{
}

Sandbox::~Sandbox() {
}

bool Sandbox::init() {
    std::string cmd = isolate_bin_ + " --init --box-id=" + std::to_string(box_id_);
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        spdlog::error("Failed to init sandbox {}", box_id_);
        return false;
    }
    // Ensure box directory exists and is writable
    if (mkdir(box_path_.c_str(), 0755) != 0 && errno != EEXIST) {
        spdlog::warn("Cannot create box directory {}: {}", box_path_, strerror(errno));
    }
    spdlog::info("Sandbox {} initialized", box_id_);
    return true;
}

bool Sandbox::cleanup() {
    std::string cmd = isolate_bin_ + " --cleanup --box-id=" + std::to_string(box_id_);
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        spdlog::warn("Failed to cleanup sandbox {}", box_id_);
        return false;
    }
    return true;
}

bool Sandbox::write_file(const std::string& filename, const std::string& content) {
    std::string path = box_path_ + filename;
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        spdlog::error("Cannot write file: {}", path);
        return false;
    }
    f << content;
    f.close();
    // Make sure the file is readable inside the sandbox
    chmod(path.c_str(), 0644);
    return true;
}

std::string Sandbox::read_file(const std::string& filename) {
    std::string path = box_path_ + filename;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

SandboxResult Sandbox::execute(const std::vector<std::string>& args,
                               const std::string& meta_filename) {
    SandboxResult result;

    // Build command string with --meta inserted before --run
    std::ostringstream cmd;
    cmd << isolate_bin_;
    bool meta_added = false;
    for (const auto& a : args) {
        if (!meta_added && a == "--run") {
            cmd << " --meta=" << box_path_ + meta_filename;
            meta_added = true;
        }
        cmd << " " << a;
    }
    // If --run was not found (shouldn't happen), add meta anyway
    if (!meta_added) {
        cmd << " --meta=" << box_path_ + meta_filename;
    }

    spdlog::debug("Running isolate: {}", cmd.str());

    int ret = std::system(cmd.str().c_str());

    if (ret == -1) {
        result.exit_code = -1;
        result.stderr_output = "Failed to execute isolate binary";
        result.status = "XX";
        return result;
    }

    // Parse the meta file
    std::string meta_path = box_path_ + meta_filename;
    result = parse_meta(meta_path);

    // Read stdout/stderr from box
    result.stdout_output = read_file("output.txt");
    result.stderr_output = read_file("error.txt");

    return result;
}

std::string Sandbox::normalize_language(const std::string& language) {
    std::string normalized = language;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "c++") return "cpp";
    if (normalized == "py") return "python";
    return normalized;
}

std::string Sandbox::java_class_name(const std::string& source_code) {
    const std::string marker = "public class ";
    auto pos = source_code.find(marker);
    if (pos != std::string::npos) {
        pos += marker.size();
        while (pos < source_code.size() && std::isspace(static_cast<unsigned char>(source_code[pos]))) {
            pos++;
        }
        std::string class_name;
        while (pos < source_code.size()) {
            unsigned char ch = static_cast<unsigned char>(source_code[pos]);
            if (!std::isalnum(ch) && ch != '_') {
                break;
            }
            class_name.push_back(static_cast<char>(ch));
            pos++;
        }
        if (!class_name.empty()) {
            return class_name;
        }
    }
    return "Main";
}

SandboxResult Sandbox::compile(const std::string& language,
                               const std::string& source_code) {
    std::string normalized = normalize_language(language);
    std::vector<std::string> args;
    std::string source_file;

    if (normalized == "cpp") {
        source_file = "solution.cpp";
        args = {
            "--box-id=" + std::to_string(box_id_),
            "--env=PATH=/usr/bin:/bin",
            "--processes=50",
            "--wall-time=30",
            "--stderr=error.txt",
            "--run",
            "--",
            "/usr/bin/g++",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-o",
            "solution",
            source_file
        };
    } else if (normalized == "java") {
        std::string class_name = java_class_name(source_code);
        java_main_class_ = class_name;
        source_file = class_name + ".java";
        args = {
            "--box-id=" + std::to_string(box_id_),
            "--env=PATH=/usr/bin:/bin",
            "--processes=50",
            "--wall-time=30",
            "--stderr=error.txt",
            "--run",
            "--",
            "/usr/bin/javac",
            source_file
        };
    } else if (normalized == "python") {
        source_file = "solution.py";
        args = {
            "--box-id=" + std::to_string(box_id_),
            "--env=PATH=/usr/bin:/bin",
            "--processes=20",
            "--wall-time=10",
            "--stderr=error.txt",
            "--run",
            "--",
            "/usr/bin/python3",
            "-m",
            "py_compile",
            source_file
        };
    } else if (normalized == "go") {
        source_file = "main.go";
        std::string tmp_path = box_path_ + "tmp";
        if (mkdir(tmp_path.c_str(), 0755) != 0 && errno != EEXIST) {
            SandboxResult r;
            r.exit_code = -1;
            r.stderr_output = "Failed to create Go temp directory";
            r.status = "XX";
            return r;
        }
        std::string go_cache_path = tmp_path + "/go-cache";
        if (mkdir(go_cache_path.c_str(), 0755) != 0 && errno != EEXIST) {
            SandboxResult r;
            r.exit_code = -1;
            r.stderr_output = "Failed to create Go build cache directory";
            r.status = "XX";
            return r;
        }
        args = {
            "--box-id=" + std::to_string(box_id_),
            "--env=PATH=/usr/local/go/bin:/usr/bin:/bin",
            "--env=GOCACHE=/tmp/go-cache",
            "--processes=50",
            "--wall-time=30",
            "--stderr=error.txt",
            "--run",
            "--",
            "/usr/bin/env",
            "go",
            "build",
            "-o",
            "solution",
            source_file
        };
    } else {
        SandboxResult r;
        r.exit_code = -1;
        r.stderr_output = "Unsupported language: " + language;
        r.status = "XX";
        return r;
    }

    if (!write_file(source_file, source_code)) {
        SandboxResult r;
        r.exit_code = -1;
        r.stderr_output = "Failed to write source file: " + source_file;
        r.status = "XX";
        return r;
    }

    auto result = execute(args, "compile_meta.txt");

    // If stderr is empty from meta, re-read from file
    if (result.stderr_output.empty()) {
        result.stderr_output = read_file("error.txt");
    }

    return result;
}

SandboxResult Sandbox::run(const std::string& language,
                           int time_limit_ms,
                           int /*memory_limit_kb*/,
                           const std::string& stdin_input) {
    if (!write_file("input.txt", stdin_input)) {
        SandboxResult r;
        r.exit_code = -1;
        r.stderr_output = "Failed to write input file";
        r.status = "XX";
        return r;
    }

    // wall-time set to 2x the CPU time limit, minimum 5 seconds
    double wall_time = std::max(5.0, time_limit_ms / 1000.0 * 2.0);
    std::string normalized = normalize_language(language);
    std::vector<std::string> command;

    if (normalized == "cpp" || normalized == "go") {
        command = {"./solution"};
    } else if (normalized == "java") {
        command = {"/usr/bin/java", "-cp", ".", java_main_class_};
    } else if (normalized == "python") {
        command = {"/usr/bin/python3", "solution.py"};
    } else {
        SandboxResult r;
        r.exit_code = -1;
        r.stderr_output = "Unsupported language: " + language;
        r.status = "XX";
        return r;
    }
    std::string process_limit = normalized == "cpp" ? "1" : "64";

    std::vector<std::string> args = {
        "--box-id=" + std::to_string(box_id_),
        "--env=PATH=/usr/local/go/bin:/usr/bin:/bin",
        "--processes=" + process_limit,
        "--wall-time=" + std::to_string(wall_time),
        "--stdin=input.txt",
        "--stdout=output.txt",
        "--stderr=error.txt",
        "--run",
        "--"
    };
    args.insert(args.end(), command.begin(), command.end());

    auto result = execute(args, "run_meta.txt");

    result.timed_out = (result.status == "TO");

    return result;
}

SandboxResult Sandbox::parse_meta(const std::string& meta_path) {
    SandboxResult result;

    std::ifstream f(meta_path);
    if (!f.is_open()) {
        result.exit_code = -1;
        result.stderr_output = "Cannot read meta file: " + meta_path;
        result.status = "XX";
        return result;
    }

    std::string line;
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        if (key == "time") {
            // time in seconds, convert to ms
            result.time_used_ms = static_cast<int>(std::stod(val) * 1000);
        } else if (key == "time-wall") {
            // wall time, use for timeout detection if larger
            int wall_ms = static_cast<int>(std::stod(val) * 1000);
            if (wall_ms > result.time_used_ms) {
                result.time_used_ms = wall_ms;
            }
        } else if (key == "max-rss") {
            result.memory_used_kb = std::stoi(val);
        } else if (key == "cg-mem") {
            result.memory_used_kb = std::stoi(val);
        } else if (key == "exitcode") {
            result.exit_code = std::stoi(val);
        } else if (key == "status") {
            result.status = val;
        } else if (key == "exitsig") {
            result.signal_num = std::stoi(val);
        } else if (key == "killed") {
            // isolate killed the process
            result.timed_out = true;
        }
    }

    return result;
}

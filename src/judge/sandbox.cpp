#include "judge/sandbox.h"

#include <cstdio>
#include <cstdlib>
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

SandboxResult Sandbox::compile(const std::string& source_code) {
    if (!write_file("solution.cpp", source_code)) {
        SandboxResult r;
        r.exit_code = -1;
        r.stderr_output = "Failed to write source file";
        r.status = "XX";
        return r;
    }

    std::vector<std::string> args = {
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
        "solution.cpp"
    };

    auto result = execute(args, "compile_meta.txt");

    // If stderr is empty from meta, re-read from file
    if (result.stderr_output.empty()) {
        result.stderr_output = read_file("error.txt");
    }

    return result;
}

SandboxResult Sandbox::run(int time_limit_ms, int /*memory_limit_kb*/,
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

    std::vector<std::string> args = {
        "--box-id=" + std::to_string(box_id_),
        "--env=PATH=/usr/bin:/bin",
        "--processes=1",
        "--wall-time=" + std::to_string(wall_time),
        "--stdin=input.txt",
        "--stdout=output.txt",
        "--stderr=error.txt",
        "--run",
        "--",
        "./solution"
    };

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

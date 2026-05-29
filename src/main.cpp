#include <csignal>
#include <iostream>
#include <string>

#include <spdlog/spdlog.h>

#include "config/config.h"
#include "worker/worker.h"

static Worker* g_worker = nullptr;

static void signal_handler(int sig) {
    spdlog::info("Received signal {}", sig);
    if (g_worker) {
        g_worker->shutdown();
    }
}

int main(int argc, char* argv[]) {
    std::string config_path = (argc >= 2) ? argv[1] : "config.json";

    // Load config
    auto cfg_opt = load_config(config_path);
    if (!cfg_opt) {
        std::cerr << "Failed to load config from " << config_path << std::endl;
        return 1;
    }

    // Setup logging
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::info("Judge worker starting, config loaded from {}", config_path);

    // Signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create and run worker
    Worker worker(*cfg_opt);
    g_worker = &worker;

    int ret = worker.run();

    spdlog::info("Judge worker exiting with code {}", ret);
    return ret;
}

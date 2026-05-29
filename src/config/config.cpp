#include "config/config.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

std::optional<AppConfig> load_config(const std::string& filepath) {
    try {
        std::ifstream f(filepath);
        if (!f.is_open()) {
            spdlog::error("Cannot open config file: {}", filepath);
            return std::nullopt;
        }

        nlohmann::json j = nlohmann::json::parse(f);
        AppConfig cfg;

        if (j.contains("rabbitmq")) {
            auto& r = j["rabbitmq"];
            if (r.contains("hostname")) cfg.rabbitmq.hostname = r["hostname"];
            if (r.contains("port")) cfg.rabbitmq.port = r["port"];
            if (r.contains("vhost")) cfg.rabbitmq.vhost = r["vhost"];
            if (r.contains("username")) cfg.rabbitmq.username = r["username"];
            if (r.contains("password")) cfg.rabbitmq.password = r["password"];
            if (r.contains("queue_name")) cfg.rabbitmq.queue_name = r["queue_name"];
        }

        if (j.contains("mysql")) {
            auto& m = j["mysql"];
            if (m.contains("hostname")) cfg.mysql.hostname = m["hostname"];
            if (m.contains("port")) cfg.mysql.port = m["port"];
            if (m.contains("username")) cfg.mysql.username = m["username"];
            if (m.contains("password")) cfg.mysql.password = m["password"];
            if (m.contains("database")) cfg.mysql.database = m["database"];
        }

        if (j.contains("isolate")) {
            auto& i = j["isolate"];
            if (i.contains("binary_path")) cfg.isolate.binary_path = i["binary_path"];
            if (i.contains("box_id_start")) cfg.isolate.box_id_start = i["box_id_start"];
            if (i.contains("num_boxes")) cfg.isolate.num_boxes = i["num_boxes"];
        }

        if (j.contains("judge")) {
            auto& jd = j["judge"];
            if (jd.contains("concurrency")) cfg.judge.concurrency = jd["concurrency"];
        }

        spdlog::info("Config loaded from {}", filepath);
        return cfg;

    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("JSON parse error in config file: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Error loading config: {}", e.what());
        return std::nullopt;
    }
}

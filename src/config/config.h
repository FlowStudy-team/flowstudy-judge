#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct RabbitMQConfig {
    std::string hostname = "127.0.0.1";
    int port = 5672;
    std::string vhost = "/";
    std::string username = "judge";
    std::string password = "judge_pass";
    std::string queue_name = "submission_queue";
};

struct MySQLConfig {
    std::string hostname = "127.0.0.1";
    int port = 3306;
    std::string username = "judge";
    std::string password = "judge_pass";
    std::string database = "online_judge";
};

struct IsolateConfig {
    std::string binary_path = "/usr/local/bin/isolate";
    int box_id_start = 1;
    int num_boxes = 5;
};

struct JudgeConfig {
    int concurrency = 1;
};

struct AppConfig {
    RabbitMQConfig rabbitmq;
    MySQLConfig mysql;
    IsolateConfig isolate;
    JudgeConfig judge;
};

std::optional<AppConfig> load_config(const std::string& filepath);

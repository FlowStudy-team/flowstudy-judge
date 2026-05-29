#pragma once

#include <atomic>
#include <memory>

#include "config/config.h"
#include "db/mysql_client.h"
#include "judge/judge_engine.h"
#include "mq/rabbitmq_client.h"

class Worker {
public:
    explicit Worker(const AppConfig& cfg);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    int run();
    void shutdown();

private:
    AppConfig cfg_;
    std::atomic<bool> running_{true};

    std::unique_ptr<RabbitMQClient> mq_;
    std::unique_ptr<MySQLClient> db_;
    std::unique_ptr<JudgeEngine> judge_engine_;

    void on_message(uint64_t delivery_tag, const std::string& body);
};

#include "worker/worker.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

Worker::Worker(const AppConfig& cfg)
    : cfg_(cfg)
    , mq_(std::make_unique<RabbitMQClient>(cfg.rabbitmq))
    , db_(std::make_unique<MySQLClient>(cfg.mysql))
    , judge_engine_(std::make_unique<JudgeEngine>(cfg.isolate,
                                                   cfg.judge.concurrency))
{
}

Worker::~Worker() = default;

int Worker::run() {
    // Connect to MySQL with retry
    spdlog::info("Connecting to MySQL...");
    int retries = 0;
    while (running_ && !db_->connect()) {
        retries++;
        int delay = std::min(retries * 2, 30);
        spdlog::warn("MySQL connection failed, retrying in {}s...", delay);
        std::this_thread::sleep_for(std::chrono::seconds(delay));
    }
    if (!running_) return 0;

    // Connect to RabbitMQ with retry
    spdlog::info("Connecting to RabbitMQ...");
    retries = 0;
    while (running_ && !mq_->connect()) {
        retries++;
        int delay = std::min(retries * 2, 30);
        spdlog::warn("RabbitMQ connection failed, retrying in {}s...", delay);
        std::this_thread::sleep_for(std::chrono::seconds(delay));
    }
    if (!running_) return 0;

    spdlog::info("Worker started, waiting for messages...");

    // Main consume loop
    auto callback = [this](uint64_t delivery_tag, const std::string& body) {
        this->on_message(delivery_tag, body);
    };

    if (!mq_->consume(callback)) {
        spdlog::error("Consume loop exited unexpectedly");
        return 1;
    }

    return 0;
}

void Worker::shutdown() {
    spdlog::info("Shutdown requested");
    running_ = false;
}

void Worker::on_message(uint64_t delivery_tag, const std::string& body) {
    spdlog::debug("Received message, delivery_tag={}", delivery_tag);

    // Parse message
    auto submission_opt = JudgeEngine::parse_message(body);
    if (!submission_opt) {
        spdlog::error("Failed to parse message, acking to discard");
        mq_->ack(delivery_tag);
        return;
    }

    auto& submission = *submission_opt;
    spdlog::info("[{}] Judging problem {}, language={}, mode={}, {} testcases",
                 submission.submission_id, submission.problem_id,
                 submission.language, submission.submit_mode,
                 submission.testcases.size());

    // Judge
    auto result = judge_engine_->judge(submission);

    // Build DB result
    JudgeResult db_result;
    db_result.submission_id = submission.submission_id;
    db_result.problem_id = submission.problem_id;
    db_result.status = to_string(result.status);
    db_result.time_used_ms = result.time_used_ms;
    db_result.memory_used_kb = result.memory_used_kb;
    db_result.error_message = result.error_message;
    db_result.compiler_output = result.compiler_output;
    db_result.failed_testcase = result.failed_testcase_index;
    for (const auto& item : result.case_results) {
        JudgeCaseResult case_result;
        case_result.testcase_id = item.testcase_id;
        case_result.case_index = item.case_index;
        case_result.status = to_string(item.status);
        case_result.time_used_ms = item.time_used_ms;
        case_result.memory_used_kb = item.memory_used_kb;
        case_result.actual_output = item.actual_output;
        case_result.expected_output = item.expected_output;
        case_result.error_message = item.error_message;
        db_result.case_results.push_back(std::move(case_result));
    }

    // Ping MySQL before writing (connection may have timed out)
    db_->ping();

    // Write to database
    if (!db_->update_submission(db_result)) {
        spdlog::error("[{}] Failed to update DB, nack with requeue",
                      submission.submission_id);
        mq_->nack_requeue(delivery_tag);
        return;
    }

    // ACK the message
    mq_->ack(delivery_tag);
    spdlog::info("[{}] Done: {}", submission.submission_id,
                 to_string(result.status));
}

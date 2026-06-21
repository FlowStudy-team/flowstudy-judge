#include "db/mysql_client.h"

#include <mysql/mysql.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <sstream>

struct MySQLClient::Impl {
    MYSQL* conn = nullptr;
    MySQLConfig config;
    bool connected = false;

    explicit Impl(const MySQLConfig& cfg) : config(cfg) {}
};

MySQLClient::MySQLClient(const MySQLConfig& cfg)
    : pimpl_(std::make_unique<Impl>(cfg))
{
}

MySQLClient::~MySQLClient() {
    if (pimpl_->conn) {
        mysql_close(pimpl_->conn);
        pimpl_->conn = nullptr;
    }
}

MySQLClient::MySQLClient(MySQLClient&&) noexcept = default;
MySQLClient& MySQLClient::operator=(MySQLClient&&) noexcept = default;

bool MySQLClient::connect() {
    pimpl_->conn = mysql_init(nullptr);
    if (!pimpl_->conn) {
        spdlog::error("mysql_init failed");
        return false;
    }

    // Set reconnect option
    bool reconnect = true;
    mysql_options(pimpl_->conn, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(pimpl_->conn,
                            pimpl_->config.hostname.c_str(),
                            pimpl_->config.username.c_str(),
                            pimpl_->config.password.c_str(),
                            pimpl_->config.database.c_str(),
                            pimpl_->config.port,
                            nullptr, 0)) {
        spdlog::error("MySQL connect failed: {}", mysql_error(pimpl_->conn));
        return false;
    }

    pimpl_->connected = true;
    spdlog::info("MySQL connected to {}:{}/{}",
                 pimpl_->config.hostname, pimpl_->config.port,
                 pimpl_->config.database);
    return true;
}

static std::string escape_sql(MYSQL* conn, const std::string& value) {
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(
        conn, escaped.data(), value.c_str(), static_cast<unsigned long>(value.size()));
    escaped.resize(len);
    return escaped;
}

static std::string nullable_sql(MYSQL* conn, const std::string& value) {
    if (value.empty()) {
        return "NULL";
    }
    return "'" + escape_sql(conn, value) + "'";
}

bool MySQLClient::update_submission(const JudgeResult& result) {
    if (!pimpl_->connected) {
        spdlog::error("MySQL not connected");
        return false;
    }

    if (mysql_query(pimpl_->conn, "START TRANSACTION") != 0) {
        spdlog::error("START TRANSACTION failed: {}", mysql_error(pimpl_->conn));
        return false;
    }

    std::ostringstream update;
    update << "UPDATE fs_submission SET "
           << "status = '" << escape_sql(pimpl_->conn, result.status) << "', "
           << "score = " << (result.status == "ACCEPTED" ? 100 : 0) << ", "
           << "time_used_ms = " << result.time_used_ms << ", "
           << "memory_used_kb = " << result.memory_used_kb << ", "
           << "compile_message = " << nullable_sql(pimpl_->conn, result.compiler_output) << ", "
           << "runtime_message = " << nullable_sql(pimpl_->conn, result.error_message) << ", "
           << "updated_at = NOW() "
           << "WHERE id = " << result.submission_id;

    if (mysql_query(pimpl_->conn, update.str().c_str()) != 0) {
        spdlog::error("Update fs_submission failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    std::ostringstream clear_cases;
    clear_cases << "DELETE FROM fs_judge_case_result WHERE submission_id = " << result.submission_id;
    if (mysql_query(pimpl_->conn, clear_cases.str().c_str()) != 0) {
        spdlog::error("Delete fs_judge_case_result failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    for (const auto& item : result.case_results) {
        std::ostringstream insert_case;
        insert_case << "INSERT INTO fs_judge_case_result ("
                    << "submission_id, testcase_id, case_index, status, time_used_ms, memory_used_kb, "
                    << "input_text, actual_output, expected_output, error_message"
                    << ") VALUES ("
                    << result.submission_id << ", "
                    << (item.testcase_id == 0 ? "NULL" : std::to_string(item.testcase_id)) << ", "
                    << item.case_index << ", "
                    << "'" << escape_sql(pimpl_->conn, item.status) << "', "
                    << item.time_used_ms << ", "
                    << item.memory_used_kb << ", "
                    << nullable_sql(pimpl_->conn, item.input_text) << ", "
                    << nullable_sql(pimpl_->conn, item.actual_output) << ", "
                    << nullable_sql(pimpl_->conn, item.expected_output) << ", "
                    << nullable_sql(pimpl_->conn, item.error_message) << ")";

        if (mysql_query(pimpl_->conn, insert_case.str().c_str()) != 0) {
            spdlog::error("Insert fs_judge_case_result failed: {}", mysql_error(pimpl_->conn));
            mysql_query(pimpl_->conn, "ROLLBACK");
            return false;
        }
    }

    if (result.status == "ACCEPTED" && result.problem_id != 0) {
        std::ostringstream update_problem;
        update_problem << "UPDATE fs_problem SET accepted_count = accepted_count + 1 "
                       << "WHERE id = " << result.problem_id;
        if (mysql_query(pimpl_->conn, update_problem.str().c_str()) != 0) {
            spdlog::error("Update fs_problem accepted_count failed: {}", mysql_error(pimpl_->conn));
            mysql_query(pimpl_->conn, "ROLLBACK");
            return false;
        }
    }

    if (mysql_query(pimpl_->conn, "COMMIT") != 0) {
        spdlog::error("COMMIT failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    spdlog::info("[{}] DB updated: status={}, time={}ms, mem={}KB",
                 result.submission_id, result.status,
                 result.time_used_ms, result.memory_used_kb);
    return true;
}

bool MySQLClient::update_code_run(const JudgeResult& result) {
    if (!pimpl_->connected) {
        spdlog::error("MySQL not connected");
        return false;
    }

    if (mysql_query(pimpl_->conn, "START TRANSACTION") != 0) {
        spdlog::error("START TRANSACTION failed: {}", mysql_error(pimpl_->conn));
        return false;
    }

    std::ostringstream update;
    update << "UPDATE fs_code_run SET "
           << "status = '" << escape_sql(pimpl_->conn, result.status) << "', "
           << "time_used_ms = " << result.time_used_ms << ", "
           << "memory_used_kb = " << result.memory_used_kb << ", "
           << "compile_message = " << nullable_sql(pimpl_->conn, result.compiler_output) << ", "
           << "runtime_message = " << nullable_sql(pimpl_->conn, result.error_message) << ", "
           << "updated_at = NOW() "
           << "WHERE id = " << result.run_id;

    if (mysql_query(pimpl_->conn, update.str().c_str()) != 0) {
        spdlog::error("Update fs_code_run failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    std::ostringstream clear_cases;
    clear_cases << "DELETE FROM fs_code_run_case_result WHERE run_id = " << result.run_id;
    if (mysql_query(pimpl_->conn, clear_cases.str().c_str()) != 0) {
        spdlog::error("Delete fs_code_run_case_result failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    for (const auto& item : result.case_results) {
        std::ostringstream insert_case;
        insert_case << "INSERT INTO fs_code_run_case_result ("
                    << "run_id, testcase_id, case_index, status, time_used_ms, memory_used_kb, "
                    << "input_text, actual_output, expected_output, error_message"
                    << ") VALUES ("
                    << result.run_id << ", "
                    << (item.testcase_id == 0 ? "NULL" : std::to_string(item.testcase_id)) << ", "
                    << item.case_index << ", "
                    << "'" << escape_sql(pimpl_->conn, item.status) << "', "
                    << item.time_used_ms << ", "
                    << item.memory_used_kb << ", "
                    << nullable_sql(pimpl_->conn, item.input_text) << ", "
                    << nullable_sql(pimpl_->conn, item.actual_output) << ", "
                    << nullable_sql(pimpl_->conn, item.expected_output) << ", "
                    << nullable_sql(pimpl_->conn, item.error_message) << ")";

        if (mysql_query(pimpl_->conn, insert_case.str().c_str()) != 0) {
            spdlog::error("Insert fs_code_run_case_result failed: {}", mysql_error(pimpl_->conn));
            mysql_query(pimpl_->conn, "ROLLBACK");
            return false;
        }
    }

    if (mysql_query(pimpl_->conn, "COMMIT") != 0) {
        spdlog::error("COMMIT failed: {}", mysql_error(pimpl_->conn));
        mysql_query(pimpl_->conn, "ROLLBACK");
        return false;
    }

    spdlog::info("[{}] Run DB updated: status={}, time={}ms, mem={}KB",
                 result.run_id, result.status,
                 result.time_used_ms, result.memory_used_kb);
    return true;
}

bool MySQLClient::ping() {
    if (!pimpl_->connected || !pimpl_->conn) {
        return connect();
    }
    if (mysql_ping(pimpl_->conn) != 0) {
        spdlog::warn("MySQL ping failed, reconnecting...");
        return connect();
    }
    return true;
}

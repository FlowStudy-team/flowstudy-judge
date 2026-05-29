#include "db/mysql_client.h"

#include <mysql/mysql.h>
#include <spdlog/spdlog.h>
#include <cstring>

struct MySQLClient::Impl {
    MYSQL* conn = nullptr;
    MYSQL_STMT* stmt = nullptr;
    MySQLConfig config;
    bool connected = false;

    explicit Impl(const MySQLConfig& cfg) : config(cfg) {}
};

MySQLClient::MySQLClient(const MySQLConfig& cfg)
    : pimpl_(std::make_unique<Impl>(cfg))
{
}

MySQLClient::~MySQLClient() {
    if (pimpl_->stmt) {
        mysql_stmt_close(pimpl_->stmt);
        pimpl_->stmt = nullptr;
    }
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

    // Prepare the UPDATE statement
    const char* query =
        "UPDATE submissions SET "
        "status = ?, "
        "time_used_ms = ?, "
        "memory_used_kb = ?, "
        "error_message = ?, "
        "compiler_output = ?, "
        "failed_testcase = ?, "
        "judged_at = NOW() "
        "WHERE submission_id = ?";

    pimpl_->stmt = mysql_stmt_init(pimpl_->conn);
    if (!pimpl_->stmt) {
        spdlog::error("mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(pimpl_->stmt, query, std::strlen(query)) != 0) {
        spdlog::error("mysql_stmt_prepare failed: {}",
                      mysql_stmt_error(pimpl_->stmt));
        return false;
    }

    pimpl_->connected = true;
    spdlog::info("MySQL connected to {}:{}/{}",
                 pimpl_->config.hostname, pimpl_->config.port,
                 pimpl_->config.database);
    return true;
}

bool MySQLClient::update_submission(const JudgeResult& result) {
    if (!pimpl_->connected) {
        spdlog::error("MySQL not connected");
        return false;
    }

    // Bind parameters
    // status (string), time_used_ms (int), memory_used_kb (int),
    // error_message (string), compiler_output (string),
    // failed_testcase (int), submission_id (uint64_t)
    enum { NUM_PARAMS = 7 };

    MYSQL_BIND bind[NUM_PARAMS];
    std::memset(bind, 0, sizeof(bind));

    // status
    unsigned long status_len = result.status.size();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(result.status.c_str());
    bind[0].buffer_length = status_len;
    bind[0].length = &status_len;

    // time_used_ms
    int time_used = result.time_used_ms;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &time_used;

    // memory_used_kb
    int memory_used = result.memory_used_kb;
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &memory_used;

    // error_message
    unsigned long error_len = result.error_message.empty() ? 0 : result.error_message.size();
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = error_len > 0 ? const_cast<char*>(result.error_message.c_str()) : nullptr;
    bind[3].buffer_length = error_len;
    bind[3].length = &error_len;

    // compiler_output
    unsigned long compiler_len = result.compiler_output.empty() ? 0 : result.compiler_output.size();
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = compiler_len > 0 ? const_cast<char*>(result.compiler_output.c_str()) : nullptr;
    bind[4].buffer_length = compiler_len;
    bind[4].length = &compiler_len;

    // failed_testcase
    int failed_tc = result.failed_testcase;
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = &failed_tc;

    // submission_id
    unsigned long long sub_id = result.submission_id;
    bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[6].buffer = &sub_id;
    bind[6].is_unsigned = 1;

    if (mysql_stmt_bind_param(pimpl_->stmt, bind) != 0) {
        spdlog::error("mysql_stmt_bind_param failed: {}",
                      mysql_stmt_error(pimpl_->stmt));
        return false;
    }

    if (mysql_stmt_execute(pimpl_->stmt) != 0) {
        spdlog::error("mysql_stmt_execute failed: {}",
                      mysql_stmt_error(pimpl_->stmt));
        return false;
    }

    spdlog::info("[{}] DB updated: status={}, time={}ms, mem={}KB",
                 result.submission_id, result.status,
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

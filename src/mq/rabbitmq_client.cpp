#include "mq/rabbitmq_client.h"

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <spdlog/spdlog.h>
#include <cstring>

struct RabbitMQClient::Impl {
    RabbitMQConfig config;
    amqp_connection_state_t conn = nullptr;
    amqp_socket_t* socket = nullptr;
    int channel = 1;
    bool connected = false;
    bool consuming = false;

    explicit Impl(const RabbitMQConfig& cfg) : config(cfg) {}
};

RabbitMQClient::RabbitMQClient(const RabbitMQConfig& cfg)
    : pimpl_(std::make_unique<Impl>(cfg))
{
}

RabbitMQClient::~RabbitMQClient() {
    if (pimpl_->conn) {
        if (pimpl_->consuming) {
            amqp_channel_close(pimpl_->conn, pimpl_->channel, AMQP_REPLY_SUCCESS);
        }
        amqp_connection_close(pimpl_->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(pimpl_->conn);
        pimpl_->conn = nullptr;
    }
}

RabbitMQClient::RabbitMQClient(RabbitMQClient&&) noexcept = default;
RabbitMQClient& RabbitMQClient::operator=(RabbitMQClient&&) noexcept = default;

bool RabbitMQClient::connect() {
    pimpl_->conn = amqp_new_connection();

    pimpl_->socket = amqp_tcp_socket_new(pimpl_->conn);
    if (!pimpl_->socket) {
        spdlog::error("Failed to create TCP socket");
        return false;
    }

    int status = amqp_socket_open(pimpl_->socket,
                                  pimpl_->config.hostname.c_str(),
                                  pimpl_->config.port);
    if (status != AMQP_STATUS_OK) {
        spdlog::error("Failed to open socket to {}:{}",
                      pimpl_->config.hostname, pimpl_->config.port);
        return false;
    }

    amqp_rpc_reply_t reply = amqp_login(pimpl_->conn,
                                        pimpl_->config.vhost.c_str(),
                                        0,           // channel max (0 = unlimited)
                                        131072,      // frame max
                                        0,           // heartbeat
                                        AMQP_SASL_METHOD_PLAIN,
                                        pimpl_->config.username.c_str(),
                                        pimpl_->config.password.c_str());
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        spdlog::error("AMQP login failed");
        return false;
    }

    amqp_channel_open(pimpl_->conn, pimpl_->channel);
    reply = amqp_get_rpc_reply(pimpl_->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        spdlog::error("Failed to open channel");
        return false;
    }

    // Declare queue
    amqp_queue_declare(pimpl_->conn, pimpl_->channel,
                       amqp_cstring_bytes(pimpl_->config.queue_name.c_str()),
                       0,   // passive
                       1,   // durable
                       0,   // exclusive
                       0,   // auto_delete
                       amqp_empty_table);

    reply = amqp_get_rpc_reply(pimpl_->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        spdlog::error("Failed to declare queue '{}'",
                      pimpl_->config.queue_name);
        return false;
    }

    // Set QoS: prefetch_count=1 for fair dispatch
    amqp_basic_qos(pimpl_->conn, pimpl_->channel, 0, 1, 0);
    reply = amqp_get_rpc_reply(pimpl_->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        spdlog::error("Failed to set QoS");
        // Non-fatal, continue
    }

    // Start consuming
    amqp_basic_consume(pimpl_->conn, pimpl_->channel,
                       amqp_cstring_bytes(pimpl_->config.queue_name.c_str()),
                       amqp_empty_bytes,  // consumer tag
                       0,                  // no_local
                       0,                  // no_ack = false (we manually ack)
                       0,                  // exclusive
                       amqp_empty_table);
    reply = amqp_get_rpc_reply(pimpl_->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        spdlog::error("Failed to start consuming");
        return false;
    }

    pimpl_->connected = true;
    pimpl_->consuming = true;
    spdlog::info("RabbitMQ connected to {}:{}, queue='{}'",
                 pimpl_->config.hostname, pimpl_->config.port,
                 pimpl_->config.queue_name);
    return true;
}

bool RabbitMQClient::consume(const MessageCallback& callback) {
    if (!pimpl_->connected) {
        spdlog::error("RabbitMQ not connected");
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout for checking shutdown
    timeout.tv_usec = 0;

    amqp_envelope_t envelope;

    while (true) {
        amqp_rpc_reply_t reply = amqp_consume_message(pimpl_->conn,
                                                      &envelope,
                                                      &timeout, 0);

        if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
            reply.library_error == AMQP_STATUS_TIMEOUT) {
            continue; // timeout, check shutdown or retry
        }

        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            spdlog::error("AMQP consume error");
            return false;
        }

        // Extract message body
        std::string body(
            reinterpret_cast<char*>(envelope.message.body.bytes),
            envelope.message.body.len);

        uint64_t delivery_tag = envelope.delivery_tag;

        callback(delivery_tag, body);

        amqp_destroy_envelope(&envelope);
    }

    return true;
}

bool RabbitMQClient::ack(uint64_t delivery_tag) {
    if (!pimpl_->connected) return false;

    int status = amqp_basic_ack(pimpl_->conn, pimpl_->channel,
                                delivery_tag, 0); // 0 = single ack
    if (status != AMQP_STATUS_OK) {
        spdlog::error("Failed to ack delivery_tag={}", delivery_tag);
    }
    return status == AMQP_STATUS_OK;
}

bool RabbitMQClient::nack_requeue(uint64_t delivery_tag) {
    if (!pimpl_->connected) return false;

    int status = amqp_basic_nack(pimpl_->conn, pimpl_->channel,
                                 delivery_tag, 0, 1); // multiple=0, requeue=1
    if (status != AMQP_STATUS_OK) {
        spdlog::error("Failed to nack delivery_tag={}", delivery_tag);
    }
    return status == AMQP_STATUS_OK;
}

bool RabbitMQClient::is_connected() const {
    return pimpl_->connected;
}

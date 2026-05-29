#pragma once

#include <functional>
#include <memory>
#include <string>

#include "config/config.h"

class RabbitMQClient {
public:
    using MessageCallback = std::function<void(uint64_t, const std::string&)>;

    explicit RabbitMQClient(const RabbitMQConfig& cfg);
    ~RabbitMQClient();

    RabbitMQClient(const RabbitMQClient&) = delete;
    RabbitMQClient& operator=(const RabbitMQClient&) = delete;
    RabbitMQClient(RabbitMQClient&&) noexcept;
    RabbitMQClient& operator=(RabbitMQClient&&) noexcept;

    bool connect();
    bool consume(const MessageCallback& callback);
    bool ack(uint64_t delivery_tag);
    bool nack_requeue(uint64_t delivery_tag);
    bool is_connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

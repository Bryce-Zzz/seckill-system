#pragma once

#include "mq/IMessageQueue.h"
#include "service/mysql_service.h"
#include <string>
#include <memory>
#include <thread>

class OrderProcessor {
public:
    // 构造函数：注入 MQ 接口实例
    explicit OrderProcessor(std::shared_ptr<IMessageQueue> mq);

    ~OrderProcessor();

    // 禁止复制
    OrderProcessor(const OrderProcessor&) = delete;
    OrderProcessor& operator=(const OrderProcessor&) = delete;

    void init(const std::string& stream_key,
              const std::string& group_name,
              const std::string& consumer_name);
    void start();
    void stop();

    // 解析 payload: "ORDxxx|user_xxx|T001|1|1713388800000"
    static bool parsePayload(const std::string& payload, Order& order);

private:
    void processLoop();

    std::shared_ptr<IMessageQueue> mq_;
    std::string topic_;
    std::string group_name_;
    std::string consumer_name_;
    bool running_ = false;
    std::thread worker_thread_;
};

// 死信队列日志函数声明
void writeToDeadLetterLog(const std::string& msg_id, const Order& order,
                          OrderErrorType error_type, const std::string& error_msg);

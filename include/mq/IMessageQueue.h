#pragma once

#include <string>
#include <vector>
#include <memory>

// 标准化消息结构体
struct MQMessage {
    std::string id;      // 消息ID (Redis的毫秒时间戳ID或Kafka的Offset)
    std::string payload; // 消息体 (如 "ORDxxx|user_xxx|T001|1|1713388800000")
};

// 抽象消息队列接口 - 面向接口编程，支持未来平滑切换到 Kafka
class IMessageQueue {
public:
    virtual ~IMessageQueue() = default;

    // 1. 初始化消费者组 (如果组已存在应忽略错误)
    virtual bool initConsumerGroup(const std::string& topic, const std::string& group_name) = 0;

    // 2. 阻塞式拉取消息 (返回获取到的消息列表)
    // count: 每次最多拉取的消息数
    // timeout_ms: 阻塞超时时间，0 表示无限等待
    virtual std::vector<MQMessage> consume(
        const std::string& topic,
        const std::string& group_name,
        const std::string& consumer_name,
        int count,
        int timeout_ms) = 0;

    // 3. 确认消息 (XACK / Commit Offset)
    virtual bool ack(const std::string& topic, const std::string& group_name, const std::string& msg_id) = 0;

    // 4. 拉取 Pending Entries List 中未确认的消息（用于故障恢复）
    virtual std::vector<MQMessage> consumePending(
        const std::string& topic,
        const std::string& group_name,
        const std::string& consumer_name,
        int count) = 0;
};

// ==================== Redis Stream 实现 ====================

#include <memory>

// 工厂函数：创建 Redis Stream MQ 实例
std::shared_ptr<IMessageQueue> createRedisStreamMQ(
    const std::string& host,
    int port,
    const std::string& password = "",
    int db = 0
);

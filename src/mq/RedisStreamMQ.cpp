#include "mq/IMessageQueue.h"
#include "common/logger.h"
#include <hiredis/hiredis.h>
#include <cstring>

// Redis Stream MQ 实现 - 专属消费连接，不与 HTTP 线程共享
class RedisStreamMQ : public IMessageQueue {
private:
    redisContext* context_;  // 专属消费连接，BLOCK 不会卡死 HTTP 线程
    std::string host_;
    int port_;

public:
    RedisStreamMQ(const std::string& host, int port, const std::string& password = "", int db = 0) 
        : host_(host), port_(port) {
        context_ = redisConnect(host.c_str(), port);
        if (!context_ || context_->err) {
            if (context_) {
                Logger::instance().error("RedisStreamMQ connection error: " + std::string(context_->errstr));
                redisFree(context_);
                context_ = nullptr;
            } else {
                Logger::instance().error("RedisStreamMQ: can't allocate context");
            }
            return;
        }

        // 认证
        if (!password.empty()) {
            auto* reply = (redisReply*)redisCommand(context_, "AUTH %s", password.c_str());
            if (reply && reply->type == REDIS_REPLY_ERROR) {
                Logger::instance().error("RedisStreamMQ auth error");
                freeReplyObject(reply);
            }
            if (reply) freeReplyObject(reply);
        }

        // 选择 DB
        if (db > 0) {
            auto* reply = (redisReply*)redisCommand(context_, "SELECT %d", db);
            if (reply) freeReplyObject(reply);
        }

        Logger::instance().info("RedisStreamMQ connected to " + host_ + ":" + std::to_string(port_));
    }

    ~RedisStreamMQ() {
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
    }

    bool initConsumerGroup(const std::string& topic, const std::string& group_name) override {
        if (!context_) return false;

        // XGROUP CREATE topic group_name 0 MKSTREAM
        // 如果组已存在，返回 BUSYGROUP 错误，我们把它当作成功
        redisReply* reply = (redisReply*)redisCommand(context_,
            "XGROUP CREATE %s %s 0 MKSTREAM",
            topic.c_str(), group_name.c_str());

        bool success = false;
        if (reply != nullptr) {
            if (reply->type == REDIS_REPLY_STATUS) {
                std::string resp(reply->str ? reply->str : "");
                if (resp == "OK") {
                    success = true;
                    Logger::instance().info("Consumer group created: " + group_name);
                }
            } else if (reply->type == REDIS_REPLY_ERROR) {
                std::string err(reply->str ? reply->str : "");
                if (err.find("BUSYGROUP") != std::string::npos) {
                    // 组已存在，不是错误
                    success = true;
                    Logger::instance().info("Consumer group already exists: " + group_name);
                } else {
                    Logger::instance().error("XGROUP CREATE error: " + err);
                }
            }
            freeReplyObject(reply);
        }
        return success;
    }

    std::vector<MQMessage> consume(
        const std::string& topic,
        const std::string& group_name,
        const std::string& consumer_name,
        int count,
        int timeout_ms) override {
        
        std::vector<MQMessage> messages;
        if (!context_) return messages;

        // XREADGROUP GROUP group_name consumer_name COUNT count BLOCK timeout_ms STREAMS topic >
        // > 表示只读取新消息，不读取 PEL 中的_pending 消息
        redisReply* reply = (redisReply*)redisCommand(context_,
            "XREADGROUP GROUP %s %s COUNT %d BLOCK %d STREAMS %s >",
            group_name.c_str(), consumer_name.c_str(), count, timeout_ms, topic.c_str());

        if (reply == nullptr) {
            return messages;  // 超时或错误
        }

        // 解析 Redis Stream 的深层嵌套数组结构
        // 格式: [[stream_key, [[msg_id, [field, value, ...]], ...]], ...]
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements > 0) {
            redisReply* stream_reply = reply->element[0];
            if (stream_reply->type == REDIS_REPLY_ARRAY && stream_reply->elements >= 2) {
                redisReply* msgs_reply = stream_reply->element[1];
                if (msgs_reply->type == REDIS_REPLY_ARRAY) {
                    for (size_t i = 0; i < msgs_reply->elements; ++i) {
                        redisReply* msg = msgs_reply->element[i];
                        if (msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) continue;

                        // 消息 ID
                        std::string msg_id;
                        if (msg->element[0]->type == REDIS_REPLY_STRING) {
                            msg_id = std::string(msg->element[0]->str, msg->element[0]->len);
                        } else if (msg->element[0]->type == REDIS_REPLY_INTEGER) {
                            msg_id = std::to_string(msg->element[0]->integer);
                        }

                        // 消息字段
                        std::string payload;
                        redisReply* fields = msg->element[1];
                        if (fields->type == REDIS_REPLY_ARRAY) {
                            for (size_t j = 0; j + 1 < fields->elements; j += 2) {
                                std::string field_name;
                                if (fields->element[j]->type == REDIS_REPLY_STRING) {
                                    field_name = std::string(fields->element[j]->str, fields->element[j]->len);
                                }
                                if (field_name == "order_data" && fields->element[j+1]->type == REDIS_REPLY_STRING) {
                                    payload = std::string(fields->element[j+1]->str, fields->element[j+1]->len);
                                    break;
                                }
                            }
                        }

                        if (!msg_id.empty()) {
                            messages.push_back({msg_id, payload});
                        }
                    }
                }
            }
        }

        freeReplyObject(reply);
        return messages;
    }

    bool ack(const std::string& topic, const std::string& group_name, const std::string& msg_id) override {
        if (!context_) return false;

        redisReply* reply = (redisReply*)redisCommand(context_,
            "XACK %s %s %s",
            topic.c_str(), group_name.c_str(), msg_id.c_str());

        bool success = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
        if (reply) freeReplyObject(reply);
        return success;
    }

    // 拉取 PEL (Pending Entries List) 中未确认的消息 - 用于故障恢复
    std::vector<MQMessage> consumePending(
        const std::string& topic,
        const std::string& group_name,
        const std::string& consumer_name,
        int count) override {
        
        std::vector<MQMessage> messages;
        if (!context_) return messages;

        // XREADGROUP GROUP group_name consumer_name STREAMS topic 0
        // 末尾的 0 表示读取 PEL 中的所有待处理消息
        redisReply* reply = (redisReply*)redisCommand(context_,
            "XREADGROUP GROUP %s %s COUNT %d STREAMS %s 0",
            group_name.c_str(), consumer_name.c_str(), count, topic.c_str());

        if (reply == nullptr) {
            return messages;
        }

        // 解析格式同上
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements > 0) {
            redisReply* stream_reply = reply->element[0];
            if (stream_reply->type == REDIS_REPLY_ARRAY && stream_reply->elements >= 2) {
                redisReply* msgs_reply = stream_reply->element[1];
                if (msgs_reply->type == REDIS_REPLY_ARRAY) {
                    for (size_t i = 0; i < msgs_reply->elements; ++i) {
                        redisReply* msg = msgs_reply->element[i];
                        if (msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) continue;

                        std::string msg_id;
                        if (msg->element[0]->type == REDIS_REPLY_STRING) {
                            msg_id = std::string(msg->element[0]->str, msg->element[0]->len);
                        }

                        std::string payload;
                        redisReply* fields = msg->element[1];
                        if (fields->type == REDIS_REPLY_ARRAY) {
                            for (size_t j = 0; j + 1 < fields->elements; j += 2) {
                                std::string field_name;
                                if (fields->element[j]->type == REDIS_REPLY_STRING) {
                                    field_name = std::string(fields->element[j]->str, fields->element[j]->len);
                                }
                                if (field_name == "order_data" && fields->element[j+1]->type == REDIS_REPLY_STRING) {
                                    payload = std::string(fields->element[j+1]->str, fields->element[j+1]->len);
                                    break;
                                }
                            }
                        }

                        if (!msg_id.empty()) {
                            messages.push_back({msg_id, payload});
                        }
                    }
                }
            }
        }

        freeReplyObject(reply);
        return messages;
    }
};

// 工厂函数实现
std::shared_ptr<IMessageQueue> createRedisStreamMQ(
    const std::string& host,
    int port,
    const std::string& password,
    int db) {
    return std::make_shared<RedisStreamMQ>(host, port, password, db);
}

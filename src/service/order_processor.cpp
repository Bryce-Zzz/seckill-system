#include "service/order_processor.h"
#include "service/mysql_service.h"
#include "service/redis_service.h"
#include "handler/sse_handler.h"
#include "common/logger.h"
#include <sstream>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <ctime>

// ==================== 死信队列配置 ====================
static const std::string DLQ_FILE_PATH = "/tmp/seckill_dead_letter.log";

void writeToDeadLetterLog(const std::string& msg_id, const Order& order,
                          OrderErrorType error_type, const std::string& error_msg) {
    std::ofstream dlq_file(DLQ_FILE_PATH, std::ios::app);
    if (dlq_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif

        dlq_file << "========================================" << std::endl;
        dlq_file << "DEAD LETTER ENTRY" << std::endl;
        dlq_file << "----------------------------------------" << std::endl;
        dlq_file << "Time: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                 << "." << std::setfill('0') << std::setw(3) << ms.count() << std::endl;
        dlq_file << "MsgID: " << msg_id << std::endl;
        dlq_file << "OrderID: " << std::to_string(order.order_id) << std::endl;
        dlq_file << "UserID: " << order.user_id << std::endl;
        dlq_file << "ProductID: " << order.product_id << std::endl;
        dlq_file << "Quantity: " << order.quantity << std::endl;
        dlq_file << "ErrorType: ";
        switch (error_type) {
            case OrderErrorType::BUSINESS_ERROR:
                dlq_file << "BUSINESS_ERROR (no retry)";
                break;
            case OrderErrorType::SYSTEM_ERROR:
                dlq_file << "SYSTEM_ERROR (should retry)";
                break;
            default:
                dlq_file << "UNKNOWN";
        }
        dlq_file << std::endl;
        dlq_file << "ErrorMsg: " << error_msg << std::endl;
        dlq_file << "========================================" << std::endl;
        dlq_file.close();

        Logger::instance().warn("Dead letter logged: " + std::to_string(order.order_id) + " - " + error_msg);
    } else {
        Logger::instance().error("Failed to open DLQ file: " + DLQ_FILE_PATH);
    }
}

// ==================== OrderProcessor 实现 ====================

OrderProcessor::OrderProcessor(std::shared_ptr<IMessageQueue> mq)
    : mq_(std::move(mq)) {
}

OrderProcessor::~OrderProcessor() {
    stop();
}

void OrderProcessor::init(const std::string& stream_key,
                         const std::string& group_name,
                         const std::string& consumer_name) {
    topic_ = stream_key;
    group_name_ = group_name;
    consumer_name_ = consumer_name;
}

void OrderProcessor::start() {
    if (running_) return;
    running_ = true;

    // 启动前先初始化消费者组
    if (mq_) {
        mq_->initConsumerGroup(topic_, group_name_);

        // 启动时先处理 PEL 中积压的消息（故障恢复）
        Logger::instance().info("Checking PEL for pending messages...");
        auto pending = mq_->consumePending(topic_, group_name_, consumer_name_, 100);
        if (!pending.empty()) {
            Logger::instance().warn("Found " + std::to_string(pending.size()) + " pending messages in PEL");
            for (const auto& msg : pending) {
                Order order;
                if (parsePayload(msg.payload, order)) {
                    auto result = MySQLService::instance().create_order_with_error_type(order);
                    if (result.success || result.error_type == OrderErrorType::DUPLICATE_ERROR) {
                        mq_->ack(topic_, group_name_, msg.id);
                        Logger::instance().info("PEL order persisted: " + std::to_string(order.order_id));
                    } else if (result.error_type == OrderErrorType::BUSINESS_ERROR) {
                        // 业务错误：写入死信队列，然后 ACK 掉
                        mq_->ack(topic_, group_name_, msg.id);
                        writeToDeadLetterLog(msg.id, order, result.error_type, result.error_message);
                    }
                    // SYSTEM_ERROR: 不 ACK，消息留在 PEL 中等待下次重试
                }
            }
        }
    }

    worker_thread_ = std::thread(&OrderProcessor::processLoop, this);
    Logger::instance().info("Order processor started (Redis Stream consumer with DLQ support)");
}

void OrderProcessor::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    Logger::instance().info("Order processor stopped");
}

// 解析 payload: "ORDxxx|user_xxx|T001|1|1713388800000"
bool OrderProcessor::parsePayload(const std::string& payload, Order& order) {
    std::istringstream iss(payload);
    std::string token;

    try {
        // 格式: order_id|user_id|product_id|quantity|timestamp
        std::string oid_str;
        if (!std::getline(iss, oid_str, '|')) return false;
        order.order_id = std::stoull(oid_str);   // 雪花 ID：字符串 → uint64_t
        if (!std::getline(iss, order.user_id, '|')) return false;
        if (!std::getline(iss, order.product_id, '|')) return false;

        std::string qty_str;
        if (!std::getline(iss, qty_str, '|')) return false;
        order.quantity = std::stoi(qty_str);

        std::string ts_str;
        if (!std::getline(iss, ts_str, '|')) return false;
        order.created_at = std::stoll(ts_str);

        order.status = 0;  // 待处理
        order.updated_at = order.created_at;

        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to parse payload: " + payload + " - " + e.what());
        return false;
    }
}

void OrderProcessor::processLoop() {
    // 1. 初始化消费者组 (仅在启动时执行一次)
    if (mq_) {
        try {
            mq_->initConsumerGroup(topic_, group_name_);
        } catch (const std::exception& e) {
            Logger::instance().error("Failed to init MQ group: " + std::string(e.what()));
        }
    }

    // 2. 进入主消费循环
    while (running_) {
        if (!mq_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        try {
            // 一次性拉取 50 条消息
            auto msgs = mq_->consume(topic_, group_name_, consumer_name_, 50, 2000);
            if (msgs.empty()) {
                continue;
            }

            std::vector<Order> valid_orders;
            std::vector<std::string> msg_ids_to_ack;

            // 1. 在内存中完成解析与组装（不碰数据库）
            for (const auto& msg : msgs) {
                Order order;
                if (!parsePayload(msg.payload, order)) {
                    Logger::instance().error("Malformed payload: " + msg.id);
                    mq_->ack(topic_, group_name_, msg.id); // 脏数据直接 ACK
                    continue;
                }
                
                // 假设业务校验（库存等）已经在 Redis Lua 中保证了
                valid_orders.push_back(order);
                msg_ids_to_ack.push_back(msg.id);
            }

            // 2. 批量写入 MySQL (走连接池)
            if (!valid_orders.empty()) {
                int inserted_count = MySQLService::instance().batchCreateOrders(valid_orders);
                
                if (inserted_count >= 0) {
                    Logger::instance().info("Batch insert: Attempted=" + std::to_string(valid_orders.size()) 
                        + ", Inserted=" + std::to_string(inserted_count));
                    
                    // 3. 【核心改造】MySQL 落盘成功后，精准单播 SSE 通知用户
                    // 注意：这里只通知 user_id，不做全局广播（保护隐私 + 避免广播风暴）
                    for (const auto& order : valid_orders) {
                        std::string json_data = "{"
                            "\"order_id\":\"" + std::to_string(order.order_id) + "\","
                            "\"product_id\":\"" + order.product_id + "\","
                            "\"quantity\":" + std::to_string(order.quantity) + ","
                            "\"user_id\":\"" + order.user_id + "\""
                            "}";
                        SSEHandler::instance().sendToUser(order.user_id, "order_created", json_data);

                        // ── 结果缓存化：写入 Redis ──────────────────────
                        // 格式：seckill:result:{user_id}:{product_id} = order_id
                        // 过期时间：1小时（防止缓存无限增长）
                        std::string result_key = "seckill:result:" + order.user_id + ":" + order.product_id;
                        RedisService::instance().setex(result_key, 3600, std::to_string(order.order_id));
                    }
                    
                    // 4. 批量写入成功后，循环 ACK 确认这些消息
                    for (const auto& id : msg_ids_to_ack) {
                        mq_->ack(topic_, group_name_, id);
                    }
                } else {
                    // 整个批量 SQL 执行彻底崩了（如数据库宕机），不执行 ACK
                    // 消息保留在 PEL 中，等数据库恢复后重试
                    Logger::instance().error("System error: Batch insert failed. NO ACK sent.");
                }
            }

        }
        catch (const std::exception& e) {
            Logger::instance().error(std::string("Consumer Thread Error: ") + e.what());
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}

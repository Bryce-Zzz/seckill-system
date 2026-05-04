#include "service/timeout_checker.h"
#include "service/mysql_service.h"
#include "service/redis_service.h"
#include "common/config.h"
#include "common/logger.h"
#include <chrono>

void TimeoutChecker::init(int timeout_seconds, int check_interval) {
    timeout_seconds_ = timeout_seconds;
    check_interval_  = check_interval;
}

void TimeoutChecker::start() {
    running_ = true;
    worker_thread_ = std::thread(&TimeoutChecker::check_loop, this);
    Logger::instance().info("TimeoutChecker started: timeout=" +
                            std::to_string(timeout_seconds_) + "s, interval=" +
                            std::to_string(check_interval_) + "s");
}

void TimeoutChecker::stop() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    Logger::instance().info("TimeoutChecker stopped");
}

void TimeoutChecker::check_loop() {
    while (running_) {
        // 分段睡眠，便于快速响应 stop()
        for (int i = 0; i < check_interval_ && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!running_) break;

        try {
            // 拉取所有待支付订单（status=0）
            auto orders = MySQLService::instance().get_all_orders(1000);
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            int cancelled = 0;
            for (const auto& order : orders) {
                if (order.status != 0) continue;  // 只处理待支付

                int64_t age = now - order.created_at;
                if (age >= timeout_seconds_) {
                    cancel_expired_order(order.order_id, order.user_id,
                                        order.product_id, order.quantity);
                    ++cancelled;
                }
            }

            if (cancelled > 0) {
                Logger::instance().info("TimeoutChecker: cancelled " +
                                        std::to_string(cancelled) + " expired orders");
            }
        } catch (const std::exception& e) {
            Logger::instance().error("TimeoutChecker error: " + std::string(e.what()));
        }
    }
}

void TimeoutChecker::cancel_expired_order(uint64_t order_id,
                                           const std::string& user_id,
                                           const std::string& product_id,
                                           int quantity) {
    // 1. 更新 MySQL 状态为已取消(2)
    if (!MySQLService::instance().update_order_status(order_id, 2)) {
        Logger::instance().error("TimeoutChecker: failed to cancel order " + std::to_string(order_id));
        return;
    }

    // 2. 归还 Redis 库存
    auto& cfg = Config::instance();
    std::string stock_key     = cfg.seckill.stock_prefix + product_id;
    std::string order_set_key = cfg.seckill.order_set_prefix + product_id;

    auto& redis = RedisService::instance();
    redis.incrby(stock_key, quantity);        // 归还库存
    redis.srem(order_set_key, user_id);       // 移出已购集合

    Logger::instance().info("TimeoutChecker: order " + std::to_string(order_id) +
                            " expired, stock+" + std::to_string(quantity) +
                            " returned for product " + product_id);
}

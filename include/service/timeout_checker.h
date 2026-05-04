#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <cstdint>

/**
 * 订单超时检查器
 * 定时扫描未支付订单，超时后自动取消并归还 Redis 库存
 */
class TimeoutChecker {
public:
    static TimeoutChecker& instance() {
        static TimeoutChecker inst;
        return inst;
    }

    // timeout_seconds: 订单超时时长（秒），默认300s = 5分钟
    // check_interval: 检查周期（秒），默认30s
    void init(int timeout_seconds = 300, int check_interval = 30);
    void start();
    void stop();

private:
    TimeoutChecker() = default;
    ~TimeoutChecker() = default;
    TimeoutChecker(const TimeoutChecker&) = delete;
    TimeoutChecker& operator=(const TimeoutChecker&) = delete;

    void check_loop();
    void cancel_expired_order(uint64_t order_id,
                              const std::string& user_id,
                              const std::string& product_id,
                              int quantity);

    int timeout_seconds_ = 300;
    int check_interval_  = 30;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

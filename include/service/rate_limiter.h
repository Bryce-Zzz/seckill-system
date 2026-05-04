#ifndef SERVICE_RATE_LIMITER_H
#define SERVICE_RATE_LIMITER_H

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <deque>

/**
 * 双限流器：令牌桶 + 滑动窗口
 * 
 * 令牌桶：控制接口整体速率
 * 滑动窗口：防止突发流量刷接口
 */
class RateLimiter {
public:
    static RateLimiter& instance() {
        static RateLimiter inst;
        return inst;
    }

    void init(int token_rate, int token_capacity,
              int window_size, int window_max);

    /**
     * 热更新参数（从 Config 读取新值）
     * 不重启限流器对象，原子更新配置，工作线程无感知
     */
    void update_params(int token_rate, int token_capacity,
                       int window_size, int window_max);

    /**
     * 热更新用户维度限流参数
     */
    void update_user_params(int user_window_size, int user_window_max);

    /**
     * 检查是否允许通过（通用，按 key 隔离）
     * @param key 限流 key（通常为 IP 或 "uid:" 前缀的用户 ID）
     * @return true=允许, false=被限流
     */
    bool allow(const std::string& key);

    /**
     * 用户 ID 维度细粒度限流（专用方法）
     * 使用独立的 user_window_size_ / user_window_max_ 参数
     * @param user_id 用户 ID
     * @return true=允许, false=被限流（该用户请求过频）
     */
    bool allow_user(const std::string& user_id);

    /**
     * 获取当前剩余令牌数
     */
    int64_t get_available_tokens();

private:
    RateLimiter() = default;
    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    // 令牌桶
    void refill_tokens();

    // 滑动窗口记录
    struct WindowRecord {
        std::deque<int64_t> timestamps;  // 请求时间戳（使用 deque 实现真正的滑动）
    };

    std::unordered_map<std::string, WindowRecord> window_records_;
    std::mutex window_mutex_;

    // 配置
    int token_rate_ = 10000;            // 每秒生成令牌数
    int token_capacity_ = 50000;        // 令牌桶容量
    int window_size_ = 60;             // 滑动窗口大小(秒)
    int window_max_ = 100000;           // 滑动窗口最大请求数（全局/IP 维度）

    // 用户维度滑动窗口配置（独立于全局配置）
    int user_window_size_ = 10;         // 用户窗口大小(秒)
    int user_window_max_ = 5;           // 用户窗口最大请求数（单用户 10 秒内最多 5 次）

    // 状态
    std::atomic<int64_t> tokens_{0};
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex token_mutex_;
};

#endif // SERVICE_RATE_LIMITER_H

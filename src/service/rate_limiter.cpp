#include "service/rate_limiter.h"
#include "common/logger.h"
#include <algorithm>
#include <cmath>

void RateLimiter::init(int token_rate, int token_capacity,
                       int window_size, int window_max) {
    token_rate_ = token_rate;
    token_capacity_ = token_capacity;
    window_size_ = window_size;
    window_max_ = window_max;
    tokens_ = token_capacity_;
    last_refill_ = std::chrono::steady_clock::now();

    Logger::instance().info("RateLimiter initialized: rate=" +
                          std::to_string(token_rate) +
                          ", capacity=" + std::to_string(token_capacity));
}

void RateLimiter::update_params(int token_rate, int token_capacity,
                                 int window_size, int window_max) {
    std::lock_guard<std::mutex> lock(token_mutex_);

    // 防止 -1 穿透
    if (token_rate > 0) {
        token_rate_ = token_rate;
    }
    if (token_capacity > 0) {
        // ✅ 正骨手术1：容量就是容量，不要和当前tokens取min
        token_capacity_ = token_capacity;
        // 如果当前令牌数超过新容量，裁剪掉超出的部分
        if (tokens_.load() > token_capacity_) {
            tokens_.store(token_capacity_);
        }
    }
    window_size_ = window_size;
    window_max_ = window_max;

    // ✅ 关键：重置 refill 时间，防止热更新后 token 计算异常
    last_refill_ = std::chrono::steady_clock::now();

    Logger::instance().info("RateLimiter hot-updated: rate=" +
                          std::to_string(token_rate) +
                          ", capacity=" + std::to_string(token_capacity) +
                          ", window_max=" + std::to_string(window_max));
}

void RateLimiter::update_user_params(int user_window_size, int user_window_max) {
    std::lock_guard<std::mutex> lock(window_mutex_);

    if (user_window_size > 0) {
        user_window_size_ = user_window_size;
    }
    if (user_window_max > 0) {
        user_window_max_ = user_window_max;
    }

    Logger::instance().info("RateLimiter user params updated: user_window_size=" +
                          std::to_string(user_window_size_) +
                          ", user_window_max=" + std::to_string(user_window_max_));
}

bool RateLimiter::allow(const std::string& key) {
    // 1. 检查令牌桶
    {
        std::lock_guard<std::mutex> lock(token_mutex_);
        refill_tokens();

        if (tokens_ <= 0) {
            return false;  // 令牌不足，限流
        }
        tokens_--;
    }

    // 2. 检查滑动窗口（统一用毫秒）
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        auto& record = window_records_[key];
        auto& timestamps = record.timestamps;  // ⚠️ deque<int64_t>
        
        // 1. 真正的"滑动"：物理剔除过期数据，绝不留内存隐患
        int64_t cutoff_ms = now_ms - window_size_;
        while (!timestamps.empty() && timestamps.front() <= cutoff_ms) {
            timestamps.pop_front();
        }
        
        // 2. 致命防御：防止 YAML 配置解析成 0 导致 100% 锁死
        if (window_max_ <= 0) {
            return false; 
        }

        // 3. 拦截超发流量
        if (timestamps.size() >= (size_t)window_max_) {
            return false;
        }

        // 4. 安全放行并记录
        timestamps.push_back(now_ms);
    }

    return true;
}

bool RateLimiter::allow_user(const std::string& user_id) {
    // 注意：不走令牌桶（令牌桶已在 allow(ip) 中统一检查）
    // 这里只做用户维度的滑动窗口防护
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        std::string key = "uid:" + user_id;
        auto& record = window_records_[key];
        auto& timestamps = record.timestamps;

        // 1. 真正的滑动：物理剔除过期数据（user_window_size_ 单位是秒，转毫秒）
        int64_t cutoff_ms = now_ms - (int64_t)user_window_size_ * 1000LL;
        while (!timestamps.empty() && timestamps.front() <= cutoff_ms) {
            timestamps.pop_front();
        }

        // 2. 防御：防止配置为 0
        if (user_window_max_ <= 0) {
            return false;
        }

        // 3. 拦截超发流量
        if (timestamps.size() >= (size_t)user_window_max_) {
            return false;
        }

        // 4. 安全放行并记录
        timestamps.push_back(now_ms);
    }

    return true;
}

void RateLimiter::refill_tokens() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_refill_).count();

    if (elapsed > 0) {
        // ✅ 正骨手术2：先乘后除，使用 int64_t 防止溢出
        int64_t tokens_to_add = (elapsed * token_rate_) / 1000;
        
        int64_t new_tokens = tokens_.load() + tokens_to_add;
        if (new_tokens > token_capacity_) {
            new_tokens = token_capacity_;
        }
        tokens_.store(new_tokens);

        // 🌟 核心修复：不要等于 now！只加上被消耗的毫秒数，保留微秒尾巴！
        last_refill_ += std::chrono::milliseconds(elapsed);
    }
}

int64_t RateLimiter::get_available_tokens() {
    std::lock_guard<std::mutex> lock(token_mutex_);
    refill_tokens();
    return tokens_;
}

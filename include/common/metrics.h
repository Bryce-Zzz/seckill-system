#pragma once
#include <atomic>
#include <chrono>
#include <string>

/**
 * 轻量级 Prometheus 指标收集器（无外部依赖）
 * 暴露 /metrics 端点，兼容 Prometheus text format
 */
class Metrics {
public:
    static Metrics& instance() {
        static Metrics inst;
        return inst;
    }

    // ---- 计数器 ----
    void inc_requests_total()       { requests_total_.fetch_add(1, std::memory_order_relaxed); }
    void inc_seckill_success()      { seckill_success_.fetch_add(1, std::memory_order_relaxed); }
    void inc_seckill_fail_sold()    { seckill_fail_sold_.fetch_add(1, std::memory_order_relaxed); }
    void inc_seckill_fail_dup()     { seckill_fail_dup_.fetch_add(1, std::memory_order_relaxed); }
    void inc_seckill_fail_ratelimit(){ seckill_fail_ratelimit_.fetch_add(1, std::memory_order_relaxed); }
    void inc_orders_paid()          { orders_paid_.fetch_add(1, std::memory_order_relaxed); }
    void inc_orders_cancelled()     { orders_cancelled_.fetch_add(1, std::memory_order_relaxed); }
    void inc_timeout_cancelled()    { timeout_cancelled_.fetch_add(1, std::memory_order_relaxed); }

    // ---- 延迟采样（简单累加，计算平均值）----
    void observe_latency_us(long long us) {
        latency_sum_us_.fetch_add(us, std::memory_order_relaxed);
        latency_count_.fetch_add(1,  std::memory_order_relaxed);
    }

    // ---- 生成 Prometheus text format ----
    std::string dump() const;

private:
    Metrics() = default;

    std::atomic<long long> requests_total_{0};
    std::atomic<long long> seckill_success_{0};
    std::atomic<long long> seckill_fail_sold_{0};
    std::atomic<long long> seckill_fail_dup_{0};
    std::atomic<long long> seckill_fail_ratelimit_{0};
    std::atomic<long long> orders_paid_{0};
    std::atomic<long long> orders_cancelled_{0};
    std::atomic<long long> timeout_cancelled_{0};
    std::atomic<long long> latency_sum_us_{0};
    std::atomic<long long> latency_count_{0};

    int64_t start_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
};

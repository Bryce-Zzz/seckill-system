#include "common/metrics.h"
#include <sstream>
#include <chrono>

std::string Metrics::dump() const {
    std::ostringstream oss;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // uptime
    oss << "# HELP seckill_uptime_seconds Seconds since server start\n"
        << "# TYPE seckill_uptime_seconds gauge\n"
        << "seckill_uptime_seconds " << (now - start_time_) << "\n\n";

    // HTTP 请求总量
    oss << "# HELP seckill_requests_total Total HTTP requests received\n"
        << "# TYPE seckill_requests_total counter\n"
        << "seckill_requests_total " << requests_total_.load() << "\n\n";

    // 秒杀结果
    oss << "# HELP seckill_buy_total Seckill buy attempts by result\n"
        << "# TYPE seckill_buy_total counter\n"
        << "seckill_buy_total{result=\"success\"}   " << seckill_success_.load()       << "\n"
        << "seckill_buy_total{result=\"sold_out\"}  " << seckill_fail_sold_.load()     << "\n"
        << "seckill_buy_total{result=\"duplicate\"} " << seckill_fail_dup_.load()      << "\n"
        << "seckill_buy_total{result=\"ratelimit\"} " << seckill_fail_ratelimit_.load()<< "\n\n";

    // 订单状态变更
    oss << "# HELP seckill_orders_paid_total Orders successfully paid\n"
        << "# TYPE seckill_orders_paid_total counter\n"
        << "seckill_orders_paid_total " << orders_paid_.load() << "\n\n";

    oss << "# HELP seckill_orders_cancelled_total Orders cancelled by user\n"
        << "# TYPE seckill_orders_cancelled_total counter\n"
        << "seckill_orders_cancelled_total " << orders_cancelled_.load() << "\n\n";

    oss << "# HELP seckill_orders_timeout_total Orders auto-cancelled due to timeout\n"
        << "# TYPE seckill_orders_timeout_total counter\n"
        << "seckill_orders_timeout_total " << timeout_cancelled_.load() << "\n\n";

    // 平均请求延迟
    long long cnt = latency_count_.load();
    double avg_ms = cnt > 0 ? (latency_sum_us_.load() / 1000.0 / cnt) : 0.0;
    oss << "# HELP seckill_request_latency_avg_ms Average request latency in ms\n"
        << "# TYPE seckill_request_latency_avg_ms gauge\n"
        << "seckill_request_latency_avg_ms " << avg_ms << "\n";

    return oss.str();
}

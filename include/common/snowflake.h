#pragma once
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <cstdint>

/**
 * Snowflake 雪花算法 ID 生成器
 *
 * 64-bit 布局：
 *   [ 1 bit 符号 ][ 41 bit 毫秒时间戳 ][ 10 bit 机器ID ][ 12 bit 序列号 ]
 *
 * 单机单实例 QPS 上限：4096 × 1000 = 4,096,000 / s
 * 支持机器数量：1024 台
 * 可用年限：约 69 年（自 epoch 起）
 *
 * 线程安全：std::mutex 保护序列号自增，适合 32 核高并发场景。
 */
class Snowflake {
public:
    // 构造函数：workerId 由调用方传入，支持多实例部署
    // workerId 范围: [0, 1023]
    explicit Snowflake(uint64_t workerId) : workerId_(workerId) {
        if (workerId >= (1ULL << kWorkerIdBits)) {
            throw std::invalid_argument(
                "Snowflake workerId out of range: max=" +
                std::to_string((1ULL << kWorkerIdBits) - 1));
        }
    }

    // 暴露 workerId 供启动日志打印
    uint64_t getWorkerId() const { return workerId_; }

    // 生成下一个唯一 ID（线程安全）
    uint64_t nextId() {
        std::lock_guard<std::mutex> lock(mtx_);

        uint64_t ts = currentTimestampMs();

        // 时钟回拨保护：容忍 5ms 以内的回拨（自旋等待）；超限则抛异常
        if (ts < lastTimestamp_) {
            uint64_t offset = lastTimestamp_ - ts;
            if (offset <= kClockBackoffToleranceMs) {
                // 小幅回拨：原地等追上，不抛异常，不丢 ID
                ts = waitNextMillis(lastTimestamp_);
            } else {
                throw std::runtime_error(
                    "Snowflake: clock moved backwards by " +
                    std::to_string(offset) + " ms (exceeds tolerance of " +
                    std::to_string(kClockBackoffToleranceMs) + " ms)");
            }
        }

        if (ts == lastTimestamp_) {
            // 同毫秒内序列号自增
            sequence_ = (sequence_ + 1) & kSequenceMask;
            if (sequence_ == 0) {
                // 序列号溢出：等到下一毫秒
                ts = waitNextMillis(lastTimestamp_);
            }
        } else {
            // 新毫秒，序列号归零
            sequence_ = 0;
        }

        lastTimestamp_ = ts;

        // 位运算拼装 64-bit ID
        return ((ts - kEpoch) << kTimestampShift)
             | (workerId_       << kWorkerIdShift)
             | sequence_;
    }

private:
    // ── 位宽常量 ──────────────────────────────────────────────
    static constexpr int      kWorkerIdBits   = 10;   // 机器ID位宽（最多 1024 台）
    static constexpr int      kSequenceBits   = 12;   // 序列号位宽（毫秒内最多 4096）
    static constexpr int      kWorkerIdShift  = kSequenceBits;
    static constexpr int      kTimestampShift = kSequenceBits + kWorkerIdBits;
    static constexpr uint64_t kSequenceMask   = (1ULL << kSequenceBits) - 1;

    // 自定义 epoch：2024-01-01 00:00:00 UTC（毫秒）
    // 相较 Unix epoch 可多使用约 54 年
    static constexpr uint64_t kEpoch = 1704067200000ULL;

    // 时钟回拨容忍阈值（毫秒）：NTP 小幅抖动时自旋等待，不丢 ID
    static constexpr uint64_t kClockBackoffToleranceMs = 5;

    // ── 成员变量 ──────────────────────────────────────────────
    const uint64_t workerId_;
    uint64_t       sequence_      = 0;
    uint64_t       lastTimestamp_ = 0;
    std::mutex     mtx_;

    // ── 内部工具 ──────────────────────────────────────────────
    static uint64_t currentTimestampMs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
            .count());
    }

    uint64_t waitNextMillis(uint64_t lastTs) const {
        uint64_t ts = currentTimestampMs();
        while (ts <= lastTs) {
            ts = currentTimestampMs();
        }
        return ts;
    }
};

// 全局 Snowflake 实例声明（定义见 main.cpp）
extern Snowflake g_snow_flake;

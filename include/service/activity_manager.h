#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <optional>
#include <cstdint>
#include "service/mysql_service.h"

/**
 * ActivityManager - 秒杀活动状态内存缓存
 * 
 * 功能：
 * 1. 从 MySQL seckill_activities 表加载活动数据到内存
 * 2. 提供纳秒级活动状态查询（纯内存操作，零 IO）
 * 3. 后台线程定期同步 MySQL（默认 30 秒）
 * 4. 线程安全（std::mutex）
 *
 * Activity 结构体定义在 mysql_service.h 中，此处直接引用。
 */
class ActivityManager {
public:
    static ActivityManager& instance() {
        static ActivityManager inst;
        return inst;
    }

    // 启动后台同步线程
    void start(int sync_interval_seconds = 30);

    // 停止后台同步线程
    void stop();

    // 手动触发一次同步（供 SIGHUP 热更新调用）
    void sync_once();

    // 纳秒级活动状态查询（纯内存，零 IO）
    // 返回 std::nullopt 表示活动不存在或已结束
    std::optional<Activity> get_active_activity(const std::string& product_id);

private:
    ActivityManager() = default;
    ~ActivityManager() { stop(); }

    // 后台线程函数：定期从 MySQL 同步活动数据
    void background_sync_loop(int interval_seconds);

    // 从 MySQL 加载全量活动数据
    bool load_from_mysql();

    // 内存中的数据：product_id -> Activity
    std::unordered_map<std::string, Activity> activities_;
    std::mutex                                   mutex_;

    // 后台线程控制
    bool        running_ = false;
    std::thread sync_thread_;
};

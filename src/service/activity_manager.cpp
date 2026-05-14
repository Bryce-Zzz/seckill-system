#include "service/activity_manager.h"
#include "service/mysql_service.h"
#include "service/mysql_pool.h"
#include "common/logger.h"
#include "common/config.h"
#include <chrono>
#include <cstring>

void ActivityManager::start(int sync_interval_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;

    // 启动后台同步线程，首次同步在线程内执行，不阻塞 main()
    sync_thread_ = std::thread([this, sync_interval_seconds]() {
        // 线程启动后立即同步一次
        sync_once();
        background_sync_loop(sync_interval_seconds);
    });
    sync_thread_.detach();

    Logger::instance().info("ActivityManager: background sync thread started");
}

void ActivityManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
    Logger::instance().info("ActivityManager: background sync thread stopped");
}

void ActivityManager::sync_once() {
    // 调用方必须持有 mutex_
    if (!load_from_mysql()) {
        Logger::instance().error("ActivityManager: sync_once failed");
    }
}

std::optional<Activity> ActivityManager::get_active_activity(const std::string& product_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = activities_.find(product_id);
    if (it == activities_.end()) {
        return std::nullopt;
    }
    const Activity& act = it->second;
    if (act.status != 1) {
        return std::nullopt;
    }
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    if (now_ms < act.start_time || now_ms > act.end_time) {
        return std::nullopt;
    }
    return act;
}

void ActivityManager::background_sync_loop(int interval_seconds) {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) break;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            load_from_mysql();
        }

        for (int i = 0; i < interval_seconds; ++i) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_) return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    Logger::instance().info("ActivityManager: background sync loop exited");
}

bool ActivityManager::load_from_mysql() {
    // 调用方必须持有 mutex_
    MYSQL* conn = MySQLPool::getInstance().getConnection();
    if (!conn) {
        Logger::instance().error("ActivityManager: failed to get MySQL connection");
        return false;
    }

    const char* sql =
        "SELECT id, product_id, seckill_price, stock, start_time, end_time, status "
        "FROM seckill_activities WHERE status = 1";

    if (mysql_real_query(conn, sql, strlen(sql)) != 0) {
        Logger::instance().error(
            "ActivityManager: load SQL failed: " + std::string(mysql_error(conn)));
        MySQLPool::getInstance().releaseConnection(conn);
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        Logger::instance().error(
            "ActivityManager: mysql_store_result failed: " + std::string(mysql_error(conn)));
        MySQLPool::getInstance().releaseConnection(conn);
        return false;
    }

    std::unordered_map<std::string, Activity> new_map;
    MYSQL_ROW row;
    int count = 0;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        Activity act;
        act.id            = row[0] ? row[0] : "";
        act.product_id    = row[1] ? row[1] : "";
        act.seckill_price = row[2] ? std::atof(row[2]) : 0.0;
        act.stock         = row[3] ? std::atoi(row[3]) : 0;
        act.start_time    = row[4] ? std::atoll(row[4]) : 0;
        act.end_time      = row[5] ? std::atoll(row[5]) : 0;
        act.status        = row[6] ? static_cast<int8_t>(std::atoi(row[6])) : 0;
        new_map[act.product_id] = std::move(act);
        ++count;
    }
    mysql_free_result(res);
    MySQLPool::getInstance().releaseConnection(conn);

    activities_ = std::move(new_map);
    Logger::instance().info("ActivityManager: synced " + std::to_string(count) + " active activities");
    return true;
}

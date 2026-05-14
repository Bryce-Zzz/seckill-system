#pragma once
#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>
#include <string>
#include "common/logger.h"

class MySQLPool {
public:
    static MySQLPool& getInstance() {
        static MySQLPool instance;
        return instance;
    }

    // 修复 1：增加 port 参数，保存配置以供后续断线重连使用
    void init(const std::string& host, const std::string& user, const std::string& pwd, const std::string& db, int port = 3306, int pool_size = 16) {
        std::lock_guard<std::mutex> lock(mutex_);
        host_ = host; user_ = user; pwd_ = pwd; db_ = db; port_ = port;

        for (int i = 0; i < pool_size; ++i) {
            MYSQL* conn = createConnection();
            if (conn) {
                pool_.push(conn);
            }
        }
    }

    MYSQL* getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 修复 7：带 Predicate 的 wait_for，彻底解决虚假唤醒 (Spurious Wakeup) Bug
        if (!cond_.wait_for(lock, std::chrono::seconds(3), [this] { return !pool_.empty(); })) {
            Logger::instance().error("MySQLPool timeout: No available connections!");
            return nullptr;
        }

        MYSQL* conn = pool_.front();
        pool_.pop();

        // 修复 8：极其严谨的 ping 与断线重连机制
        if (mysql_ping(conn) != 0) {
            Logger::instance().warn("MySQL connection dead, forcing hard reconnect...");
            mysql_close(conn); // 彻底销毁死连接
            
            conn = createConnection(); // 重新走握手流程
            if (!conn) {
                Logger::instance().error("Hard reconnect failed. Dropping this request.");
                return nullptr;
            }
        }

        return conn;
    }

    void releaseConnection(MYSQL* conn) {
        if (conn) {
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push(conn);
            cond_.notify_one(); 
        }
    }

private:
    MySQLPool() = default;
    
    // 修复 3：优雅的析构函数，遍历关闭所有网络描述符
    ~MySQLPool() { 
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            MYSQL* conn = pool_.front();
            pool_.pop();
            if (conn) {
                mysql_close(conn);
            }
        }
    }

    // 提取创建连接的通用逻辑
    MYSQL* createConnection() {
        // 修复 6：检查 mysql_init 的返回值
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            Logger::instance().error("mysql_init failed out of memory.");
            return nullptr;
        }

        // MySQL 8.3+ 已移除 MYSQL_OPT_RECONNECT，直接注释掉
        // char reconnect = 1;
        // mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), pwd_.c_str(), db_.c_str(), port_, nullptr, 0)) {
            Logger::instance().error("MySQL connect failed: " + std::string(mysql_error(conn)));
            mysql_close(conn);
            return nullptr;
        }
        return conn;
    }
    
    std::queue<MYSQL*> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;

    // 必须缓存配置，否则重连时连去哪都不知道
    std::string host_, user_, pwd_, db_;
    int port_;
};

// 修复 2：加上 explicit，防止手抖引发隐式转换
class ConnectionGuard {
public:
    explicit ConnectionGuard(MySQLPool& pool) : pool_(pool) {
        conn_ = pool_.getConnection();
    }
    ~ConnectionGuard() {
        if (conn_) {
            pool_.releaseConnection(conn_);
        }
    }
    MYSQL* get() const { return conn_; }
    
    // 禁用拷贝构造和赋值操作符 (RAII 类的基本素养)
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
private:
    MySQLPool& pool_;
    MYSQL* conn_;
};

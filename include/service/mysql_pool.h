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
        
        if (!cond_.wait_for(lock, std::chrono::seconds(3), [this] { return !pool_.empty(); })) {
            Logger::instance().error("MySQLPool timeout: No available connections!");
            return nullptr;
        }

        MYSQL* conn = pool_.front();
        pool_.pop();

        if (mysql_ping(conn) != 0) {
            Logger::instance().warn("MySQL connection dead, forcing hard reconnect...");
            mysql_close(conn);
            
            conn = createConnection();
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

    MYSQL* createConnection() {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            Logger::instance().error("mysql_init failed out of memory.");
            return nullptr;
        }

        char reconnect = 1;
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

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

    std::string host_, user_, pwd_, db_;
    int port_;
};

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
    
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
private:
    MySQLPool& pool_;
    MYSQL* conn_;
};

#pragma once

#include <mysql/mysql.h>
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <stdexcept>
#include <atomic>
#include "common/logger.h"

class MySQLPool {
private:
    std::queue<MYSQL*> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::string host_, user_, pass_, db_;
    int port_;
    int pool_size_;
    std::atomic<int> active_count_{0};

    // 创建单个 MySQL 连接
    MYSQL* createConnection() {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            Logger::instance().error("MySQL Pool: mysql_init failed");
            return nullptr;
        }
        
        // 设置连接超时（防止永久阻塞）
        int conn_timeout = 5;
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &conn_timeout);
        mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &conn_timeout);
        mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &conn_timeout);
        
        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), pass_.c_str(), 
                                 db_.c_str(), port_, nullptr, 0)) {
            // 用 stderr + flush 确保错误立即输出
            std::cerr << "[MYSQL_POOL_ERROR] connect failed: " << mysql_error(conn) << std::endl;
            std::cerr.flush();
            mysql_close(conn);
            return nullptr;
        }
        
        std::cerr << "[MYSQL_POOL] connection " << host_ << ":" << port_ << " OK" << std::endl;
        std::cerr.flush();
        return conn;
    }

    MySQLPool() = default;
    ~MySQLPool() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!pool_.empty()) {
            mysql_close(pool_.front());
            pool_.pop();
        }
    }

    // 禁用拷贝
    MySQLPool(const MySQLPool&) = delete;
    MySQLPool& operator=(const MySQLPool&) = delete;

public:
    static MySQLPool& instance() {
        static MySQLPool instance;
        return instance;
    }

    void init(const std::string& host, const std::string& user, const std::string& pass, 
              const std::string& db, int port, int pool_size = 10) {
        host_ = host;
        user_ = user;
        pass_ = pass;
        db_ = db;
        port_ = port;
        pool_size_ = pool_size;
        
        // 预创建连接
        for (int i = 0; i < pool_size_; ++i) {
            MYSQL* conn = createConnection();
            if (conn) {
                pool_.push(conn);
            }
        }
        
        Logger::instance().info("MySQL Pool initialized: " + std::to_string(pool_.size()) + " connections ready");
    }

    // 阻塞获取连接（带健康检查）
    MYSQL* getConnection() {
        std::unique_lock<std::mutex> lock(mtx_);
        
        // 等待直到池中有连接
        if (!cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !pool_.empty(); })) {
            throw std::runtime_error("MySQL Pool timeout: No available connections");
        }
        
        MYSQL* conn = pool_.front();
        pool_.pop();
        active_count_++;
        
        // 🚨 核心优化：出池前的健康检查
        // mysql_ping 会尝试与 MySQL 服务器通信，如果连接已断开，会返回非 0 值
        if (mysql_ping(conn) != 0) {
            Logger::instance().warn("MySQL connection dead, recycling and creating a new one...");
            mysql_close(conn);          // 1. 关闭死连接
            conn = createConnection();  // 2. 重新创建一个全新的连接
            
            if (!conn) {
                active_count_--;
                throw std::runtime_error("MySQL Pool: Failed to recreate connection during health check.");
            }
        }
        
        return conn;
    }

    // 归还连接
    void releaseConnection(MYSQL* conn) {
        if (!conn) return;
        
        std::lock_guard<std::mutex> lock(mtx_);
        active_count_--;
        
        // 检查连接是否有效（归还时也检查）
        if (mysql_ping(conn) != 0) {
            Logger::instance().warn("MySQL Pool: returning dead connection, recreating...");
            mysql_close(conn);
            conn = createConnection();
        }
        
        if (conn) {
            pool_.push(conn);
        }
        
        cv_.notify_one();
    }

    // 获取池状态
    int available() const { return pool_.size(); }
    int active() const { return active_count_.load(); }
};

// RAII 守卫：离开作用域时自动归还连接，防止泄露！
class ConnectionGuard {
private:
    MYSQL* conn_;
    bool released_ = false;
public:
    ConnectionGuard() : conn_(MySQLPool::instance().getConnection()) {}
    
    ~ConnectionGuard() {
        if (!released_ && conn_) {
            MySQLPool::instance().releaseConnection(conn_);
        }
    }
    
    MYSQL* get() { return conn_; }
    
    // 手动释放（如果需要提前还连接）
    void release() {
        if (!released_ && conn_) {
            MySQLPool::instance().releaseConnection(conn_);
            released_ = true;
        }
    }
    
    // 禁用拷贝
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
};

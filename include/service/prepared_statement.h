#pragma once
#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include "common/logger.h"

class PreparedStatement {
public:
    PreparedStatement(MYSQL* conn, const std::string& sql) : stmt_(nullptr) {
        stmt_ = mysql_stmt_init(conn);
        if (!stmt_) throw std::runtime_error("mysql_stmt_init failed");
        
        if (mysql_stmt_prepare(stmt_, sql.c_str(), sql.length()) != 0) {
            std::string err = mysql_stmt_error(stmt_);
            mysql_stmt_close(stmt_);
            throw std::runtime_error("mysql_stmt_prepare failed: " + err);
        }
        
        unsigned long param_count = mysql_stmt_param_count(stmt_);
        binds_.resize(param_count);
        memset(binds_.data(), 0, sizeof(MYSQL_BIND) * param_count);
    }

    ~PreparedStatement() {
        if (stmt_) mysql_stmt_close(stmt_);
    }

    void bindString(int index, const std::string& value) {
        checkIndex(index);
        string_buffers_.push_back(value);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = (void*)string_buffers_.back().c_str();
        bind.buffer_length = string_buffers_.back().length();
        bind.is_null = 0;
        bind.length = 0;
    }

    void bindUint64(int index, uint64_t value) {
        checkIndex(index);
        uint64_buffers_.push_back(value);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type = MYSQL_TYPE_LONGLONG;
        bind.buffer = (void*)&uint64_buffers_.back();
        bind.is_unsigned = 1;
        bind.is_null = 0;
        bind.length = 0;
    }
    
    void bindInt(int index, int value) {
        checkIndex(index);
        int_buffers_.push_back(value);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type = MYSQL_TYPE_LONG;
        bind.buffer = (void*)&int_buffers_.back();
        bind.is_null = 0;
        bind.length = 0;
    }
    
    void bindInt64(int index, int64_t value) {
        checkIndex(index);
        int64_buffers_.push_back(value);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type = MYSQL_TYPE_LONGLONG;
        bind.buffer = (void*)&int64_buffers_.back();
        bind.is_null = 0;
        bind.length = 0;
    }
    
    void bindDouble(int index, double value) {
        checkIndex(index);
        double_buffers_.push_back(value);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type = MYSQL_TYPE_DOUBLE;
        bind.buffer = (void*)&double_buffers_.back();
        bind.is_null = 0;
        bind.length = 0;
    }

    bool executeUpdate() {
        if (mysql_stmt_bind_param(stmt_, binds_.data()) != 0) {
            Logger::instance().error("bind failed: " + std::string(mysql_stmt_error(stmt_)));
            return false;
        }
        if (mysql_stmt_execute(stmt_) != 0) {
            Logger::instance().error("execute failed: " + std::string(mysql_stmt_error(stmt_)));
            return false;
        }
        return true;
    }

private:
    MYSQL_STMT* stmt_;
    std::vector<MYSQL_BIND> binds_;
    std::vector<std::string> string_buffers_;
    std::vector<uint64_t> uint64_buffers_;
    std::vector<int> int_buffers_;
    std::vector<int64_t> int64_buffers_;
    std::vector<double> double_buffers_;
    
    void checkIndex(int index) {
        if (index < 0 || index >= static_cast<int>(binds_.size())) {
            throw std::out_of_range("PreparedStatement bind index out of range");
        }
    }
};

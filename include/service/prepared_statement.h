#pragma once
#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <memory>
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
        for (char* p : string_raw_ptrs_) delete[] p;
        for (int* p : int_raw_ptrs_) delete p;
        for (int64_t* p : int64_raw_ptrs_) delete p;
        for (uint64_t* p : uint64_raw_ptrs_) delete p;
        for (double* p : double_raw_ptrs_) delete p;
    }

    // 绑定字符串（堆分配，防 vector 扩容导致指针失效）
    void bindString(int index, const std::string& value) {
        checkIndex(index);
        size_t len = value.length();
        char* buf = new char[len + 1];
        memcpy(buf, value.c_str(), len + 1);
        string_raw_ptrs_.push_back(buf);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type   = MYSQL_TYPE_STRING;
        bind.buffer        = buf;
        bind.buffer_length = static_cast<unsigned long>(len);
        bind.is_null       = 0;
        bind.length        = 0;
    }

    // 绑定 64 位无符号整数（堆分配）
    void bindUint64(int index, uint64_t value) {
        checkIndex(index);
        uint64_t* buf = new uint64_t(value);
        uint64_raw_ptrs_.push_back(buf);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type   = MYSQL_TYPE_LONGLONG;
        bind.buffer        = (void*)buf;
        bind.is_unsigned  = 1;
        bind.is_null       = 0;
        bind.length        = 0;
    }

    // 绑定 32 位有符号整数（堆分配）
    void bindInt(int index, int value) {
        checkIndex(index);
        int* buf = new int(value);
        int_raw_ptrs_.push_back(buf);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type   = MYSQL_TYPE_LONG;
        bind.buffer        = (void*)buf;
        bind.is_null       = 0;
        bind.length        = 0;
    }

    // 绑定 64 位有符号整数（堆分配）
    void bindInt64(int index, int64_t value) {
        checkIndex(index);
        int64_t* buf = new int64_t(value);
        int64_raw_ptrs_.push_back(buf);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type   = MYSQL_TYPE_LONGLONG;
        bind.buffer        = (void*)buf;
        bind.is_null       = 0;
        bind.length        = 0;
    }

    // 绑定双精度浮点数（堆分配）
    void bindDouble(int index, double value) {
        checkIndex(index);
        double* buf = new double(value);
        double_raw_ptrs_.push_back(buf);
        MYSQL_BIND& bind = binds_[index];
        bind.buffer_type   = MYSQL_TYPE_DOUBLE;
        bind.buffer        = (void*)buf;
        bind.is_null       = 0;
        bind.length        = 0;
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
    // 堆分配缓冲区，析构时释放，指针永不过期
    std::vector<char*>    string_raw_ptrs_;
    std::vector<int*>      int_raw_ptrs_;
    std::vector<int64_t*>  int64_raw_ptrs_;
    std::vector<uint64_t*> uint64_raw_ptrs_;
    std::vector<double*>   double_raw_ptrs_;

    void checkIndex(int index) {
        if (index < 0 || index >= static_cast<int>(binds_.size())) {
            throw std::out_of_range("PreparedStatement bind index out of range");
        }
    }
};

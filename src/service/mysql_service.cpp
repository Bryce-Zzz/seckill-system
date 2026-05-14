#include "service/mysql_service.h"
#include "service/mysql_pool.h"
#include "service/prepared_statement.h"
#include "common/logger.h"
#include <iostream>
#include <sstream>
#include <optional>

// 辅助函数：转义字符串防止 SQL 注入（用于读操作查询条件）
static std::string escape_string(MYSQL* conn, const std::string& str) {
    if (str.empty()) return "";
    std::vector<char> buffer(str.length() * 2 + 1);
    unsigned long len = mysql_real_escape_string(conn, buffer.data(), str.c_str(), str.length());
    return std::string(buffer.data(), len);
}

bool MySQLService::init(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database, int pool_size) {
    MySQLPool::getInstance().init(host, user, password, database, port, pool_size);
    Logger::instance().info("MySQL connected: " + host + ":" + std::to_string(port));
    return true;
}


// 原子扣除商品库存（启动秒杀时划拨）
bool MySQLService::decrease_product_stock(const std::string& product_id, int amount) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;
        
        std::string sql = "UPDATE products SET stock = stock - ?, updated_at = ? WHERE id = ? AND stock >= ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, amount);
        stmt.bindInt64(1, time(nullptr) * 1000);
        stmt.bindString(2, product_id);
        stmt.bindInt(3, amount);
        
        int affected = stmt.executeUpdate();
        return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("decrease_product_stock exception: " + std::string(e.what()));
        return false;
    }
}

// 原子增加商品库存（停止秒杀时返还未售出）
bool MySQLService::add_product_stock(const std::string& product_id, int amount) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;
        
        std::string sql = "UPDATE products SET stock = stock + ?, updated_at = ? WHERE id = ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, amount);
        stmt.bindInt64(1, time(nullptr) * 1000);
        stmt.bindString(2, product_id);
        
        int affected = stmt.executeUpdate();
        return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("add_product_stock exception: " + std::string(e.what()));
        return false;
    }
}

void MySQLService::close() {
    // MySQLPool 析构时会自动关闭所有连接
}

// Product 写操作 - 使用 PreparedStatement
bool MySQLService::add_product(const Product& product) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql = "INSERT INTO products (id, name, stock, price, created_at) VALUES (?, ?, ?, ?, ?)";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, product.id);
        stmt.bindString(1, product.name);
        stmt.bindInt(2, product.stock);
        stmt.bindDouble(3, product.price);
        stmt.bindInt64(4, product.created_at);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("add_product exception: " + std::string(e.what()));
        return false;
    }
}

bool MySQLService::update_product_stock(const std::string& product_id, int stock) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql = "UPDATE products SET stock = ?, updated_at = ? WHERE id = ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, stock);
        stmt.bindInt64(1, time(nullptr) * 1000);
        stmt.bindString(2, product_id);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("update_product_stock exception: " + std::string(e.what()));
        return false;
    }
}

bool MySQLService::delete_product(const std::string& product_id) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql = "DELETE FROM products WHERE id = ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, product_id);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("delete_product exception: " + std::string(e.what()));
        return false;
    }
}

// Product 读操作 - 使用 mysql_query + escape_string
std::vector<Product> MySQLService::get_all_products() {
    std::vector<Product> products;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return products;

    if (mysql_query(conn, "SELECT id, name, stock, price, created_at, updated_at FROM products")) {
        return products;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return products;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Product p;
        p.id = row[0] ? row[0] : "";
        p.name = row[1] ? row[1] : "";
        p.stock = row[2] ? std::stoi(row[2]) : 0;
        p.price = row[3] ? std::stod(row[3]) : 0.0;
        p.created_at = row[4] ? std::stoll(row[4]) : 0;
        p.updated_at = row[5] ? std::stoll(row[5]) : 0;
        products.push_back(p);
    }

    mysql_free_result(res);
    return products;
}

std::optional<Product> MySQLService::get_product(const std::string& product_id) {
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return std::nullopt;

    std::string escaped_id = escape_string(conn, product_id);
    std::string sql = "SELECT id, name, stock, price, created_at, updated_at FROM products WHERE id='" + escaped_id + "'";
    
    if (mysql_query(conn, sql.c_str())) {
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Product> result = std::nullopt;
    if (row) {
        Product product;
        product.id = row[0] ? row[0] : "";
        product.name = row[1] ? row[1] : "";
        product.stock = row[2] ? std::stoi(row[2]) : 0;
        product.price = row[3] ? std::stod(row[3]) : 0.0;
        product.created_at = row[4] ? std::stoll(row[4]) : 0;
        product.updated_at = row[5] ? std::stoll(row[5]) : 0;
        result = product;
    }

    mysql_free_result(res);
    return result;
}

// Product tuple 版本 - 写操作用 PreparedStatement
bool MySQLService::create_product(const std::string& id, const std::string& name, int stock, double price) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql = "INSERT INTO products (id, name, stock, price, created_at) VALUES (?, ?, ?, ?, ?)";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, id);
        stmt.bindString(1, name);
        stmt.bindInt(2, stock);
        stmt.bindDouble(3, price);
        stmt.bindInt64(4, time(nullptr) * 1000);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("create_product exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::tuple<std::string, std::string, int, double>> MySQLService::get_all_products_tuple() {
    std::vector<std::tuple<std::string, std::string, int, double>> products;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return products;

    if (mysql_query(conn, "SELECT id, name, stock, price FROM products")) {
        return products;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return products;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string id = row[0] ? row[0] : "";
        std::string name = row[1] ? row[1] : "";
        int stock = row[2] ? std::stoi(row[2]) : 0;
        double price = row[3] ? std::stod(row[3]) : 0.0;
        products.emplace_back(id, name, stock, price);
    }

    mysql_free_result(res);
    return products;
}

std::tuple<std::string, std::string, int, double> MySQLService::get_product_by_id(const std::string& id) {
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return {"", "", 0, 0.0};

    std::string escaped_id = escape_string(conn, id);
    std::string sql = "SELECT id, name, stock, price FROM products WHERE id='" + escaped_id + "'";
    
    if (mysql_query(conn, sql.c_str())) {
        return {"", "", 0, 0.0};
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return {"", "", 0, 0.0};

    MYSQL_ROW row = mysql_fetch_row(res);
    std::tuple<std::string, std::string, int, double> result;
    if (row) {
        result = {
            row[0] ? row[0] : "",
            row[1] ? row[1] : "",
            row[2] ? std::stoi(row[2]) : 0,
            row[3] ? std::stod(row[3]) : 0.0
        };
    }

    mysql_free_result(res);
    return result;
}

// Order 操作
bool MySQLService::create_order(const Order& order) {
    auto result = create_order_with_error_type(order);
    return result.success;
}

// 区分错误类型的订单创建 - 使用 PreparedStatement
CreateOrderResult MySQLService::create_order_with_error_type(const Order& order) {
    CreateOrderResult result;

    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) {
        result.error_type = OrderErrorType::SYSTEM_ERROR;
        result.error_message = "Failed to get MySQL connection";
        return result;
    }

    // 开始事务
    if (mysql_query(conn, "START TRANSACTION") != 0) {
        result.error_type = OrderErrorType::SYSTEM_ERROR;
        result.error_message = "START TRANSACTION failed: " + std::string(mysql_error(conn));
        return result;
    }

    bool success = true;
    std::string error_msg;

    // 1. 扣减库存（使用 PreparedStatement）
    try {
        std::string sql = "UPDATE products SET stock = stock - ?, updated_at = ? WHERE id = ? AND stock >= ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, order.quantity);
        stmt.bindInt64(1, order.created_at);
        stmt.bindString(2, order.product_id);
        stmt.bindInt(3, order.quantity);

        if (!stmt.executeUpdate()) {
            error_msg = "UPDATE stock failed: " + std::string(mysql_error(conn));
            result.error_type = OrderErrorType::SYSTEM_ERROR;
            success = false;
        } else if (mysql_affected_rows(conn) == 0) {
            error_msg = "Stock insufficient or product not found: " + order.product_id;
            result.error_type = OrderErrorType::BUSINESS_ERROR;
            success = false;
        }
    } catch (const std::exception& e) {
        error_msg = "UPDATE stock exception: " + std::string(e.what());
        result.error_type = OrderErrorType::SYSTEM_ERROR;
        success = false;
    }

    // 2. 插入订单（使用 PreparedStatement）
    if (success) {
        try {
            std::string sql = "INSERT INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES (?, ?, ?, ?, ?, ?)";
            PreparedStatement stmt(conn, sql);
            stmt.bindUint64(0, order.order_id);
            stmt.bindString(1, order.user_id);
            stmt.bindString(2, order.product_id);
            stmt.bindInt(3, order.quantity);
            stmt.bindInt(4, order.status);
            stmt.bindInt64(5, order.created_at);

            if (!stmt.executeUpdate()) {
                std::string mysql_err = std::string(mysql_error(conn));
                if (mysql_err.find("Duplicate entry") != std::string::npos) {
                    error_msg = "Duplicate order: " + std::to_string(order.order_id);
                    result.error_type = OrderErrorType::DUPLICATE_ERROR;
                } else {
                    error_msg = "INSERT order failed: " + mysql_err;
                    result.error_type = OrderErrorType::SYSTEM_ERROR;
                }
                success = false;
            }
        } catch (const std::exception& e) {
            error_msg = "INSERT order exception: " + std::string(e.what());
            result.error_type = OrderErrorType::SYSTEM_ERROR;
            success = false;
        }
    }

    // 提交或回滚
    if (success) {
        if (mysql_query(conn, "COMMIT") != 0) {
            error_msg = "COMMIT failed: " + std::string(mysql_error(conn));
            result.error_type = OrderErrorType::SYSTEM_ERROR;
            success = false;
            mysql_query(conn, "ROLLBACK");
        }
    } else {
        mysql_query(conn, "ROLLBACK");
    }

    result.success = success;
    result.error_message = error_msg;

    if (!success && result.error_type == OrderErrorType::SYSTEM_ERROR) {
        Logger::instance().error("Order transaction system error: " + error_msg);
    } else if (!success) {
        Logger::instance().warn("Order transaction business error: " + error_msg);
    }

    return result;
}

// 批量插入订单 - 保留字符串拼接（数值字段无注入风险，字符串字段转义）
int MySQLService::batchCreateOrders(const std::vector<Order>& orders) {
    if (orders.empty()) return 0;

    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return -1;

        std::string sql = "INSERT IGNORE INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES ";
        
        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& o = orders[i];
            std::string escaped_user = escape_string(conn, o.user_id);
            std::string escaped_product = escape_string(conn, o.product_id);
            sql += "(" + std::to_string(o.order_id) + ", '" + escaped_user + "', '" + escaped_product + "', "
                   + std::to_string(o.quantity) + ", " + std::to_string(o.status) + ", "
                   + std::to_string(o.created_at) + ")";
            if (i != orders.size() - 1) {
                sql += ",";
            }
        }

        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("Batch insert failed: " + std::string(mysql_error(conn)));
            return -1;
        }

        int affected = mysql_affected_rows(conn);
        return affected;

    } catch (const std::exception& e) {
        Logger::instance().error("Batch insert exception: " + std::string(e.what()));
        return -1;
    }
}

// Order 读操作 - 使用 mysql_query + escape_string
std::vector<Order> MySQLService::get_orders_by_user(const std::string& user_id, int limit) {
    std::vector<Order> orders;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return orders;

    std::string escaped_id = escape_string(conn, user_id);
    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE user_id='" + escaped_id + "' LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return orders;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Order o;
        o.order_id   = row[0] ? std::stoull(row[0]) : 0ULL;
        o.user_id    = row[1] ? row[1] : "";
        o.product_id = row[2] ? row[2] : "";
        o.quantity   = row[3] ? std::stoi(row[3]) : 0;
        o.status     = row[4] ? std::stoi(row[4]) : 0;
        o.created_at = row[5] ? std::stoll(row[5]) : 0;
        o.updated_at = row[6] ? std::stoll(row[6]) : 0;
        orders.push_back(o);
    }

    mysql_free_result(res);
    return orders;
}

std::vector<Order> MySQLService::get_orders_by_product(const std::string& product_id, int limit) {
    std::vector<Order> orders;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return orders;

    std::string escaped_id = escape_string(conn, product_id);
    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE product_id='" + escaped_id + "' LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return orders;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Order o;
        o.order_id   = row[0] ? std::stoull(row[0]) : 0ULL;
        o.user_id    = row[1] ? row[1] : "";
        o.product_id = row[2] ? row[2] : "";
        o.quantity   = row[3] ? std::stoi(row[3]) : 0;
        o.status     = row[4] ? std::stoi(row[4]) : 0;
        o.created_at = row[5] ? std::stoll(row[5]) : 0;
        o.updated_at = row[6] ? std::stoll(row[6]) : 0;
        orders.push_back(o);
    }

    mysql_free_result(res);
    return orders;
}


int MySQLService::count_orders_by_product(const std::string& product_id) {
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return 0;

    std::string escaped = escape_string(conn, product_id);
    std::string sql = "SELECT COUNT(*) FROM orders WHERE product_id='" + escaped + "'";
    if (mysql_query(conn, sql.c_str())) return 0;

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return 0;

    MYSQL_ROW row = mysql_fetch_row(res);
    int count = (row && row[0]) ? std::atoi(row[0]) : 0;
    mysql_free_result(res);
    return count;
}

std::vector<Order> MySQLService::get_all_orders(int limit) {
    std::vector<Order> orders;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return orders;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders ORDER BY created_at DESC LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return orders;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Order o;
        o.order_id   = row[0] ? std::stoull(row[0]) : 0ULL;
        o.user_id    = row[1] ? row[1] : "";
        o.product_id = row[2] ? row[2] : "";
        o.quantity   = row[3] ? std::stoi(row[3]) : 0;
        o.status     = row[4] ? std::stoi(row[4]) : 0;
        o.created_at = row[5] ? std::stoll(row[5]) : 0;
        o.updated_at = row[6] ? std::stoll(row[6]) : 0;
        orders.push_back(o);
    }

    mysql_free_result(res);
    return orders;
}

std::optional<Order> MySQLService::get_order_by_id(uint64_t order_id) {
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE order_id=" + std::to_string(order_id);
    
    if (mysql_query(conn, sql.c_str())) {
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Order> result = std::nullopt;
    if (row) {
        Order order;
        order.order_id   = row[0] ? std::stoull(row[0]) : 0ULL;
        order.user_id    = row[1] ? row[1] : "";
        order.product_id = row[2] ? row[2] : "";
        order.quantity   = row[3] ? std::stoi(row[3]) : 0;
        order.status     = row[4] ? std::stoi(row[4]) : 0;
        order.created_at = row[5] ? std::stoll(row[5]) : 0;
        order.updated_at = row[6] ? std::stoll(row[6]) : 0;
        result = order;
    }

    mysql_free_result(res);
    return result;
}

bool MySQLService::update_order_status(uint64_t order_id, int status) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql = "UPDATE orders SET status = ?, updated_at = ? WHERE order_id = ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, status);
        stmt.bindInt64(1, time(nullptr) * 1000);
        stmt.bindUint64(2, order_id);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("update_order_status exception: " + std::string(e.what()));
        return false;
    }
}

// ========== 活动操作方法（seckill_activities 表）==========

bool MySQLService::create_activity(const Activity& act) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        // INSERT ... ON DUPLICATE KEY UPDATE（幂等）
        std::string sql =
            "INSERT INTO seckill_activities (id, product_id, seckill_price, stock, "
            "start_time, end_time, status, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "seckill_price = VALUES(seckill_price), "
            "stock = VALUES(stock), "
            "start_time = VALUES(start_time), "
            "end_time = VALUES(end_time), "
            "status = VALUES(status)";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, act.id);
        stmt.bindString(1, act.product_id);
        stmt.bindDouble(2, act.seckill_price);
        stmt.bindInt(3, act.stock);
        stmt.bindInt64(4, act.start_time);
        stmt.bindInt64(5, act.end_time);
        stmt.bindInt(6, static_cast<int>(act.status));
        stmt.bindInt64(7, std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("create_activity exception: " + std::string(e.what()));
        return false;
    }
}

std::optional<Activity> MySQLService::get_activity_by_product(const std::string& product_id) {
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return std::nullopt;

    std::string escaped = escape_string(conn, product_id);
    std::string sql =
        "SELECT id, product_id, seckill_price, stock, start_time, end_time, status "
        "FROM seckill_activities WHERE product_id = '" + escaped + "' LIMIT 1";

    if (mysql_query(conn, sql.c_str())) {
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Activity> result = std::nullopt;
    if (row) {
        Activity act;
        act.id            = row[0] ? row[0] : "";
        act.product_id    = row[1] ? row[1] : "";
        act.seckill_price = row[2] ? std::stod(row[2]) : 0.0;
        act.stock         = row[3] ? std::atoi(row[3]) : 0;
        act.start_time     = row[4] ? std::stoll(row[4]) : 0;
        act.end_time      = row[5] ? std::stoll(row[5]) : 0;
        act.status        = row[6] ? static_cast<int8_t>(std::atoi(row[6])) : 0;
        result = std::move(act);
    }
    mysql_free_result(res);
    return result;
}

bool MySQLService::update_activity_status(const std::string& id, int8_t status) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;

        std::string sql =
            "UPDATE seckill_activities SET status = ?, end_time = ? WHERE id = ?";
        PreparedStatement stmt(conn, sql);
        stmt.bindInt(0, static_cast<int>(status));
        stmt.bindInt64(1, std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
        stmt.bindString(2, id);

        int affected = stmt.executeUpdate(); Logger::instance().error("DEBUG: create_activity affected=" + std::to_string(affected) + ", mysql_err=" + std::string(mysql_error(conn))); return affected > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("update_activity_status exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<Activity> MySQLService::get_all_active_activities() {
    std::vector<Activity> result;
    ConnectionGuard guard(MySQLPool::getInstance());
    MYSQL* conn = guard.get();
    if (!conn) return result;

    std::string sql =
        "SELECT id, product_id, seckill_price, stock, start_time, end_time, status "
        "FROM seckill_activities WHERE status = 1";

    if (mysql_query(conn, sql.c_str())) {
        return result;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Activity act;
        act.id            = row[0] ? row[0] : "";
        act.product_id    = row[1] ? row[1] : "";
        act.seckill_price = row[2] ? std::stod(row[2]) : 0.0;
        act.stock         = row[3] ? std::atoi(row[3]) : 0;
        act.start_time     = row[4] ? std::stoll(row[4]) : 0;
        act.end_time      = row[5] ? std::stoll(row[5]) : 0;
        act.status        = row[6] ? static_cast<int8_t>(std::atoi(row[6])) : 0;
        result.push_back(std::move(act));
    }
    mysql_free_result(res);
    return result;
}

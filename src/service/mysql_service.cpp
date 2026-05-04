#include "service/mysql_service.h"
#include "service/mysql_pool.h"
#include "common/logger.h"
#include <iostream>
#include <sstream>
#include <optional>

bool MySQLService::init(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         const std::string& database, int pool_size) {
    host_ = host;
    port_ = port;
    user_ = user;
    password_ = password;
    database_ = database;

    // Initialize connection pool
    for (int i = 0; i < pool_size; i++) {
        MYSQL* mysql = mysql_init(nullptr);
        if (!mysql) {
            Logger::instance().error("Failed to init MySQL");
            return false;
        }

        // 设置连接超时（防止永久阻塞）
        int conn_timeout = 5;
        mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &conn_timeout);
        mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &conn_timeout);
        mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &conn_timeout);

        if (!mysql_real_connect(mysql, host.c_str(), user.c_str(), 
                                password.c_str(), database.c_str(),
                                port, nullptr, 0)) {
            std::cerr << "[MYSQL_ERROR] connect failed: " << mysql_error(mysql) << std::endl;
            std::cerr.flush();
            mysql_close(mysql);
            return false;
        }

        mysql_set_character_set(mysql, "utf8mb4");
        pool_.push_back({mysql, false});
    }

    Logger::instance().info("MySQL connected: " + host + ":" + std::to_string(port));
    return true;
}

void MySQLService::close() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& conn : pool_) {
        if (conn.mysql) {
            mysql_close(conn.mysql);
        }
    }
    pool_.clear();
}

MYSQL* MySQLService::get_connection() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    pool_cv_.wait(lock, [this] {
        for (auto& conn : pool_) {
            if (!conn.in_use) {
                return true;
            }
        }
        return false;
    });

    for (auto& conn : pool_) {
        if (!conn.in_use) {
            conn.in_use = true;
            return conn.mysql;
        }
    }
    return nullptr;
}

void MySQLService::release_connection(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& c : pool_) {
        if (c.mysql == conn) {
            c.in_use = false;
            pool_cv_.notify_one();
            return;
        }
    }
}

bool MySQLService::execute_sql(const std::string& sql) {
    MYSQL* conn = get_connection();
    if (!conn) return false;

    bool result = mysql_query(conn, sql.c_str()) == 0;
    if (!result) {
        Logger::instance().error("SQL error: " + std::string(mysql_error(conn)));
    }

    release_connection(conn);
    return result;
}

// Product struct 版本
bool MySQLService::add_product(const Product& product) {
    std::stringstream ss;
    ss << "INSERT INTO products (id, name, stock, price, created_at) VALUES ('"
       << product.id << "', '" << product.name << "', "
       << product.stock << ", " << product.price << ", "
       << product.created_at << ")";

    return execute_sql(ss.str());
}

bool MySQLService::update_product_stock(const std::string& product_id, int stock) {
    std::stringstream ss;
    ss << "UPDATE products SET stock = " << stock 
       << ", updated_at = " << time(nullptr) * 1000
       << " WHERE id = '" << product_id << "'";

    return execute_sql(ss.str());
}

std::vector<Product> MySQLService::get_all_products() {
    std::vector<Product> products;
    MYSQL* conn = get_connection();
    if (!conn) return products;

    if (mysql_query(conn, "SELECT id, name, stock, price, created_at, updated_at FROM products")) {
        release_connection(conn);
        return products;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return products;
    }

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
    release_connection(conn);
    return products;
}

std::optional<Product> MySQLService::get_product(const std::string& product_id) {
    MYSQL* conn = get_connection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT id, name, stock, price, created_at, updated_at FROM products WHERE id='" + product_id + "'";
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return std::nullopt;
    }

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
    release_connection(conn);
    return result;
}

bool MySQLService::delete_product(const std::string& product_id) {
    std::string sql = "DELETE FROM products WHERE id='" + product_id + "'";
    return execute_sql(sql);
}

// Product tuple 版本
bool MySQLService::create_product(const std::string& id, const std::string& name, int stock, double price) {
    std::stringstream ss;
    ss << "INSERT INTO products (id, name, stock, price, created_at) VALUES ('"
       << id << "', '" << name << "', " << stock << ", " << price << ", "
       << time(nullptr) * 1000 << ")";
    return execute_sql(ss.str());
}

std::vector<std::tuple<std::string, std::string, int, double>> MySQLService::get_all_products_tuple() {
    std::vector<std::tuple<std::string, std::string, int, double>> products;
    MYSQL* conn = get_connection();
    if (!conn) return products;

    if (mysql_query(conn, "SELECT id, name, stock, price FROM products")) {
        release_connection(conn);
        return products;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return products;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string id = row[0] ? row[0] : "";
        std::string name = row[1] ? row[1] : "";
        int stock = row[2] ? std::stoi(row[2]) : 0;
        double price = row[3] ? std::stod(row[3]) : 0.0;
        products.emplace_back(id, name, stock, price);
    }

    mysql_free_result(res);
    release_connection(conn);
    return products;
}

std::tuple<std::string, std::string, int, double> MySQLService::get_product_by_id(const std::string& id) {
    MYSQL* conn = get_connection();
    if (!conn) return {"", "", 0, 0.0};

    std::string sql = "SELECT id, name, stock, price FROM products WHERE id='" + id + "'";
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return {"", "", 0, 0.0};
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return {"", "", 0, 0.0};
    }

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
    release_connection(conn);
    return result;
}

// Order 操作
bool MySQLService::create_order(const Order& order) {
    auto result = create_order_with_error_type(order);
    return result.success;
}

// 区分错误类型的订单创建（用于死信队列处理）
CreateOrderResult MySQLService::create_order_with_error_type(const Order& order) {
    CreateOrderResult result;

    // 使用事务保证原子性：插入订单 + 扣减库存
    MYSQL* conn = get_connection();
    if (!conn) {
        result.error_type = OrderErrorType::SYSTEM_ERROR;
        result.error_message = "Failed to get MySQL connection";
        return result;
    }

    // 开始事务
    if (mysql_query(conn, "START TRANSACTION") != 0) {
        result.error_type = OrderErrorType::SYSTEM_ERROR;
        result.error_message = "START TRANSACTION failed: " + std::string(mysql_error(conn));
        release_connection(conn);
        return result;
    }

    bool success = true;
    std::string error_msg;

    // 1. 扣减库存（乐观锁：只有 stock > 0 时才扣减）
    {
        std::stringstream ss;
        ss << "UPDATE products SET stock = stock - " << order.quantity
           << ", updated_at = " << order.created_at
           << " WHERE id = '" << order.product_id << "' AND stock >= " << order.quantity;
        if (mysql_query(conn, ss.str().c_str()) != 0) {
            // SQL 执行失败 = 系统错误
            error_msg = "UPDATE stock failed: " + std::string(mysql_error(conn));
            result.error_type = OrderErrorType::SYSTEM_ERROR;
            success = false;
        } else if (mysql_affected_rows(conn) == 0) {
            // 影响行数为 0 = 商品不存在或库存不足 = 业务错误
            error_msg = "Stock insufficient or product not found: " + order.product_id;
            result.error_type = OrderErrorType::BUSINESS_ERROR;
            success = false;
        }
    }

    // 2. 插入订单
    if (success) {
        std::stringstream ss;
        ss << "INSERT INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES ("
           << order.order_id << ", '" << order.user_id << "', '" << order.product_id << "', "
           << order.quantity << ", " << order.status << ", " << order.created_at << ")";
        if (mysql_query(conn, ss.str().c_str()) != 0) {
            std::string mysql_err = std::string(mysql_error(conn));
            // 检查是否是重复订单（UNIQUE KEY 冲突）
            if (mysql_err.find("Duplicate entry") != std::string::npos) {
                error_msg = "Duplicate order: " + order.order_id;
                result.error_type = OrderErrorType::DUPLICATE_ERROR;
            } else {
                error_msg = "INSERT order failed: " + mysql_err;
                result.error_type = OrderErrorType::SYSTEM_ERROR;
            }
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

    release_connection(conn);

    result.success = success;
    result.error_message = error_msg;

    if (!success && result.error_type == OrderErrorType::SYSTEM_ERROR) {
        Logger::instance().error("Order transaction system error: " + error_msg);
    } else if (!success) {
        Logger::instance().warn("Order transaction business error: " + error_msg);
    }

    return result;
}

// 批量插入订单（使用 INSERT IGNORE，幂等性保护）
int MySQLService::batchCreateOrders(const std::vector<Order>& orders) {
    if (orders.empty()) return 0;

    try {
        // 1. 通过 RAII 守卫获取连接（函数结束自动归还）
        ConnectionGuard guard;
        MYSQL* conn = guard.get();

        // 2. 拼接批量 SQL
        // 使用 INSERT IGNORE：重复订单静默忽略，不影响其他订单
        std::string sql = "INSERT IGNORE INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES ";
        
        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& o = orders[i];
            sql += "(" + std::to_string(o.order_id) + ", '" + o.user_id + "', '" + o.product_id + "', "
                   + std::to_string(o.quantity) + ", " + std::to_string(o.status) + ", "
                   + std::to_string(o.created_at) + ")";
            if (i != orders.size() - 1) {
                sql += ",";
            }
        }

        // 3. 执行批量 SQL
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("Batch insert failed: " + std::string(mysql_error(conn)));
            return -1;  // 系统错误
        }

        // 4. 返回受影响的行数（成功插入的订单数）
        int affected = mysql_affected_rows(conn);
        return affected;

    } catch (const std::exception& e) {
        Logger::instance().error("Batch insert exception: " + std::string(e.what()));
        return -1;
    }
}

std::vector<Order> MySQLService::get_orders_by_user(const std::string& user_id, int limit) {
    std::vector<Order> orders;
    MYSQL* conn = get_connection();
    if (!conn) return orders;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE user_id='" + user_id + "' LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return orders;
    }

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
    release_connection(conn);
    return orders;
}

std::vector<Order> MySQLService::get_orders_by_product(const std::string& product_id, int limit) {
    std::vector<Order> orders;
    MYSQL* conn = get_connection();
    if (!conn) return orders;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE product_id='" + product_id + "' LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return orders;
    }

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
    release_connection(conn);
    return orders;
}

std::vector<Order> MySQLService::get_all_orders(int limit) {
    std::vector<Order> orders;
    MYSQL* conn = get_connection();
    if (!conn) return orders;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders ORDER BY created_at DESC LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return orders;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return orders;
    }

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
    release_connection(conn);
    return orders;
}

std::optional<Order> MySQLService::get_order_by_id(uint64_t order_id) {
    MYSQL* conn = get_connection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE order_id=" + std::to_string(order_id);
    
    if (mysql_query(conn, sql.c_str())) {
        release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        release_connection(conn);
        return std::nullopt;
    }

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
    release_connection(conn);
    return result;
}

bool MySQLService::update_order_status(uint64_t order_id, int status) {
    std::stringstream ss;
    ss << "UPDATE orders SET status = " << status 
       << ", updated_at = " << time(nullptr) * 1000
       << " WHERE order_id = " << order_id;

    return execute_sql(ss.str());
}

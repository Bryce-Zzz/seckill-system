#include "service/mysql_service.h"
#include "service/mysql_pool.h"
#include "service/prepared_statement.h"
#include "common/logger.h"
#include <iostream>
#include <sstream>
#include <optional>

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

void MySQLService::close() {
}

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
        return stmt.executeUpdate();
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
        return stmt.executeUpdate();
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
        return stmt.executeUpdate();
    } catch (const std::exception& e) {
        Logger::instance().error("delete_product exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<Product> MySQLService::get_all_products() {
    std::vector<Product> products;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return products;
        std::string sql = "SELECT id, name, stock, price, created_at, updated_at FROM products";
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_all_products failed: " + std::string(mysql_error(conn)));
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
    } catch (const std::exception& e) {
        Logger::instance().error("get_all_products exception: " + std::string(e.what()));
    }
    return products;
}

std::optional<Product> MySQLService::get_product(const std::string& product_id) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return std::nullopt;
        std::string escaped_id = escape_string(conn, product_id);
        std::string sql = "SELECT id, name, stock, price, created_at, updated_at FROM products WHERE id = '" + escaped_id + "'";
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_product failed: " + std::string(mysql_error(conn)));
            return std::nullopt;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return std::nullopt;
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return std::nullopt;
        }
        Product p;
        p.id = row[0] ? row[0] : "";
        p.name = row[1] ? row[1] : "";
        p.stock = row[2] ? std::stoi(row[2]) : 0;
        p.price = row[3] ? std::stod(row[3]) : 0.0;
        p.created_at = row[4] ? std::stoll(row[4]) : 0;
        p.updated_at = row[5] ? std::stoll(row[5]) : 0;
        mysql_free_result(res);
        return p;
    } catch (const std::exception& e) {
        Logger::instance().error("get_product exception: " + std::string(e.what()));
        return std::nullopt;
    }
}

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
        return stmt.executeUpdate();
    } catch (const std::exception& e) {
        Logger::instance().error("create_product exception: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::tuple<std::string, std::string, int, double>> MySQLService::get_all_products_tuple() {
    std::vector<std::tuple<std::string, std::string, int, double>> results;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return results;
        std::string sql = "SELECT id, name, stock, price FROM products";
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_all_products_tuple failed: " + std::string(mysql_error(conn)));
            return results;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return results;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            std::string id = row[0] ? row[0] : "";
            std::string name = row[1] ? row[1] : "";
            int stock = row[2] ? std::stoi(row[2]) : 0;
            double price = row[3] ? std::stod(row[3]) : 0.0;
            results.emplace_back(id, name, stock, price);
        }
        mysql_free_result(res);
    } catch (const std::exception& e) {
        Logger::instance().error("get_all_products_tuple exception: " + std::string(e.what()));
    }
    return results;
}

std::tuple<std::string, std::string, int, double> MySQLService::get_product_by_id(const std::string& id) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return std::make_tuple("", "", 0, 0.0);
        std::string escaped_id = escape_string(conn, id);
        std::string sql = "SELECT id, name, stock, price FROM products WHERE id = '" + escaped_id + "'";
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_product_by_id failed: " + std::string(mysql_error(conn)));
            return std::make_tuple("", "", 0, 0.0);
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return std::make_tuple("", "", 0, 0.0);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return std::make_tuple("", "", 0, 0.0);
        }
        std::string rid = row[0] ? row[0] : "";
        std::string name = row[1] ? row[1] : "";
        int stock = row[2] ? std::stoi(row[2]) : 0;
        double price = row[3] ? std::stod(row[3]) : 0.0;
        mysql_free_result(res);
        return std::make_tuple(rid, name, stock, price);
    } catch (const std::exception& e) {
        Logger::instance().error("get_product_by_id exception: " + std::string(e.what()));
        return std::make_tuple("", "", 0, 0.0);
    }
}

bool MySQLService::create_order(const Order& order) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return false;
        std::string sql = "INSERT INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES (?, ?, ?, ?, ?, ?)";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, std::to_string(order.order_id));
        stmt.bindString(1, order.user_id);
        stmt.bindString(2, order.product_id);
        stmt.bindInt(3, order.quantity);
        stmt.bindInt(4, order.status);
        stmt.bindInt64(5, order.created_at);
        return stmt.executeUpdate();
    } catch (const std::exception& e) {
        Logger::instance().error("create_order exception: " + std::string(e.what()));
        return false;
    }
}

CreateOrderResult MySQLService::create_order_with_error_type(const Order& order) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) {
            return CreateOrderResult(false, OrderErrorType::SYSTEM_ERROR, "Failed to get connection");
        }

        std::string escaped_pid = escape_string(conn, order.product_id);
        std::string check_sql = "SELECT stock FROM products WHERE id = '" + escaped_pid + "'";
        if (mysql_query(conn, check_sql.c_str()) != 0) {
            return CreateOrderResult(false, OrderErrorType::SYSTEM_ERROR, std::string(mysql_error(conn)));
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) {
            return CreateOrderResult(false, OrderErrorType::SYSTEM_ERROR, "Failed to store result");
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return CreateOrderResult(false, OrderErrorType::BUSINESS_ERROR, "Product not found");
        }
        int stock = row[0] ? std::stoi(row[0]) : 0;
        mysql_free_result(res);
        if (stock < order.quantity) {
            return CreateOrderResult(false, OrderErrorType::BUSINESS_ERROR, "Insufficient stock");
        }

        std::string sql = "INSERT INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES (?, ?, ?, ?, ?, ?)";
        PreparedStatement stmt(conn, sql);
        stmt.bindString(0, std::to_string(order.order_id));
        stmt.bindString(1, order.user_id);
        stmt.bindString(2, order.product_id);
        stmt.bindInt(3, order.quantity);
        stmt.bindInt(4, order.status);
        stmt.bindInt64(5, order.created_at);
        if (!stmt.executeUpdate()) {
            std::string err = mysql_error(conn);
            if (err.find("Duplicate") != std::string::npos || mysql_errno(conn) == 1062) {
                return CreateOrderResult(true, OrderErrorType::DUPLICATE_ERROR, "Duplicate order, treated as success");
            }
            return CreateOrderResult(false, OrderErrorType::SYSTEM_ERROR, err);
        }
        return CreateOrderResult(true, OrderErrorType::SUCCESS, "");
    } catch (const std::exception& e) {
        Logger::instance().error("create_order_with_error_type exception: " + std::string(e.what()));
        return CreateOrderResult(false, OrderErrorType::SYSTEM_ERROR, e.what());
    }
}

int MySQLService::batchCreateOrders(const std::vector<Order>& orders) {
    if (orders.empty()) return 0;
    int success_count = 0;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return 0;

        std::string sql = "INSERT INTO orders (order_id, user_id, product_id, quantity, status, created_at) VALUES ";
        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& order = orders[i];
            std::string escaped_uid = escape_string(conn, order.user_id);
            std::string escaped_pid = escape_string(conn, order.product_id);
            sql += "('" + std::to_string(order.order_id) + "', '" + escaped_uid + "', '" + escaped_pid + "', "
                + std::to_string(order.quantity) + ", " + std::to_string(order.status) + ", " + std::to_string(order.created_at) + ")";
            if (i + 1 < orders.size()) sql += ", ";
        }
        if (mysql_query(conn, sql.c_str()) == 0) {
            success_count = static_cast<int>(mysql_affected_rows(conn));
        } else {
            Logger::instance().error("batchCreateOrders failed: " + std::string(mysql_error(conn)));
        }
    } catch (const std::exception& e) {
        Logger::instance().error("batchCreateOrders exception: " + std::string(e.what()));
    }
    return success_count;
}

std::vector<Order> MySQLService::get_orders_by_user(const std::string& user_id, int limit) {
    std::vector<Order> orders;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return orders;
        std::string escaped_uid = escape_string(conn, user_id);
        std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE user_id = '"
            + escaped_uid + "' LIMIT " + std::to_string(limit);
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_orders_by_user failed: " + std::string(mysql_error(conn)));
            return orders;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return orders;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            Order o;
            o.order_id = row[0] ? std::stoull(row[0]) : 0;
            o.user_id = row[1] ? row[1] : "";
            o.product_id = row[2] ? row[2] : "";
            o.quantity = row[3] ? std::stoi(row[3]) : 0;
            o.status = row[4] ? std::stoi(row[4]) : 0;
            o.created_at = row[5] ? std::stoll(row[5]) : 0;
            o.updated_at = row[6] ? std::stoll(row[6]) : 0;
            orders.push_back(o);
        }
        mysql_free_result(res);
    } catch (const std::exception& e) {
        Logger::instance().error("get_orders_by_user exception: " + std::string(e.what()));
    }
    return orders;
}

std::vector<Order> MySQLService::get_orders_by_product(const std::string& product_id, int limit) {
    std::vector<Order> orders;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return orders;
        std::string escaped_pid = escape_string(conn, product_id);
        std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE product_id = '"
            + escaped_pid + "' LIMIT " + std::to_string(limit);
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_orders_by_product failed: " + std::string(mysql_error(conn)));
            return orders;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return orders;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            Order o;
            o.order_id = row[0] ? std::stoull(row[0]) : 0;
            o.user_id = row[1] ? row[1] : "";
            o.product_id = row[2] ? row[2] : "";
            o.quantity = row[3] ? std::stoi(row[3]) : 0;
            o.status = row[4] ? std::stoi(row[4]) : 0;
            o.created_at = row[5] ? std::stoll(row[5]) : 0;
            o.updated_at = row[6] ? std::stoll(row[6]) : 0;
            orders.push_back(o);
        }
        mysql_free_result(res);
    } catch (const std::exception& e) {
        Logger::instance().error("get_orders_by_product exception: " + std::string(e.what()));
    }
    return orders;
}

std::vector<Order> MySQLService::get_all_orders(int limit) {
    std::vector<Order> orders;
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return orders;
        std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders LIMIT "
            + std::to_string(limit);
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_all_orders failed: " + std::string(mysql_error(conn)));
            return orders;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return orders;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            Order o;
            o.order_id = row[0] ? std::stoull(row[0]) : 0;
            o.user_id = row[1] ? row[1] : "";
            o.product_id = row[2] ? row[2] : "";
            o.quantity = row[3] ? std::stoi(row[3]) : 0;
            o.status = row[4] ? std::stoi(row[4]) : 0;
            o.created_at = row[5] ? std::stoll(row[5]) : 0;
            o.updated_at = row[6] ? std::stoll(row[6]) : 0;
            orders.push_back(o);
        }
        mysql_free_result(res);
    } catch (const std::exception& e) {
        Logger::instance().error("get_all_orders exception: " + std::string(e.what()));
    }
    return orders;
}

std::optional<Order> MySQLService::get_order_by_id(uint64_t order_id) {
    try {
        ConnectionGuard guard(MySQLPool::getInstance());
        MYSQL* conn = guard.get();
        if (!conn) return std::nullopt;
        std::string sql = "SELECT order_id, user_id, product_id, quantity, status, created_at, updated_at FROM orders WHERE order_id = '"
            + std::to_string(order_id) + "'";
        if (mysql_query(conn, sql.c_str()) != 0) {
            Logger::instance().error("get_order_by_id failed: " + std::string(mysql_error(conn)));
            return std::nullopt;
        }
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return std::nullopt;
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) {
            mysql_free_result(res);
            return std::nullopt;
        }
        Order o;
        o.order_id = row[0] ? std::stoull(row[0]) : 0;
        o.user_id = row[1] ? row[1] : "";
        o.product_id = row[2] ? row[2] : "";
        o.quantity = row[3] ? std::stoi(row[3]) : 0;
        o.status = row[4] ? std::stoi(row[4]) : 0;
        o.created_at = row[5] ? std::stoll(row[5]) : 0;
        o.updated_at = row[6] ? std::stoll(row[6]) : 0;
        mysql_free_result(res);
        return o;
    } catch (const std::exception& e) {
        Logger::instance().error("get_order_by_id exception: " + std::string(e.what()));
        return std::nullopt;
    }
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
        stmt.bindString(2, std::to_string(order_id));
        return stmt.executeUpdate();
    } catch (const std::exception& e) {
        Logger::instance().error("update_order_status exception: " + std::string(e.what()));
        return false;
    }
}

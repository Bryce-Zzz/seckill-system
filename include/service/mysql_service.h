#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <optional>
#include <mysql/mysql.h>
#include <mutex>
#include <condition_variable>

struct Product {
    std::string id;
    std::string name;
    int stock;
    double price;
    int64_t created_at;
    int64_t updated_at;
};

struct Order {
    uint64_t    order_id;      // 雪花算法生成的 64-bit 唯一 ID（对应 MySQL BIGINT UNSIGNED）
    std::string user_id;
    std::string product_id;
    int quantity;
    int status;  // 0=待处理, 1=已支付, 2=已取消
    int64_t created_at;
    int64_t updated_at;
};

// ==================== 订单创建结果（区分错误类型）====================
enum class OrderErrorType {
    SUCCESS,            // 成功
    SYSTEM_ERROR,        // 系统错误（连接失败、SQL错误等）- 需要重试
    BUSINESS_ERROR,      // 业务错误（商品不存在、库存不足）- 不重试
    DUPLICATE_ERROR,    // 重复订单（UNIQUE KEY 冲突）- 视为成功
};

struct CreateOrderResult {
    bool success;
    OrderErrorType error_type;
    std::string error_message;  // 原始错误信息，用于日志

    CreateOrderResult() : success(false), error_type(OrderErrorType::SYSTEM_ERROR), error_message("") {}
    CreateOrderResult(bool s, OrderErrorType t, const std::string& msg = "")
        : success(s), error_type(t), error_message(msg) {}
};

struct MySQLConnection {
    MYSQL* mysql;
    bool in_use;
};

class MySQLService {
public:
    static MySQLService& instance() {
        static MySQLService inst;
        return inst;
    }

    bool init(const std::string& host, int port,
              const std::string& user, const std::string& password,
              const std::string& database, int pool_size);

    // 产品操作 (返回 struct 版本)
    bool add_product(const Product& product);
    bool update_product_stock(const std::string& product_id, int stock);
    std::vector<Product> get_all_products();
    std::optional<Product> get_product(const std::string& product_id);
    bool delete_product(const std::string& product_id);

    // 产品操作 (返回 tuple 版本，用于兼容)
    bool create_product(const std::string& id, const std::string& name, int stock, double price);
    std::vector<std::tuple<std::string, std::string, int, double>> get_all_products_tuple();
    std::tuple<std::string, std::string, int, double> get_product_by_id(const std::string& id);

    // 订单操作
    bool create_order(const Order& order);
    CreateOrderResult create_order_with_error_type(const Order& order);  // 区分错误类型的版本
    int batchCreateOrders(const std::vector<Order>& orders);  // 批量插入订单
    std::vector<Order> get_orders_by_user(const std::string& user_id, int limit = 100);
    std::vector<Order> get_orders_by_product(const std::string& product_id, int limit = 100);
    std::vector<Order> get_all_orders(int limit = 100);
    std::optional<Order> get_order_by_id(uint64_t order_id);
    bool update_order_status(uint64_t order_id, int status);

    void close();

private:
    MYSQL* get_connection();
    void release_connection(MYSQL* conn);
    bool execute_sql(const std::string& sql);

    std::string host_;
    int port_ = 0;
    std::string user_;
    std::string password_;
    std::string database_;
    std::vector<MySQLConnection> pool_;
    std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
};

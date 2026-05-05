#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <optional>
#include <mysql/mysql.h>

struct Product {
    std::string id;
    std::string name;
    int stock;
    double price;
    int64_t created_at;
    int64_t updated_at;
};

struct Order {
    uint64_t    order_id;
    std::string user_id;
    std::string product_id;
    int quantity;
    int status;
    int64_t created_at;
    int64_t updated_at;
};

enum class OrderErrorType {
    SUCCESS,
    SYSTEM_ERROR,
    BUSINESS_ERROR,
    DUPLICATE_ERROR,
};

struct CreateOrderResult {
    bool success;
    OrderErrorType error_type;
    std::string error_message;

    CreateOrderResult() : success(false), error_type(OrderErrorType::SYSTEM_ERROR), error_message("") {}
    CreateOrderResult(bool s, OrderErrorType t, const std::string& msg = "")
        : success(s), error_type(t), error_message(msg) {}
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

    bool add_product(const Product& product);
    bool update_product_stock(const std::string& product_id, int stock);
    std::vector<Product> get_all_products();
    std::optional<Product> get_product(const std::string& product_id);
    bool delete_product(const std::string& product_id);

    bool create_product(const std::string& id, const std::string& name, int stock, double price);
    std::vector<std::tuple<std::string, std::string, int, double>> get_all_products_tuple();
    std::tuple<std::string, std::string, int, double> get_product_by_id(const std::string& id);

    bool create_order(const Order& order);
    CreateOrderResult create_order_with_error_type(const Order& order);
    int batchCreateOrders(const std::vector<Order>& orders);
    std::vector<Order> get_orders_by_user(const std::string& user_id, int limit = 100);
    std::vector<Order> get_orders_by_product(const std::string& product_id, int limit = 100);
    std::vector<Order> get_all_orders(int limit = 100);
    std::optional<Order> get_order_by_id(uint64_t order_id);
    bool update_order_status(uint64_t order_id, int status);

    void close();
};

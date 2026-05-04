#include "handler/admin_handler.h"
#include "handler/sse_handler.h"
#include "service/mysql_service.h"
#include "service/redis_service.h"
#include "common/logger.h"
#include "common/config.h"
#include <hv/json.hpp>
#include <sstream>

using json = nlohmann::json;

// 辅助函数：从 query_params 获取字符串参数
static inline std::string get_query_string(const hv::QueryParams& params, const char* key, const char* def = "") {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : def;
}

// 辅助函数：优先从JSON body读，再从query_params读
static inline std::string get_param(HttpRequest* req, const char* key, const char* def = "") {
    // 先尝试从 JSON body 读
    if (!req->body.empty()) {
        try {
            auto j = json::parse(req->body);
            if (j.contains(key) && !j[key].is_null()) {
                if (j[key].is_string()) return j[key].get<std::string>();
                return j[key].dump(); // 数字类型转字符串
            }
        } catch (...) {}
    }
    // 再从 query_params 读
    return get_query_string(req->query_params, key, def);
}

// 添加商品
static int handle_add_product(HttpRequest* req, HttpResponse* resp) {
    std::string id    = get_param(req, "id");
    std::string name  = get_param(req, "name");
    std::string stock_str = get_param(req, "stock", "0");
    std::string price_str = get_param(req, "price", "0");

    int stock = std::atoi(stock_str.c_str());
    double price = std::atof(price_str.c_str());

    json json_resp;
    if (id.empty() || name.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing required parameters";
    } else if (MySQLService::instance().create_product(id, name, stock, price)) {
        // 初始化 Redis 库存
        RedisService::instance().set(
            Config::instance().seckill.stock_prefix + id,
            std::to_string(stock));
        json_resp["code"] = 200;
        json_resp["msg"] = "Product added successfully";
        Logger::instance().info("Product added: " + id);
        
        // 广播商品添加事件
        json event_data = {
            {"product_id", id},
            {"product_name", name},
            {"stock", stock},
            {"price", price}
        };
        SSEHandler::instance().broadcast("product_added", event_data.dump());
    } else {
        json_resp["code"] = 500;
        json_resp["msg"] = "Failed to add product";
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 更新商品库存
static int handle_update_product(HttpRequest* req, HttpResponse* resp) {
    std::string id = get_param(req, "id");
    std::string stock_str = get_param(req, "stock", "0");
    int stock = std::atoi(stock_str.c_str());

    json json_resp;
    if (id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing product ID";
    } else if (MySQLService::instance().update_product_stock(id, stock)) {
        RedisService::instance().set(
            Config::instance().seckill.stock_prefix + id,
            std::to_string(stock));
        json_resp["code"] = 200;
        json_resp["msg"] = "Product updated successfully";
    } else {
        json_resp["code"] = 500;
        json_resp["msg"] = "Failed to update product";
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 删除商品
static int handle_delete_product(HttpRequest* req, HttpResponse* resp) {
    std::string id = get_param(req, "id");

    json json_resp;
    if (id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing product ID";
    } else if (MySQLService::instance().delete_product(id)) {
        RedisService::instance().del(Config::instance().seckill.stock_prefix + id);
        json_resp["code"] = 200;
        json_resp["msg"] = "Product deleted successfully";
        
        // 广播商品删除事件
        json event_data = {{"product_id", id}};
        SSEHandler::instance().broadcast("product_deleted", event_data.dump());
    } else {
        json_resp["code"] = 500;
        json_resp["msg"] = "Failed to delete product";
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 管理后台-商品列表
static int handle_admin_list_products(HttpRequest* req, HttpResponse* resp) {
    (void)req;

    auto products = MySQLService::instance().get_all_products_tuple();
    auto& cfg = Config::instance();

    json json_resp;
    json_resp["code"] = 200;
    json_resp["products"] = json::array();

    for (const auto& p : products) {
        std::string product_id = std::get<0>(p);
        std::string redis_stock = RedisService::instance().get(
            cfg.seckill.stock_prefix + product_id);
        int real_stock = redis_stock.empty() ? std::get<2>(p) : std::stoi(redis_stock);
        
        // 检查是否正在秒杀（Redis中有库存key说明秒杀已启动）
        bool seckill_active = !redis_stock.empty();
        int seckill_stock = seckill_active ? real_stock : 0;

        json obj = {
            {"id",            product_id},
            {"name",          std::get<1>(p)},
            {"stock",         std::get<2>(p)},  // MySQL原始库存
            {"seckill_stock", seckill_stock}, // 秒杀库存（0表示未启动）
            {"seckill_active", seckill_active},
            {"price",         std::get<3>(p)}
        };
        json_resp["products"].push_back(obj);
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}



// 管理后台-订单列表
static int handle_admin_list_orders(HttpRequest* req, HttpResponse* resp) {
    (void)req;

    // 获取订单列表
    auto orders = MySQLService::instance().get_all_orders(100);

    // 获取商品价格映射
    std::unordered_map<std::string, double> price_map;
    auto products = MySQLService::instance().get_all_products();
    for (const auto& p : products) {
        price_map[p.id] = p.price;
    }

    json json_resp;
    json_resp["code"] = 200;
    json_resp["orders"] = json::array();

    for (const auto& order : orders) {
        double price = 0.0;
        auto it = price_map.find(order.product_id);
        if (it != price_map.end()) {
            price = it->second;
        }
        json obj = {
            {"order_id",   std::to_string(order.order_id)},
            {"user_id",    order.user_id},
            {"product_id", order.product_id},
            {"quantity",   order.quantity},
            {"status",     order.status},
            {"created_at", order.created_at},
            {"price",      price}
        };
        json_resp["orders"].push_back(obj);
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 启动秒杀 —— stock 默认用商品现有库存
static int handle_start_seckill(HttpRequest* req, HttpResponse* resp) {
    std::string product_id = get_param(req, "product_id");
    std::string stock_str  = get_param(req, "stock", "0");
    int stock = std::atoi(stock_str.c_str());

    json json_resp;
    if (product_id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing parameters";
    } else {
        // 如果没传 stock，从 MySQL 读商品库存
        if (stock <= 0) {
            auto products = MySQLService::instance().get_all_products_tuple();
            for (const auto& p : products) {
                if (std::get<0>(p) == product_id) {
                    stock = std::get<2>(p);
                    break;
                }
            }
        }
        if (stock <= 0) stock = 100; // 最终兜底

        RedisService::instance().set(
            Config::instance().seckill.stock_prefix + product_id,
            std::to_string(stock));

        json_resp["code"] = 200;
        json_resp["msg"] = "Seckill started for product: " + product_id;
        json_resp["stock"] = stock;
        Logger::instance().info("Seckill started: product_id=" + product_id + ", stock=" + std::to_string(stock));
        
        // 广播秒杀开始事件
        json event_data = {
            {"product_id", product_id},
            {"stock", stock}
        };
        SSEHandler::instance().broadcast("seckill_started", event_data.dump());
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 停止秒杀
static int handle_stop_seckill(HttpRequest* req, HttpResponse* resp) {
    std::string product_id = get_param(req, "product_id");

    json json_resp;
    if (product_id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing product ID";
    } else {
        RedisService::instance().del(Config::instance().seckill.stock_prefix + product_id);
        json_resp["code"] = 200;
        json_resp["msg"] = "Seckill stopped for product: " + product_id;
        Logger::instance().info("Seckill stopped: product_id=" + product_id);
        
        // 广播秒杀停止事件
        json event_data = {{"product_id", product_id}};
        SSEHandler::instance().broadcast("seckill_stopped", event_data.dump());
    }

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 注册路由
void register_admin_routes(hv::HttpService* service) {
    service->POST("/api/admin/product/add",    handle_add_product);
    service->POST("/api/admin/product/update", handle_update_product);
    service->POST("/api/admin/product/delete", handle_delete_product);
    service->GET("/api/admin/product/list",    handle_admin_list_products);
    service->GET("/api/admin/products",        handle_admin_list_products);
    service->GET("/api/admin/orders",          handle_admin_list_orders);
    service->POST("/api/admin/seckill/start",  handle_start_seckill);
    service->POST("/api/admin/seckill/stop",   handle_stop_seckill);
}

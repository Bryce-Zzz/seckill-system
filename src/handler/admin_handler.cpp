#include "handler/admin_handler.h"
#include "handler/sse_handler.h"
#include "service/mysql_service.h"
#include "service/redis_service.h"
#include "service/activity_manager.h"
#include "common/logger.h"
#include "common/config.h"
#include <hv/json.hpp>
#include <sstream>
#include <chrono>

using json = nlohmann::json;

// 辅助函数：从 query_params 获取字符串参数
static inline std::string get_query_string(const hv::QueryParams& params, const char* key, const char* def = "") {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : def;
}

// 辅助函数：优先从JSON body读，再从query_params读
static inline std::string get_param(HttpRequest* req, const char* key, const char* def = "") {
    if (!req->body.empty()) {
        try {
            auto j = json::parse(req->body);
            if (j.contains(key) && !j[key].is_null()) {
                if (j[key].is_string()) return j[key].get<std::string>();
                return j[key].dump();
            }
        } catch (...) {}
    }
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
        RedisService::instance().set(
            Config::instance().seckill.stock_prefix + id,
            std::to_string(stock));
        json_resp["code"] = 200;
        json_resp["msg"] = "Product added successfully";
        Logger::instance().info("Product added: " + id);
        
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
        
        bool seckill_active = !redis_stock.empty();
        int seckill_stock = seckill_active ? real_stock : 0;
        
        json obj = {
            {"id",            product_id},
            {"name",          std::get<1>(p)},
            {"stock",         std::get<2>(p)},
            {"seckill_stock", seckill_stock},
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
    
    auto orders = MySQLService::instance().get_all_orders(100);
    
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

// 启动秒杀 - 活动状态机（写入 MySQL + 同步内存 + 设置 Redis）
static int handle_start_seckill(HttpRequest* req, HttpResponse* resp) {
    std::string product_id    = get_param(req, "product_id");
    std::string stock_str     = get_param(req, "stock", "0");
    std::string price_str     = get_param(req, "seckill_price", "0");
    std::string start_time_str = get_param(req, "start_time", "0");
    std::string end_time_str   = get_param(req, "end_time", "0");
    
    json json_resp;
    if (product_id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing product_id";
        resp->body = json_resp.dump();
    std::cerr << "[DEBUG] handle_start_seckill called, product_id=" << product_id << std::endl;
        resp->SetContentType("application/json");
        return 200;
    }
    
    // 读取库存
    int stock = std::atoi(stock_str.c_str());
    if (stock <= 0) {
        auto products = MySQLService::instance().get_all_products_tuple();
        for (const auto& p : products) {
            if (std::get<0>(p) == product_id) {
                stock = std::get<2>(p);
                break;
            }
        }
    }
    if (stock <= 0) {
        json_resp["code"] = 400;
        json_resp["msg"]  = "Product stock is 0, cannot start seckill";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    if (stock <= 0) {
        json_resp["code"] = 400;
        json_resp["msg"]  = "Product stock is 0, cannot start seckill";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    
    // 读取秒杀价
    double seckill_price = std::atof(price_str.c_str());
    if (seckill_price <= 0.0) {
        auto products = MySQLService::instance().get_all_products_tuple();
        for (const auto& p : products) {
            if (std::get<0>(p) == product_id) {
                seckill_price = std::get<3>(p);
                break;
            }
        }
    }
    
    // 读取开始/结束时间（毫秒时间戳）
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    int64_t start_time = std::atoll(start_time_str.c_str());
    if (start_time <= 0) start_time = now_ms;
    
    int64_t end_time = std::atoll(end_time_str.c_str());
    if (end_time <= 0) end_time = now_ms + 3600 * 1000;
    
    // 1. 写入 MySQL seckill_activities 表
    Activity act = {};
    act.id            = "act_" + product_id;
    act.product_id    = product_id;
    act.seckill_price = seckill_price;
    act.stock         = stock;
    act.start_time    = start_time;
    act.end_time      = end_time;
    act.status        = 1;  // 1=进行中
    
    Logger::instance().error("DEBUG: Calling create_activity for " + product_id);
    // Force-clear act.id to prevent garbage values
    act.id.clear();
    act.id = "act_" + product_id;
    if (!MySQLService::instance().create_activity(act)) {
        Logger::instance().error("start_seckill: failed to create activity for " + product_id);
        json_resp["code"] = 500;
        json_resp["msg"]  = "Failed to create activity record";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    std::cerr << "[DEBUG] act.id=" << act.id << " (hex:"; for (char c : act.id) std::cerr << std::hex << (static_cast<int>(c) & 0xFF) << " "; std::cerr << std::dec << ")" << std::endl;
    Logger::instance().error("DEBUG: create_activity SUCCESS for " + product_id);
    
    // 2. 同步到 ActivityManager 内存
    ActivityManager::instance().sync_once();
    
    // 2.5 先从商品表扣除库存
    if (!MySQLService::instance().decrease_product_stock(product_id, stock)) {
        Logger::instance().error("start_seckill: failed to decrease product stock for " + product_id);
        json_resp["code"] = 500;
        json_resp["msg"]  = "Failed to allocate stock from product";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    Logger::instance().info("Allocated " + std::to_string(stock) + " stock from product: " + product_id);
    
    // 3. 设置 Redis 库存
    RedisService::instance().set(
        Config::instance().seckill.stock_prefix + product_id, "0");
        Config::instance().seckill.stock_prefix + product_id,
        std::to_string(stock));
        std::to_string(stock));
    
    json_resp["code"] = 200;
    json_resp["msg"]  = "Seckill activity started";
    json_resp["activity_id"]   = act.id;
    json_resp["product_id"]    = product_id;
    json_resp["stock"]         = stock;
    json_resp["seckill_price"] = seckill_price;
    json_resp["start_time"]    = start_time;
    json_resp["end_time"]      = end_time;
    
    Logger::instance().info("Seckill activity started: " + act.id + ", product_id=" + product_id);
    
    // 4. 广播活动开始事件
    json event_data = {
        {"activity_id",   act.id},
        {"product_id",    product_id},
        {"stock",         stock},
        {"seckill_price", seckill_price},
        {"start_time",    start_time},
        {"end_time",      end_time}
    };
    SSEHandler::instance().broadcast("seckill_started", event_data.dump());
    
    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 停止秒杀 - 活动状态机（对账 + 库存返还 + 清理 Redis + 同步内存）
static int handle_stop_seckill(HttpRequest* req, HttpResponse* resp) {
    std::string product_id = get_param(req, "product_id");
    
    json json_resp;
    if (product_id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing product ID";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    // 1. 查询活动 ID
    auto activity_opt = MySQLService::instance().get_activity_by_product(product_id);
    if (!activity_opt) {
        json_resp["code"] = 404;
        json_resp["msg"] = "Activity not found for product: " + product_id;
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    std::string activity_id = activity_opt->id;
    
    // 2. 对账：先暂停秒杀（原子清空 Redis 库存），等待 Stream 消费完毕再对账

    // 2.1 原子性清空 Redis 库存（阻止新的成功下单）
    RedisService::instance().set(
        Config::instance().seckill.stock_prefix + product_id, "0");

    // 2.2 等待 Redis Stream 消费完毕（固定等待 3 秒，让 OrderProcessor 处理完 pending 消息）
    Logger::instance().info("Stopping seckill for " + product_id + ", waiting 3s for stream to drain...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 2.3 读取 Redis 剩余库存（此时应已被设为 0）
    std::string redis_stock_str = RedisService::instance().get(
        Config::instance().seckill.stock_prefix + product_id);
    int redis_remaining = redis_stock_str.empty() ? 0 : std::stoi(redis_stock_str);

    // 2.4 读取活动快照库存
    int initial_stock = activity_opt->stock;

    // 2.5 用 COUNT(*) 查询 MySQL 真实订单数（无上限）
    int mysql_order_count = MySQLService::instance().count_orders_by_product(product_id);

    // 2.6 对账校验：Redis 视角销量 vs MySQL 视角销量
    int redis_sold = initial_stock - redis_remaining;
    int mysql_sold = mysql_order_count;
    if (redis_sold != mysql_sold) {
        Logger::instance().error(
            "RECONCILIATION MISMATCH! product_id=" + product_id +
            ", redis_sold=" + std::to_string(redis_sold) +
            ", mysql_sold=" + std::to_string(mysql_sold) +
            ". Using mysql_sold as truth.");
    }

    // 2.7 计算未售出库存（应该返还的）
    int unsold_stock = initial_stock - mysql_order_count;
    if (unsold_stock < 0) {
        Logger::instance().error(
            "UNSOLD STOCK NEGATIVE! product_id=" + product_id +
            ", initial_stock=" + std::to_string(initial_stock) +
            ", mysql_order_count=" + std::to_string(mysql_order_count) +
            ". Setting unsold_stock=0.");
        unsold_stock = 0;
    }
    
    // 3. 更新 MySQL 活动状态为停止
    if (!MySQLService::instance().update_activity_status(activity_id, 0)) {
        Logger::instance().error("Failed to update activity status: " + activity_id);
    }
    
    // 4. 将未售出库存返还给商品表
    if (unsold_stock > 0) {
        if (MySQLService::instance().add_product_stock(product_id, unsold_stock)) {
            Logger::instance().info("Returned " + std::to_string(unsold_stock) + 
                                   " unsold stock to product: " + product_id);
        } else {
            Logger::instance().error("Failed to return stock to product: " + product_id);
        }
    }
    
    // 5. 同步 ActivityManager 内存（清理该活动）
    ActivityManager::instance().sync_once();
    
    // 6. 删除 Redis 库存 key
    RedisService::instance().del(Config::instance().seckill.stock_prefix + product_id);
    
    // 7. 构造响应
    json_resp["code"] = 200;
    json_resp["msg"]            = "Seckill stopped for product: " + product_id;
    json_resp["activity_id"]    = activity_id;
    json_resp["initial_stock"]   = initial_stock;
    json_resp["mysql_order_count"] = mysql_order_count;
    json_resp["unsold_returned"]  = unsold_stock;
    json_resp["redis_remaining"]  = redis_remaining;
    json_resp["reconciliation_ok"] = (initial_stock - redis_remaining == mysql_order_count);
    
    Logger::instance().info("Seckill stopped: " + activity_id + ", product_id=" + 
                           product_id + ", sold=" + std::to_string(mysql_order_count) + 
                           ", returned=" + std::to_string(unsold_stock));
    
    // 8. 广播活动停止事件
    json event_data = {
        {"activity_id",    activity_id},
        {"product_id",    product_id},
        {"sold_count",     mysql_order_count},
        {"unsold_returned", unsold_stock}
    };
    SSEHandler::instance().broadcast("seckill_stopped", event_data.dump());
    
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

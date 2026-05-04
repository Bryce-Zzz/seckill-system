#include "handler/order_handler.h"
#include "handler/sse_handler.h"
#include "service/mysql_service.h"
#include "service/redis_service.h"
#include "common/logger.h"
#include "common/config.h"
#include "common/metrics.h"
#include <hv/json.hpp>

using json = nlohmann::json;

// 辅助函数：从 query_params 获取字符串参数
static inline std::string get_query_string(const hv::QueryParams& params, const char* key, const char* def = "") {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : def;
}

// ============================================================
//  🔍 查询秒杀结果（优先 Redis 缓存，缓存未命中查 MySQL）
//
//  当 SSE 推送断连时，用户刷新页面可以直接调用此接口。
//  缓存 Key：seckill:result:{user_id}:{product_id}
//  缓存 Value：order_id
//  缓存 TTL：1小时
// ============================================================
static int handle_seckill_result(HttpRequest* req, HttpResponse* resp) {
    std::string user_id    = get_query_string(req->query_params, "user_id");
    std::string product_id = get_query_string(req->query_params, "product_id");

    json json_resp;

    if (user_id.empty() || product_id.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing user_id or product_id";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }

    // Step 1：先查 Redis 缓存
    std::string result_key = "seckill:result:" + user_id + ":" + product_id;
    std::string cached_order_id = RedisService::instance().get(result_key);

    if (!cached_order_id.empty()) {
        // 缓存命中：直接返回订单号，告知前端秒杀成功
        json_resp["code"] = 200;
        json_resp["status"] = "success";
        json_resp["order_id"] = cached_order_id;
        json_resp["source"] = "cache";

        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }

    // Step 2：缓存未命中，查 MySQL（历史订单）
    auto orders = MySQLService::instance().get_orders_by_user(user_id, 100);

    for (const auto& order : orders) {
        if (order.product_id == product_id) {
            // MySQL 中找到订单
            json_resp["code"] = 200;
            json_resp["status"] = "success";
            json_resp["order_id"] = std::to_string(order.order_id);
            json_resp["source"] = "database";
            json_resp["order"] = {
                {"order_id", std::to_string(order.order_id)},
                {"product_id", order.product_id},
                {"quantity", order.quantity},
                {"status", order.status},
                {"created_at", order.created_at}
            };

            resp->body = json_resp.dump();
            resp->SetContentType("application/json");
            return 200;
        }
    }

    // Step 3：MySQL 中也没有 → 用户尚未秒杀或秒杀失败
    json_resp["code"] = 200;
    json_resp["status"] = "pending";
    json_resp["msg"] = "No seckill record found";

    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 订单列表
static int handle_order_list(HttpRequest* req, HttpResponse* resp) {
    std::string user_id = get_query_string(req->query_params, "user_id");
    std::string limit_str = get_query_string(req->query_params, "limit", "100");
    int limit = std::atoi(limit_str.c_str());
    
    auto orders = MySQLService::instance().get_orders_by_user(user_id, limit);
    
    json json_resp;
    json_resp["code"] = 200;
    json_resp["orders"] = json::array();
    
    for (const auto& order : orders) {
        json obj = {
            {"order_id", std::to_string(order.order_id)},  // 字符串，防 JS 精度丢失
            {"user_id", order.user_id},
            {"product_id", order.product_id},
            {"quantity", order.quantity},
            {"status", order.status},
            {"created_at", order.created_at}
        };
        json_resp["orders"].push_back(obj);
    }
    
    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 订单详情
static int handle_order_detail(HttpRequest* req, HttpResponse* resp) {
    std::string order_id_str = get_query_string(req->query_params, "order_id");
    
    json json_resp;
    if (order_id_str.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing order_id";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }

    uint64_t order_id = std::stoull(order_id_str);
    auto order_opt = MySQLService::instance().get_order_by_id(order_id);
    
    if (order_opt.has_value()) {
        const auto& order = order_opt.value();
        json_resp["code"] = 200;
        json_resp["order"] = {
            {"order_id", std::to_string(order.order_id)},  // 字符串，防 JS 精度丢失
            {"user_id", order.user_id},
            {"product_id", order.product_id},
            {"quantity", order.quantity},
            {"status", order.status},
            {"created_at", order.created_at}
        };
    } else {
        json_resp["code"] = 404;
        json_resp["msg"] = "Order not found";
    }
    
    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 支付订单
static int handle_order_pay(HttpRequest* req, HttpResponse* resp) {
    std::string order_id_str = get_query_string(req->query_params, "order_id");
    
    json json_resp;
    if (order_id_str.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing order_id";
    } else {
        uint64_t order_id = std::stoull(order_id_str);
        if (MySQLService::instance().update_order_status(order_id, 1)) {
        // 1. 更新支付状态成功
        Metrics::instance().inc_orders_paid();
        Logger::instance().info("Order paid status updated in DB: " + order_id_str);
        
        // ==========================================
        // 🚨 核心 SSE 单播推送逻辑 (if-else 完整版)
        // ==========================================
        auto order_opt = MySQLService::instance().get_order_by_id(order_id);
        if (order_opt.has_value()) {
            const auto& order = order_opt.value();
            json event_data = {{"order_id", order_id_str}};
            
            // 探针日志：确认解析出的 user_id
            Logger::instance().info("SSE [order_paid] order_id=" + order_id_str + " target_user=[" + order.user_id + "]");
            
            // 执行精准单播！(彻底告别 broadcast 大喇叭)
            SSEHandler::instance().sendToUser(order.user_id, "order_paid", event_data.dump());
        } else {
            // 探针日志：数据库查无此单
            Logger::instance().error("CRITICAL: order_paid order not found in DB! order_id=" + order_id_str);
        }
        // ==========================================
        
        // 2. 准备 HTTP 成功响应
        json_resp["code"] = 200;
        json_resp["msg"] = "Payment successful";
    } else {
        // 更新支付状态失败
        json_resp["code"] = 500;
        json_resp["msg"] = "Payment failed";
    }
    }
    
    // 3. 发送最终 HTTP 响应
    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200; 
}

// 取消订单（超时未支付）
static int handle_order_cancel(HttpRequest* req, HttpResponse* resp) {
    std::string order_id_str = get_query_string(req->query_params, "order_id");
    
    json json_resp;
    if (order_id_str.empty()) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Missing order_id";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    uint64_t order_id = std::stoull(order_id_str);

    // 获取订单信息
    auto order_opt = MySQLService::instance().get_order_by_id(order_id);
    if (!order_opt.has_value()) {
        json_resp["code"] = 404;
        json_resp["msg"] = "Order not found";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    const auto& order = order_opt.value();
    
    // 检查订单状态，只有待支付(0)才能取消
    if (order.status != 0) {
        json_resp["code"] = 400;
        json_resp["msg"] = "Order cannot be cancelled";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    // 更新订单状态为已取消(2)
    if (!MySQLService::instance().update_order_status(order_id, 2)) {
        json_resp["code"] = 500;
        json_resp["msg"] = "Cancel failed";
        resp->body = json_resp.dump();
        resp->SetContentType("application/json");
        return 200;
    }
    
    // 恢复 Redis 库存
    auto& cfg = Config::instance();
    std::string stock_key = cfg.seckill.stock_prefix + order.product_id;
    std::string order_set_key = cfg.seckill.order_set_prefix + order.product_id;
    std::string cooldown_key = "seckill:cooldown:" + order.user_id + ":" + order.product_id;
    
    auto& redis = RedisService::instance();
    redis.incrby(stock_key, order.quantity);  // 归还库存
    redis.srem(order_set_key, order.user_id);  // 从已购买集合移除
    redis.setex(cooldown_key, 60, "1");  // 设置60秒冷却时间
    
    Metrics::instance().inc_orders_cancelled();
    json_resp["code"] = 200;
    json_resp["msg"] = "Order cancelled, stock returned";
    Logger::instance().info("Order cancelled: " + order_id_str + ", stock returned: " + std::to_string(order.quantity));
    
    // 单播订单取消事件（只通知订单所有者）
    json event_data = {{"order_id", order_id_str}};
    Logger::instance().info("SSE [order_cancelled] order_id=" + order_id_str + " target_user=[" + order.user_id + "]");
    SSEHandler::instance().sendToUser(order.user_id, "order_cancelled", event_data.dump());
    
    resp->body = json_resp.dump();
    resp->SetContentType("application/json");
    return 200;
}

// 注册路由
void register_order_routes(hv::HttpService* service) {
    service->GET("/api/orders",            handle_order_list);
    service->GET("/api/order/detail",      handle_order_detail);
    service->GET("/api/seckill/result",    handle_seckill_result);  // 查询秒杀结果（优先缓存）
    service->POST("/api/order/pay",        handle_order_pay);
    service->POST("/api/order/cancel",     handle_order_cancel);
}

#include "handler/seckill_handler.h"
#include "service/redis_service.h"
#include "service/rate_limiter.h"
#include "service/mysql_service.h"
#include "service/activity_manager.h"
#include "common/logger.h"
#include "common/config.h"
#include "common/metrics.h"
#include "common/snowflake.h"
#include <hv/json.hpp>
#include <hv/HttpMessage.h>
#include <sstream>
#include <random>
#include <chrono>

using json = nlohmann::json;

extern Snowflake g_snow_flake;

// ============================================================
//  辅助函数
// ============================================================

static std::string parse_body_param(const std::string& body, const char* key) {
    if (body.empty()) return "";
    size_t pos = 0;
    while (pos < body.size()) {
        size_t eq  = body.find('=', pos);
        size_t amp = body.find('&', pos);
        if (eq == std::string::npos) break;
        std::string k = body.substr(pos, eq - pos);
        size_t val_start = eq + 1;
        size_t val_end   = (amp == std::string::npos) ? body.size() : amp;
        if (k == key) return body.substr(val_start, val_end - val_start);
        pos = (amp == std::string::npos) ? body.size() : amp + 1;
    }
    return "";
}

static std::string get_param(const HttpRequest* req, const char* key) {
    auto it = req->query_params.find(key);
    if (it != req->query_params.end() && !it->second.empty()) {
        return it->second;
    }
    return parse_body_param(req->body, key);
}

// 优先 JSON body，降级到 query/form body
static std::string get_param2(const HttpRequest* req, const hv::Json& req_json, const char* key) {
    if (!req_json.is_null() && req_json.contains(key)) {
        const hv::Json& val = req_json[key];
        if (val.is_string()) return val.get<std::string>();
        return val.dump();
    }
    return get_param(req, key);
}

// ============================================================
//  🚀 异步秒杀处理
// ============================================================
static int handle_seckill(const HttpContextPtr& ctx) {
    try {
        Metrics::instance().inc_requests_total();
        auto& cfg = Config::instance();

        const hv::Json& req_json = ctx->request->GetJson();

        // 1. 限流检查
        std::string client_ip = ctx->ip();
        if (!RateLimiter::instance().allow(client_ip)) {
            Metrics::instance().inc_seckill_fail_ratelimit();
            ctx->response->status_code = HTTP_STATUS_TOO_MANY_REQUESTS;
            return ctx->send(R"({"code":429,"msg":"Too Many Requests"})");
        }

        // 1.5 活动状态检查
        std::string product_id = get_param2(ctx->request.get(), req_json, "product_id");

        if (product_id.empty()) {
            ctx->response->status_code = HTTP_STATUS_BAD_REQUEST;
            return ctx->send(R"({"code":400,"msg":"Missing product_id"})");
        }

        auto activity_opt = ActivityManager::instance().get_active_activity(product_id);
        if (!activity_opt.has_value()) {
            Metrics::instance().inc_seckill_fail_ratelimit();
            ctx->response->status_code = static_cast<http_status>(403);
            return ctx->send(R"({"code":403,"msg":"Seckill activity not found or ended"})");
        }

        // 2. 参数解析
        std::string user_id = get_param2(ctx->request.get(), req_json, "user_id");
        std::string qty_str = get_param2(ctx->request.get(), req_json, "quantity");
        std::string token   = get_param2(ctx->request.get(), req_json, "token");
        if (qty_str.empty()) qty_str = "1";
        int quantity = std::atoi(qty_str.c_str());

        if (user_id.empty()) {
            ctx->response->status_code = HTTP_STATUS_BAD_REQUEST;
            return ctx->send(R"({"code":400,"msg":"Missing parameters"})");
        }

        // 2.1 用户 ID 限流
        if (!user_id.empty()) {
            if (!RateLimiter::instance().allow_user(user_id)) {
                Metrics::instance().inc_seckill_fail_ratelimit();
                ctx->response->status_code = HTTP_STATUS_TOO_MANY_REQUESTS;
                return ctx->send(R"({"code":429,"msg":"User rate limit exceeded"})");
            }
        }

        // 2.2 Token 验证
        if (cfg.security.token_required) {
            if (token.empty()) {
                ctx->response->status_code = HTTP_STATUS_UNAUTHORIZED;
                return ctx->send(R"({"code":401,"msg":"Token required"})");
            }
            if (!RedisService::instance().verifySeckillToken(token, user_id, product_id)) {
                ctx->response->status_code = HTTP_STATUS_UNAUTHORIZED;
                return ctx->send(R"({"code":401,"msg":"Invalid or expired token"})");
            }
        } else {
            if (token.empty()) {
                Logger::instance().warn("Token verification bypassed (test mode) for user=" + user_id);
            }
        }

        // 本地预扣减
        if (!RedisService::instance().tryLocalPreDeduct(product_id)) {
            Metrics::instance().inc_seckill_fail_sold();
            ctx->response->status_code = static_cast<http_status>(410);
            return ctx->send(R"({"code":410,"msg":"Sold out"})");
        }

        // 3. 生成订单 ID
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        uint64_t order_id_int = g_snow_flake.nextId();
        std::string order_id  = std::to_string(order_id_int);

        std::string order_data = order_id + "|" + user_id + "|" +
                                 product_id + "|" + std::to_string(quantity) + "|" +
                                 std::to_string(now_ms);

        // 4. 异步 Redis 调用
        RedisService::instance().executeSeckillAsync(
            product_id, user_id, order_data,
            cfg.seckill.request_cooldown_seconds,

            [ctx, order_id, product_id](int result) {
                json resp;
                int status = 200;

                switch (result) {
                    case 1:
                        resp["code"]     = 200;
                        resp["msg"]      = "Seckill success";
                        resp["order_id"] = order_id;
                        Metrics::instance().inc_seckill_success();
                        Logger::instance().info("Seckill success: order_id=" + order_id);
                        break;
                    case 0:
                        resp["code"] = 410;
                        resp["msg"]  = "Sold out";
                        status = 410;
                        Metrics::instance().inc_seckill_fail_sold();
                        break;
                    case -2:
                        resp["code"] = 409;
                        resp["msg"]  = "Already purchased";
                        status = 409;
                        Metrics::instance().inc_seckill_fail_dup();
                        break;
                    case -3:
                        resp["code"] = 429;
                        resp["msg"]  = "Cooldown, please wait";
                        status = 429;
                        Metrics::instance().inc_seckill_fail_ratelimit();
                        break;
                    default:
                        resp["code"] = 503;
                        resp["msg"]  = "Service Unavailable";
                        status = 503;
                        break;
                }

                if (result != 1) {
                    RedisService::instance().rollbackLocalPreDeduct(product_id);
                }

                ctx->response->status_code = static_cast<http_status>(status);
                ctx->send(resp.dump());
            }
        );

        return 0;

    } catch (const std::exception& e) {
        Logger::instance().error("Seckill handler exception: " + std::string(e.what()));
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return ctx->send(R"({"code":500,"msg":"Internal error"})");
    } catch (...) {
        Logger::instance().error("Seckill handler unknown exception");
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return ctx->send(R"({"code":500,"msg":"Internal error"})");
    }
}

// ============================================================
//  🔐 获取秒杀 Token
// ============================================================
static int handle_get_token(const HttpContextPtr& ctx) {
    try {
        const hv::Json& req_json = ctx->request->GetJson();

        std::string user_id    = get_param2(ctx->request.get(), req_json, "user_id");
        std::string product_id = get_param2(ctx->request.get(), req_json, "product_id");

        if (user_id.empty() || product_id.empty()) {
            ctx->response->status_code = HTTP_STATUS_BAD_REQUEST;
            return ctx->send(R"({"code":400,"msg":"Missing user_id or product_id"})");
        }

        if (!Config::instance().security.token_required) {
            json resp;
            resp["code"] = 200;
            resp["token"] = "TEST_TOKEN_" + user_id + "_" + product_id;
            resp["expires_in"] = 0;
            resp["mode"] = "test";
            ctx->response->status_code = HTTP_STATUS_OK;
            return ctx->send(resp.dump());
        }

        std::string token = RedisService::instance().generateSeckillToken(user_id, product_id);

        json resp;
        resp["code"] = 200;
        resp["token"] = token;
        resp["expires_in"] = Config::instance().security.token_expire_seconds;

        ctx->response->status_code = HTTP_STATUS_OK;
        return ctx->send(resp.dump());

    } catch (const std::exception& e) {
        Logger::instance().error("Get token exception: " + std::string(e.what()));
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return ctx->send(R"({"code":500,"msg":"Internal error"})");
    }
}

// ============================================================
//  获取商品列表
// ============================================================
static int handle_list_products(const HttpContextPtr& ctx) {
    try {
        auto products = MySQLService::instance().get_all_products();

        json resp;
        resp["code"]     = 200;
        resp["products"] = json::array();

        for (const auto& p : products) {
            std::string stock_key    = Config::instance().seckill.stock_prefix + p.id;
            std::string redis_stock  = RedisService::instance().get(stock_key);

            bool seckill_active = !redis_stock.empty() && std::stoi(redis_stock) > 0;
            int  seckill_stock  = redis_stock.empty() ? 0 : std::stoi(redis_stock);

            json obj = {
                {"id",             p.id},
                {"name",           p.name},
                {"stock",          p.stock},
                {"seckill_stock",  seckill_stock},
                {"seckill_active", seckill_active},
                {"price",          p.price}
            };
            resp["products"].push_back(obj);
        }

        ctx->response->status_code = HTTP_STATUS_OK;
        return ctx->send(resp.dump());

    } catch (const std::exception& e) {
        Logger::instance().error("List products exception: " + std::string(e.what()));
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return ctx->send(R"({"code":500,"msg":"Internal error"})");
    } catch (...) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        return ctx->send(R"({"code":500,"msg":"Internal error"})");
    }
}

// ============================================================
//  注册路由
// ============================================================
void register_seckill_routes(hv::HttpService* service) {
    service->POST("/api/seckill/buy",    handle_seckill);
    service->GET("/api/seckill/token",   handle_get_token);
    service->GET("/api/products",        handle_list_products);
    service->GET("/api/seckill/products", handle_list_products);
}

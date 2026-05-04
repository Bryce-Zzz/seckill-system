#include "handler/seckill_handler.h"
#include "service/redis_service.h"
#include "service/rate_limiter.h"
#include "service/mysql_service.h"
#include "common/logger.h"
#include "common/config.h"
#include "common/metrics.h"
#include "common/snowflake.h"
#include <hv/json.hpp>
#include <hv/HttpMessage.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>

using json = nlohmann::json;

// 引用 main.cpp 中定义的全局雪花算法实例
extern Snowflake g_snowflake;

// ============================================================
//  辅助函数
// ============================================================

// 从 POST body 中手动解析 URL 编码参数（兼容 application/x-www-form-urlencoded）
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

// 从请求中获取参数（优先 query，fallback POST body）
static std::string get_param(const HttpRequest* req, const char* key) {
    // query params (GET ?user_id=xxx 或 POST ?user_id=xxx)
    auto it = req->query_params.find(key);
    if (it != req->query_params.end() && !it->second.empty()) {
        return it->second;
    }
    // body (POST application/x-www-form-urlencoded)
    return parse_body_param(req->body, key);
}

// ============================================================
//  🚀 异步秒杀处理（HttpContextPtr + RedisAsync）
//
//  新流程：
//    HTTP 线程 → 限流检查 → 解析参数 → 发起 async 调用 → 立刻 return 0
//    Redis IO 线程 → EVALSHA → callback → ctx->send() 发送响应
//
//  关键：ctx 按值捕获 [ctx]，shared_ptr 引用计数 +1，
//        等 Redis 回调执行完，shared_ptr 销毁，ctx 才被回收。
// ============================================================
static int handle_seckill(const HttpContextPtr& ctx) {
    try {
        Metrics::instance().inc_requests_total();
        auto& cfg = Config::instance();

        // ── 1. 限流检查（纳秒级，内存操作，同步执行）────────────
        std::string client_ip = ctx->ip();
        if (!RateLimiter::instance().allow(client_ip)) {
            Metrics::instance().inc_seckill_fail_ratelimit();
            ctx->response->status_code = HTTP_STATUS_TOO_MANY_REQUESTS;
            return ctx->send(R"({"code":429,"msg":"Too Many Requests"})");
        }

        // ── 2. 参数解析 ────────────────────────────────────────
        std::string user_id     = get_param(ctx->request.get(), "user_id");
        std::string product_id  = get_param(ctx->request.get(), "product_id");
        std::string qty_str     = get_param(ctx->request.get(), "quantity");
        std::string token       = get_param(ctx->request.get(), "token");
        if (qty_str.empty()) qty_str = "1";
        int quantity = std::atoi(qty_str.c_str());

        if (user_id.empty() || product_id.empty()) {
            ctx->response->status_code = HTTP_STATUS_BAD_REQUEST;
            return ctx->send(R"({"code":400,"msg":"Missing parameters"})");
        }

        // ── 2.1 用户 ID 维度细粒度限流（新增，Phase 1）
        // 注意：此检查在参数解析之后执行，因为需要 user_id 作为限流 key
        if (!user_id.empty()) {
            if (!RateLimiter::instance().allow_user(user_id)) {
                Metrics::instance().inc_seckill_fail_ratelimit();
                ctx->response->status_code = HTTP_STATUS_TOO_MANY_REQUESTS;
                return ctx->send(R"({"code":429,"msg":"User rate limit exceeded"})");
            }
        }

        // ── 2.2 动态 Token 验证（防脚本直刷）───────────────────
        if (cfg.security.token_required) {
            // Token 为空 → 拒绝
            if (token.empty()) {
                ctx->response->status_code = HTTP_STATUS_UNAUTHORIZED;
                return ctx->send(R"({"code":401,"msg":"Token required"})");
            }
            // 验证 Token 有效性
            if (!RedisService::instance().verifySeckillToken(token, user_id, product_id)) {
                ctx->response->status_code = HTTP_STATUS_UNAUTHORIZED;
                return ctx->send(R"({"code":401,"msg":"Invalid or expired token"})");
            }
        } else {
            // 测试模式：打印警告日志
            if (token.empty()) {
                Logger::instance().warn("Token verification bypassed (test mode) for user=" + user_id);
            }
        }

        // 本地预扣减：若本地库存不足，直接返回售罄，不打 Redis
        if (!RedisService::instance().tryLocalPreDeduct(product_id)) {
            Metrics::instance().inc_seckill_fail_sold();
            ctx->response->status_code = static_cast<http_status>(410);
            return ctx->send(R"({"code":410,"msg":"Sold out"})");
        }

        // ── 3. 生成订单 ID（雪花算法，全局唯一 64-bit 整数，线程安全）──
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        uint64_t order_id_int = g_snowflake.nextId();
        std::string order_id  = std::to_string(order_id_int);  // 传给前端用字符串，防 JS 精度丢失

        // order_data 格式：order_id|user_id|product_id|quantity|timestamp
        std::string order_data = order_id + "|" + user_id + "|" +
                                 product_id + "|" + std::to_string(quantity) + "|" +
                                 std::to_string(now_ms);

        // ── 4. 发起异步 Redis 调用，立刻返回 0 ───────────────────
        RedisService::instance().executeSeckillAsync(
            product_id, user_id, order_data,
            cfg.seckill.request_cooldown_seconds,

            // [ctx, product_id] 按值捕获：shared_ptr 引用计数 +1，生命周期延长到回调执行完
            [ctx, order_id, product_id](int result) {
                json resp;
                int status = 200;

                switch (result) {
                    case 1:
                        // ✅ 秒杀成功
                        resp["code"]     = 200;
                        resp["msg"]      = "Seckill success";
                        resp["order_id"] = order_id;
                        Metrics::instance().inc_seckill_success();
                        Logger::instance().info("Seckill success: order_id=" + order_id);
                        break;
                    case 0:
                        // 售罄
                        resp["code"] = 410;
                        resp["msg"]  = "Sold out";
                        status = 410;
                        Metrics::instance().inc_seckill_fail_sold();
                        break;
                    case -2:
                        // 已购买
                        resp["code"] = 409;
                        resp["msg"]  = "Already purchased";
                        status = 409;
                        Metrics::instance().inc_seckill_fail_dup();
                        break;
                    case -3:
                        // 冷却中
                        resp["code"] = 429;
                        resp["msg"]  = "Cooldown, please wait";
                        status = 429;
                        Metrics::instance().inc_seckill_fail_ratelimit();
                        break;
                    default:
                        // Redis 错误
                        resp["code"] = 503;
                        resp["msg"]  = "Service Unavailable";
                        status = 503;
                        break;
                }

                // 若秒杀失败，回滚本地预扣减
                if (result != 1) {
                    RedisService::instance().rollbackLocalPreDeduct(product_id);
                }

                // 🌟 在 Redis IO 线程中发送响应
                ctx->response->status_code = static_cast<http_status>(status);
                ctx->send(resp.dump());
            }
        );

        // ── 5. 立刻返回 0，HTTP 线程解放出来接待下一个请求 ─────
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
//  🔐 获取秒杀 Token（动态地址隐藏）
//
//  前端需要在秒杀开始前先调用此接口获取 Token，
//  然后在 /api/seckill/buy 请求中带上 Token 参数。
//
//  返回格式：{"code":200,"token":"xxx","expires_in":30}
// ============================================================
static int handle_get_token(const HttpContextPtr& ctx) {
    try {
        std::string user_id    = get_param(ctx->request.get(), "user_id");
        std::string product_id = get_param(ctx->request.get(), "product_id");

        if (user_id.empty() || product_id.empty()) {
            ctx->response->status_code = HTTP_STATUS_BAD_REQUEST;
            return ctx->send(R"({"code":400,"msg":"Missing user_id or product_id"})");
        }

        // 检查 Token 是否已开启
        if (!Config::instance().security.token_required) {
            // 测试模式：返回固定 Token
            json resp;
            resp["code"] = 200;
            resp["token"] = "TEST_TOKEN_" + user_id + "_" + product_id;
            resp["expires_in"] = 0;
            resp["mode"] = "test";
            ctx->response->status_code = HTTP_STATUS_OK;
            return ctx->send(resp.dump());
        }

        // 生成真正的 Token
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
//  获取商品列表（保持同步，list_products 不需要异步）
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
//  注册路由（HttpContextPtr 异步模式）
// ============================================================
void register_seckill_routes(hv::HttpService* service) {
    service->POST("/api/seckill/buy",      handle_seckill);
    service->GET("/api/seckill/token",     handle_get_token);  // 获取秒杀 Token
    service->GET("/api/products",           handle_list_products);
    service->GET("/api/seckill/products",   handle_list_products);
}

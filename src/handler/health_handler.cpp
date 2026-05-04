#include "handler/health_handler.h"
#include "service/redis_service.h"
#include "service/mysql_service.h"
#include "common/logger.h"
#include <hv/json.hpp>
#include <chrono>

using json = nlohmann::json;

// 服务启动时间（用于计算 uptime）
static int64_t g_start_time = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

static int handle_health(HttpRequest* req, HttpResponse* resp) {
    (void)req;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 探测 Redis 连通性
    bool redis_ok = false;
    try {
        std::string pong = RedisService::instance().get("__health_ping__");
        redis_ok = true;   // 不抛异常即认为连通
    } catch (...) {}

    // 探测 MySQL 连通性
    bool mysql_ok = false;
    try {
        auto products = MySQLService::instance().get_all_products();
        mysql_ok = true;
    } catch (...) {}

    bool all_ok = redis_ok && mysql_ok;

    json body = {
        {"status",  all_ok ? "ok" : "degraded"},
        {"uptime_seconds", now - g_start_time},
        {"components", {
            {"redis", redis_ok ? "ok" : "error"},
            {"mysql", mysql_ok ? "ok" : "error"}
        }}
    };

    resp->body = body.dump();
    resp->SetContentType("application/json");
    return all_ok ? 200 : 503;
}

void register_health_routes(hv::HttpService* service) {
    service->GET("/health", handle_health);
    service->GET("/api/health", handle_health);
}

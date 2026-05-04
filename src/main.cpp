    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include <iostream>
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include <memory>
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include <csignal>
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include <thread>
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include <chrono>
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "common/globals.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "server/http_server.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "handler/sse_handler.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "service/redis_service.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "service/mysql_service.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "service/order_processor.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "service/timeout_checker.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "service/rate_limiter.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "common/logger.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
#include "common/config.h"
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
// 全局运行标志定义
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
bool g_running = true;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
void signal_handler(int sig) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    (void)sig;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    g_running = false;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
}
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
int main(int argc, char** argv) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::cout << "=======================================\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::cout << "     C++ Seckill System v2.0\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::cout << "=======================================\n\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 加载配置
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::string config_path = "config/config.yaml";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (argc > 1) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        config_path = argv[1];
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (!Config::instance().load(config_path)) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        std::cerr << "Failed to load config: " << config_path << "\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        return 1;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    auto& cfg = Config::instance();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化日志（支持 JSON 格式）
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    Logger::instance().init(cfg.log.file, cfg.log.level, cfg.log.format);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    Logger::instance().info("Starting seckill system...");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化 Redis
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (!RedisService::instance().init(cfg.redis.host, cfg.redis.port,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                      cfg.redis.password, cfg.redis.db,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                      cfg.redis.pool_size)) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        Logger::instance().error("Failed to init Redis");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        return 1;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化 MySQL
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (!MySQLService::instance().init(cfg.mysql.host, cfg.mysql.port,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                      cfg.mysql.user, cfg.mysql.password,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                      cfg.mysql.database, cfg.mysql.pool_size)) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        Logger::instance().error("Failed to init MySQL");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        return 1;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化限流器
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    RateLimiter::instance().init(cfg.rate_limit.token_rate, cfg.rate_limit.token_capacity,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                 cfg.rate_limit.window_size, cfg.rate_limit.window_max);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 临时测试
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    fprintf(stderr, "DEBUG001: entering main...
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
"); fflush(stderr);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    fprintf(stderr, "MAIN START
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    fflush(stderr);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化订单处理器
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    OrderProcessor::instance().init(cfg.seckill.order_stream_key);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    OrderProcessor::instance().start();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化订单超时检查器
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    TimeoutChecker::instance().init(cfg.timeout.order_timeout_seconds,
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
                                    cfg.timeout.check_interval_seconds);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    TimeoutChecker::instance().start();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 初始化 HTTP 服务器
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (!SeckillHttpServer::instance().init(cfg.server.port, cfg.server.worker_threads)) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        Logger::instance().error("Failed to init HTTP server");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        return 1;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 启动 SSE 服务器（独立端口，用于实时推送）
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    int sse_port = cfg.server.port + 1;  // SSE 使用 HTTP 端口 + 1
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    if (!SSEHandler::instance().start(sse_port)) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        Logger::instance().warn("Failed to start SSE server, real-time updates disabled");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    } else {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        Logger::instance().info("SSE server started on port " + std::to_string(sse_port));
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 注册信号处理
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    signal(SIGINT, signal_handler);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    signal(SIGTERM, signal_handler);
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 启动服务器
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    SeckillHttpServer::instance().start();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::cout << "\nServer is running. Press Ctrl+C to stop.\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    std::cout << "SSE events: http://localhost:" << sse_port << "\n\n";
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 等待退出信号
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    while (g_running) {
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    }
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    Logger::instance().info("Seckill system stopped");
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    // 清理
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    SSEHandler::instance().stop();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    SeckillHttpServer::instance().stop();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    TimeoutChecker::instance().stop();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    OrderProcessor::instance().stop();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    RedisService::instance().close();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    MySQLService::instance().close();
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);

    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
    return 0;
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);
}
    // DEBUG
    fprintf(stderr, "DEBUG_BEFORE_MYSQL_INIT
");
    fflush(stderr);


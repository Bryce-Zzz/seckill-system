#include <iostream>
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>
#include <mysql/mysql.h>
#include "common/globals.h"
#include "common/snowflake.h"
#include "server/http_server.h"
#include "handler/sse_handler.h"
#include "service/redis_service.h"
#include "service/mysql_service.h"
#include "service/mysql_pool.h"
#include "service/order_processor.h"
#include "service/timeout_checker.h"
#include "service/rate_limiter.h"
#include "common/logger.h"
#include "common/config.h"
#include "mq/IMessageQueue.h"

// 全局运行标志定义
bool g_running = true;

// 全局雪花算法 ID 生成器（workerId=1，单机部署；分布式环境从配置/环境变量读取）
Snowflake g_snowflake(1);

// 热更新：保存配置路径供 SIGHUP 处理器使用
std::string g_config_path = "config/config.yaml";

/**
 * SIGHUP 信号处理器：热更新配置
 * 收到 kill -1 时，自动重新加载 config.yaml
 * 可动态修改：限流参数、日志级别、Stream 配置等
 */
void handle_sighup(int sig) {
    if (sig == SIGHUP) {
        // 注意：Logger 本身需要热更新日志级别，单独处理
        Logger::instance().info("Received SIGHUP, reloading config from: " + g_config_path);

        // 1. 重新加载配置
        if (Config::instance().reload(g_config_path)) {
            // 2. 热更新限流器参数
            auto& cfg = Config::instance();
            RateLimiter::instance().update_params(
                cfg.rate_limit.token_rate.load(),
                cfg.rate_limit.token_capacity.load(),
                cfg.rate_limit.window_size.load(),
                cfg.rate_limit.window_max.load()
            );

            // 3. 热更新日志级别
            Logger::instance().set_level(cfg.log.level);

            Logger::instance().info("Config hot-reload SUCCESS: rate_limit and log_level updated");
        } else {
            Logger::instance().error("Config hot-reload FAILED!");
        }
    }
}

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char** argv) {
    // ⚠️ MySQL 库必须在所有线程创建前初始化！
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        std::cerr << "[ERROR] mysql_library_init failed" << std::endl;
        return 1;
    }

    std::cout << "=======================================\n";
    std::cout << "     C++ Seckill System v3.0\n";
    std::cout << "  (Redis Stream + MySQL Consumer)\n";
    std::cout << "=======================================\n";
    std::cout << "\n[CRITICAL] Snowflake Worker ID: " << g_snowflake.getWorkerId() << "\n";
    std::cout << "(分布式部署时必须确保各节点 worker_id 互不冲突！)\n\n";

    // 加载配置
    if (argc > 1) {
        g_config_path = argv[1];
    }
    
    if (!Config::instance().load(g_config_path)) {
        std::cerr << "[ERROR] Config load failed: " << g_config_path << std::endl;
        return 1;
    }

    auto& cfg = Config::instance();

    // 初始化日志（支持 JSON 格式）
    Logger::instance().init(cfg.log.file, cfg.log.level, cfg.log.format);
    Logger::instance().info("Starting seckill system...");

    // [CRITICAL] 醒目日志：防止分布式部署时 worker_id 冲突被忽略
    Logger::instance().warn("[CRITICAL] Snowflake Worker ID: " + std::to_string(g_snowflake.getWorkerId()) +
                            " — 确保分布式环境下各节点 worker_id 互不冲突！");

    // 初始化 Redis（主连接，用于 HTTP 秒杀请求）
    if (!RedisService::instance().init(cfg.redis.host, cfg.redis.port,
                                      cfg.redis.password, cfg.redis.db,
                                      cfg.redis.pool_size)) {
        Logger::instance().error("Failed to init Redis");
        return 1;
    }
    
    // Lua 脚本已在 RedisService::init() 内部预加载（EVALSHA 极致优化：只传 40 字节 SHA1）

    // 初始化 MySQL 连接池
    MySQLPool::instance().init(cfg.mysql.host, cfg.mysql.user, cfg.mysql.password,
                              cfg.mysql.database, cfg.mysql.port, cfg.mysql.pool_size);

    // 初始化 MySQL Service (兼容旧接口)
    if (!MySQLService::instance().init(cfg.mysql.host, cfg.mysql.port,
                                      cfg.mysql.user, cfg.mysql.password,
                                      cfg.mysql.database, cfg.mysql.pool_size)) {
        Logger::instance().error("Failed to init MySQL");
        return 1;
    }

    // 初始化本地库存缓存（需要在 MySQL 初始化后调用）
    RedisService::instance().initAllLocalStock();

    // 初始化限流器
    RateLimiter::instance().init(cfg.rate_limit.token_rate, cfg.rate_limit.token_capacity,
                                 cfg.rate_limit.window_size, cfg.rate_limit.window_max);

    // sw::redis++ RedisAsync 连接池已在 RedisService::init() 内部建立

    // ========== 创建 MQ 实例（依赖注入）==========
    // 使用独立的 Redis 连接，专门用于消费 Stream，不会阻塞 HTTP 线程
    std::shared_ptr<IMessageQueue> mq = createRedisStreamMQ(
        cfg.redis.host,
        cfg.redis.port,
        cfg.redis.password,
        cfg.redis.db
    );
    
    // 初始化订单处理器（注入 MQ 实例）
    OrderProcessor processor(mq);
    processor.init(
        cfg.seckill.order_stream_key,  // topic: "seckill:stream:orders"
        "mysql_writer_group",           // 消费者组名
        "node_1_thread_1"                // 消费者名（分布式环境下应使用机器名+线程ID）
    );
    processor.start();

    // 初始化订单超时检查器
    TimeoutChecker::instance().init(cfg.timeout.order_timeout_seconds,
                                   cfg.timeout.check_interval_seconds);
    TimeoutChecker::instance().start();

    // 初始化 HTTP 服务器
    if (!SeckillHttpServer::instance().init(cfg.server.port, cfg.server.worker_threads)) {
        Logger::instance().error("Failed to init HTTP server");
        return 1;
    }

    // 启动 SSE 服务器（独立端口，用于实时推送）
    int sse_port = cfg.server.port + 1;  // SSE 使用 HTTP 端口 + 1
    if (!SSEHandler::instance().start(sse_port)) {
        Logger::instance().warn("Failed to start SSE server, real-time updates disabled");
    } else {
        Logger::instance().info("SSE server started on port " + std::to_string(sse_port));
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  handle_sighup);  // 热更新信号：kill -1
    Logger::instance().info("Signal handlers registered: SIGINT, SIGTERM, SIGHUP");

    // 启动服务器
    SeckillHttpServer::instance().start();

    std::cout << "\nServer is running. Press Ctrl+C to stop.\n";
    std::cout << "SSE events: http://localhost:" << sse_port << "\n\n";

    // 等待退出信号
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    Logger::instance().info("Seckill system stopped");

    // 清理
    SSEHandler::instance().stop();
    SeckillHttpServer::instance().stop();
    TimeoutChecker::instance().stop();
    processor.stop();  // 停止 MQ 消费者
    RedisService::instance().close();
    MySQLService::instance().close();
    mysql_library_end();

    return 0;
}

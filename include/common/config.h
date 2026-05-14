#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <string>
#include <map>
#include <atomic>
#include <mutex>

class Config {
public:
    static Config& instance() {
        static Config inst;
        return inst;
    }

    /**
     * 热更新：重新加载配置文件（可被 SIGHUP 信号触发）
     * 线程安全，使用 reload_mutex_ 保护 YAML 解析过程
     * atomic 成员可在工作线程读取时无锁安全更新
     */
    bool load(const std::string& path);
    bool reload(const std::string& path);

    struct Server {
        int port = 9000;
        int worker_threads = 4;
        std::string static_path = "web";
    };

    struct Redis {
        std::string host = "127.0.0.1";
        int port = 6379;
        std::string password;
        int db = 0;
        int pool_size = 16;
    };

    struct MySQL {
        std::string host = "127.0.0.1";
        int port = 3306;
        std::string user = "seckill";
        std::string password = "seckill123";
        std::string database = "seckill";
        int pool_size = 8;
    };

    /**
     * RateLimit：设为 atomic，支持热更新
     * 工作线程读取时无需加锁，主线程 reload 时原子更新
     */
    struct RateLimit {
        std::atomic<int> token_rate{10000};       // 每秒生成令牌数
        std::atomic<int> token_capacity{50000};   // 令牌桶容量
        std::atomic<int> window_size{60};          // 滑动窗口大小(秒)
        std::atomic<int> window_max{100000};       // 滑动窗口最大请求数（全局/IP维度）
        std::atomic<int> user_window_size{10};      // 用户维度滑动窗口大小(秒)
        std::atomic<int> user_window_max{5};        // 用户维度窗口最大请求数
    };

    struct Seckill {
        std::string order_stream_key = "seckill:orders";
        std::string stock_prefix = "seckill:stock:";
        std::string order_set_prefix = "seckill:orders:";
        int lua_script_id = 1;
        int request_cooldown_seconds = 5;
        int activity_sync_interval = 30;            // ActivityManager 后台同步间隔（秒）
        std::atomic<int> stream_maxlen{100000};    // atomic：可热更新
    };

    struct Log {
        std::string file   = "logs/seckill.log";
        std::string level = "info";   // 日志级别（非 atomic，但 reload 时受 reload_mutex_ 保护）
        std::string format = "text";
    };

    struct Timeout {
        int order_timeout_seconds = 300;
        int check_interval_seconds = 30;
    };

    struct Security {
        bool token_required = true;                    // 是否启用动态 Token 验证
        std::string token_salt = "default_salt_change"; // Token 加密盐值（生产环境必须修改）
        std::string test_bypass_secret;                 // 测试模式旁路密钥（可选）
        int token_expire_seconds = 30;                  // Token 有效期（秒）
    };

    Timeout timeout;
    Server server;
    Redis redis;
    MySQL mysql;
    RateLimit rate_limit;
    Seckill seckill;
    Log log;
    Security security;

private:
    std::mutex reload_mutex_;
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};

#endif // COMMON_CONFIG_H

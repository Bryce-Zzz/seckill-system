#include "common/config.h"
#include "service/rate_limiter.h"
#include "service/mysql_pool.h"
#include <fstream>
#include <iostream>

#ifdef __linux__
#include <yaml-cpp/yaml.h>
#else
#include <yaml.h>
#endif

bool Config::load(const std::string& path) {
    try {
#ifdef __linux__
        YAML::Node config = YAML::LoadFile(path);
#else
        std::ifstream fin(path);
        if (!fin) {
            std::cerr << "Config file not found: " << path << std::endl;
            return false;
        }
        // Simple YAML parsing for Windows
        std::string line;
        std::map<std::string, std::string> values;
        while (std::getline(fin, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                // trim
                while (val.size() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
                while (val.size() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
                values[key] = val;
            }
        }
#endif

#ifdef __linux__
        // Server config
        if (config["server"]) {
            server.port = config["server"]["port"].as<int>(9000);
            server.worker_threads = config["server"]["worker_threads"].as<int>(4);
            if (config["server"]["static_path"])
                server.static_path = config["server"]["static_path"].as<std::string>();
        }

        // Redis config
        if (config["redis"]) {
            redis.host = config["redis"]["host"].as<std::string>("127.0.0.1");
            redis.port = config["redis"]["port"].as<int>(6379);
            redis.password = config["redis"]["password"].as<std::string>("");
            redis.db = config["redis"]["db"].as<int>(0);
            redis.pool_size = config["redis"]["pool_size"].as<int>(16);
        }

        // MySQL config
        if (config["mysql"]) {
            mysql.host = config["mysql"]["host"].as<std::string>("127.0.0.1");
            mysql.port = config["mysql"]["port"].as<int>(3306);
            mysql.user = config["mysql"]["user"].as<std::string>("seckill");
            mysql.password = config["mysql"]["password"].as<std::string>("seckill123");
            mysql.database = config["mysql"]["database"].as<std::string>("seckill");
            mysql.pool_size = config["mysql"]["pool_size"].as<int>(8);
        }

        // Rate limit config
        if (config["rate_limit"]) {
            // 严格匹配 YAML 层级：rate_limit -> token_bucket -> rate/capacity
            if (config["rate_limit"]["token_bucket"]) {
                rate_limit.token_rate = config["rate_limit"]["token_bucket"]["rate"].as<int>(-1);
                rate_limit.token_capacity = config["rate_limit"]["token_bucket"]["capacity"].as<int>(-1);
            }
            // sliding_window 配置（备用）
            if (config["rate_limit"]["sliding_window"]) {
                rate_limit.window_size = config["rate_limit"]["sliding_window"]["window_size"].as<int>(60);
                rate_limit.window_max = config["rate_limit"]["sliding_window"]["max_requests"].as<int>(100000);
            }
            // 用户维度滑动窗口配置（Phase 1 新增）
            if (config["rate_limit"]["user_window"]) {
                rate_limit.user_window_size = config["rate_limit"]["user_window"]["window_size"].as<int>(10);
                rate_limit.user_window_max = config["rate_limit"]["user_window"]["max_requests"].as<int>(5);
            }

            // 初始化限流器（包括用户维度参数）
            RateLimiter::instance().update_params(
                rate_limit.token_rate.load(),
                rate_limit.token_capacity.load(),
                rate_limit.window_size.load(),
                rate_limit.window_max.load()
            );
            RateLimiter::instance().update_user_params(
                rate_limit.user_window_size.load(),
                rate_limit.user_window_max.load()
            );
        }

        // Seckill config
        if (config["seckill"]) {
            seckill.order_stream_key = config["seckill"]["order_stream_key"].as<std::string>("seckill:orders");
            seckill.stock_prefix = config["seckill"]["stock_prefix"].as<std::string>("seckill:stock:");
            seckill.order_set_prefix = config["seckill"]["order_set_prefix"].as<std::string>("seckill:orders:");
            seckill.request_cooldown_seconds = config["seckill"]["request_cooldown_seconds"].as<int>(5);
            seckill.stream_maxlen = config["seckill"]["stream_maxlen"].as<int>(100000);
            seckill.activity_sync_interval = config["seckill"]["activity_sync_interval"].as<int>(30);
        }

        // Log config
        if (config["log"]) {
            log.file   = config["log"]["file"].as<std::string>("logs/seckill.log");
            log.level  = config["log"]["level"].as<std::string>("info");
            log.format = config["log"]["format"].as<std::string>("text");
        }

        // Timeout config
        if (config["timeout"]) {
            timeout.order_timeout_seconds  = config["timeout"]["order_timeout_seconds"].as<int>(300);
            timeout.check_interval_seconds = config["timeout"]["check_interval_seconds"].as<int>(30);
        }

        // Security config (for dynamic token)
        if (config["security"]) {
            security.token_required = config["security"]["token_required"].as<bool>(true);
            security.token_salt = config["security"]["token_salt"].as<std::string>("default_salt_change");
            security.test_bypass_secret = config["security"]["test_bypass_secret"].as<std::string>("");
            security.token_expire_seconds = config["security"]["token_expire_seconds"].as<int>(30);
        }
#else
        // Windows fallback - use default values
        (void)path;
#endif

        std::cout << "Config loaded:\n";
        std::cout << "  Server port: " << server.port << "\n";
        std::cout << "  Redis: " << redis.host << ":" << redis.port << "\n";
        std::cout << "  MySQL: " << mysql.host << ":" << mysql.port << "\n";

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << std::endl;
        return false;
    }
}

// 热更新：线程安全地重新加载配置
// 被 SIGHUP 信号处理器调用
bool Config::reload(const std::string& path) {
    std::lock_guard<std::mutex> lock(reload_mutex_);
    return load(path);
}

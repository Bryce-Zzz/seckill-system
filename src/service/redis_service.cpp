// ============================================================
//  RedisService — sw::redis++ AsyncRedis 纯异步实现
// ============================================================
#include "service/redis_service.h"
#include "common/logger.h"
#include "common/config.h"
#include "service/mysql_service.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <openssl/md5.h>  // OpenSSL MD5

using namespace sw::redis;

// ============================================================
//  初始化：同步客户端 + 异步客户端
// ============================================================
bool RedisService::init(const std::string& host, int port,
                        const std::string& password,
                        int db, int pool_size) {
    host_     = host;
    port_     = port;
    password_ = password;
    db_       = db;

    // ── 同步客户端（给 get/set 等低频操作用）─────────────────
    ConnectionOptions sync_opts;
    sync_opts.host     = host;
    sync_opts.port     = port;
    sync_opts.password = password;
    sync_opts.db       = db;

    ConnectionPoolOptions sync_pool_opts;
    sync_pool_opts.size = std::max(4, pool_size / 8);  // 同步连接少一些就够了

    try {
        sync_redis_ = std::make_unique<Redis>(sync_opts, sync_pool_opts);
    } catch (const std::exception& e) {
        Logger::instance().error("Redis sync client init failed: " + std::string(e.what()));
        return false;
    }

    // ── 异步客户端（秒杀专用，多路复用）────────────────────────
    ConnectionOptions async_opts;
    async_opts.host     = host;
    async_opts.port     = port;
    async_opts.password = password;
    async_opts.db       = db;

    ConnectionPoolOptions async_pool_opts;
    async_pool_opts.size = pool_size;  // 全给异步，秒杀吞吐全靠它

    Logger::instance().info("Initializing Redis Pool with size: " + std::to_string(pool_size));

    try {
        async_redis_ = std::make_unique<AsyncRedis>(async_opts, async_pool_opts);
    } catch (const std::exception& e) {
        Logger::instance().error("Redis async client init failed: " + std::string(e.what()));
        return false;
    }

    // ── 预加载 Lua 脚本（同步执行一次，获取 SHA1）──────────────
    if (!loadSeckillScript()) {
        Logger::instance().error("Failed to load seckill Lua script, async seckill will be unavailable");
        // 不强制退出，连接池已建立，sync 操作依然可用
    }

    Logger::instance().info("RedisService initialized: host=" + host +
                            ", pool_size=" + std::to_string(pool_size) +
                            ", seckill_sha=" + (seckill_sha_.empty() ? "UNLOADED" : seckill_sha_.substr(0, 8) + "..."));
    initAllLocalStock();
    return true;
}

// ============================================================
//  预加载 Lua 脚本（同步方式）
// ============================================================
bool RedisService::loadSeckillScript() {
    if (!sync_redis_) return false;

    try {
        // SCRIPT LOAD 返回 SHA1 校验和（40字节十六进制字符串）
        seckill_sha_ = sync_redis_->script_load(SECKILL_LUA_SCRIPT);
        Logger::instance().info("Seckill Lua script loaded, SHA: " + seckill_sha_);
        return true;
    } catch (const Error& e) {
        Logger::instance().error("SCRIPT LOAD failed: " + std::string(e.what()));
        return false;
    }
}

// ============================================================
//  🚀 异步秒杀执行（核心改造）
//
//  网络 IO 完全在 Redis IO 线程执行，HTTP 线程发起调用后
//  立刻返回，绝不阻塞。Redis 返回后触发 callback。
// ============================================================
void RedisService::executeSeckillAsync(const std::string& product_id,
                                       const std::string& user_id,
                                       const std::string& order_data,
                                       int cooldown_seconds,
                                       AsyncCallback callback) {
    if (!async_redis_ || seckill_sha_.empty()) {
        Logger::instance().error("AsyncRedis not initialized or script not loaded");
        if (callback) callback(-1);
        return;
    }

    std::string stock_key     = "seckill:stock:" + product_id;
    std::string order_set_key = "seckill:orders:" + product_id;
    std::string cooldown_key   = "seckill:cooldown:" + user_id + ":" + product_id;
    std::string stream_key    = "seckill:stream:orders";

    std::vector<std::string> keys = {
        stock_key, order_set_key, cooldown_key, stream_key
    };

    // ARGV[1]=user_id, ARGV[2]=order_data, ARGV[3]=cooldown, ARGV[4]=stream_maxlen
    std::vector<std::string> args = {
        user_id,
        order_data,
        std::to_string(cooldown_seconds),
        std::to_string(Config::instance().seckill.stream_maxlen)
    };

    // ── 发起 EVALSHA，传入 callback，发射后立刻返回，不等 Redis ─
    async_redis_->evalsha<long long>(
        seckill_sha_,
        keys.begin(), keys.end(),
        args.begin(), args.end(),
        [cb = std::move(callback)](Future<long long>&& fut) mutable {
            // 这里在 Redis IO 线程执行（不是 HTTP 线程！）
            int result = -1;
            try {
                long long reply = fut.get();   // 已就绪，瞬间返回
                result = static_cast<int>(reply);
            } catch (const Error& e) {
                // NOSCRIPT（Redis 重启脚本被清空）/ 网络断开等
                Logger::instance().error("Async EVALSHA error: " + std::string(e.what()));
                result = -1;
            } catch (const std::exception& e) {
                Logger::instance().error("Async EVALSHA exception: " + std::string(e.what()));
                result = -1;
            } catch (...) {
                result = -1;
            }
            // 触发上层回调（在 Redis IO 线程中调用）
            if (cb) cb(result);
        }
    );
}

// ============================================================
//  同步操作（给 list_products 等低频接口用）
// ============================================================
bool RedisService::isConnected() const {
    return sync_redis_ != nullptr && async_redis_ != nullptr;
}

std::string RedisService::get(const std::string& key) {
    if (!sync_redis_) return "";
    try {
        auto val = sync_redis_->get(key);
        return val ? *val : "";
    } catch (const std::exception& e) {
        Logger::instance().error("Redis GET error: " + std::string(e.what()));
        return "";
    }
}

bool RedisService::set(const std::string& key, const std::string& value) {
    if (!sync_redis_) return false;
    try {
        sync_redis_->set(key, value);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Redis SET error: " + std::string(e.what()));
        return false;
    }
}

bool RedisService::setex(const std::string& key, int expire_seconds, const std::string& value) {
    if (!sync_redis_) return false;
    try {
        sync_redis_->setex(key, expire_seconds, value);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Redis SETEX error: " + std::string(e.what()));
        return false;
    }
}

bool RedisService::exists(const std::string& key) {
    if (!sync_redis_) return false;
    try {
        return sync_redis_->exists(key);
    } catch (const std::exception& e) {
        Logger::instance().error("Redis EXISTS error: " + std::string(e.what()));
        return false;
    }
}

bool RedisService::del(const std::string& key) {
    if (!sync_redis_) return false;
    try {
        return sync_redis_->del(key) > 0;
    } catch (const std::exception& e) {
        Logger::instance().error("Redis DEL error: " + std::string(e.what()));
        return false;
    }
}

bool RedisService::incrby(const std::string& key, int64_t increment) {
    if (!sync_redis_) return false;
    try {
        sync_redis_->incrby(key, increment);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Redis INCRBY error: " + std::string(e.what()));
        return false;
    }
}

bool RedisService::srem(const std::string& key, const std::string& member) {
    if (!sync_redis_) return false;
    try {
        sync_redis_->srem(key, member);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Redis SREM error: " + std::string(e.what()));
        return false;
    }
}

// ============================================================
//  关闭
// ============================================================
void RedisService::close() {
    if (async_redis_) {
        async_redis_.reset();
        Logger::instance().info("AsyncRedis closed");
    }
    if (sync_redis_) {
        sync_redis_.reset();
        Logger::instance().info("Redis sync client closed");
    }
}

// ============================================================
//  本地预扣减实现（v2 新增）
// ============================================================

void RedisService::initLocalStock(const std::string& product_id) {
    std::string stock_key = Config::instance().seckill.stock_prefix + product_id;
    if (!sync_redis_) {
        Logger::instance().warn("initLocalStock: sync_redis_ is null, skipping " + product_id);
        return;
    }
    try {
        auto val = sync_redis_->get(stock_key);
        int64_t stock = 0;
        if (val && !val->empty()) {
            try {
                stock = std::stoll(val.value());
            } catch (...) {
                stock = 0;
            }
        }
        std::unique_lock<std::shared_mutex> lock(local_stock_mutex_);
        local_stock_map_[product_id] = std::make_unique<std::atomic<int64_t>>(stock);
        Logger::instance().info("initLocalStock: " + product_id + " = " + std::to_string(stock));
    } catch (const std::exception& e) {
        Logger::instance().warn("initLocalStock failed for " + product_id + ": " + e.what());
    }
}

void RedisService::initAllLocalStock() {
    try {
        auto products = MySQLService::instance().get_all_products();
        for (const auto& p : products) {
            initLocalStock(p.id);
        }
    } catch (const std::exception& e) {
        Logger::instance().warn("initAllLocalStock failed: " + std::string(e.what()));
    }
    // 用单独作用域限制锁的生命周期，日志输出后立即释放
    {
        std::shared_lock<std::shared_mutex> lock(local_stock_mutex_);
        Logger::instance().info("initAllLocalStock done, total " + std::to_string(local_stock_map_.size()) + " products");
    }
}

bool RedisService::tryLocalPreDeduct(const std::string& product_id) {
    // 快路径：shared_lock 查找 + atomic fetch_sub
    {
        std::shared_lock<std::shared_mutex> lock(local_stock_mutex_);
        auto it = local_stock_map_.find(product_id);
        if (it != local_stock_map_.end()) {
            int64_t old_val = it->second->fetch_sub(1);
            if (old_val <= 0) {
                it->second->fetch_add(1);
                return false;
            }
            return true;
        }
    }
    // 不在 map 中 → 惰性初始化，然后重试一次
    initLocalStock(product_id);
    {
        std::shared_lock<std::shared_mutex> lock(local_stock_mutex_);
        auto it = local_stock_map_.find(product_id);
        if (it != local_stock_map_.end()) {
            int64_t old_val = it->second->fetch_sub(1);
            if (old_val <= 0) {
                it->second->fetch_add(1);
                return false;
            }
            return true;
        }
    }
    // 连 Redis 都读不到（异常），放行让 Redis 处理
    return true;
}

void RedisService::rollbackLocalPreDeduct(const std::string& product_id) {
    std::shared_lock<std::shared_mutex> lock(local_stock_mutex_);
    auto it = local_stock_map_.find(product_id);
    if (it != local_stock_map_.end()) {
        it->second->fetch_add(1);
    }
}

// ============================================================
//  动态 Token 实现（防脚本直刷）
//
//  Token 生成规则：MD5(user_id + product_id + salt + timestamp)
//  Token 存储：Redis key = "seckill:token:{user_id}:{product_id}"
//  Token 有效期：可配置，默认 30 秒
// ============================================================

// MD5 计算辅助函数
static std::string md5_hex(const std::string& input) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), digest);
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return oss.str();
}

std::string RedisService::generateSeckillToken(const std::string& user_id,
                                               const std::string& product_id) {
    if (!sync_redis_) return "";

    auto& cfg = Config::instance();
    long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 生成 Token：MD5(user_id + product_id + salt + timestamp)
    std::string raw = user_id + product_id + cfg.security.token_salt + std::to_string(timestamp);
    std::string token = md5_hex(raw);

    // 存入 Redis，设置过期时间
    std::string key = "seckill:token:" + user_id + ":" + product_id;
    sync_redis_->setex(key, cfg.security.token_expire_seconds, token);

    Logger::instance().info("Generated token for user=" + user_id + ", product=" + product_id);

    return token;
}

bool RedisService::verifySeckillToken(const std::string& token,
                                       const std::string& user_id,
                                       const std::string& product_id) {
    if (!sync_redis_) return false;

    // 查 Redis 中的 Token
    std::string key = "seckill:token:" + user_id + ":" + product_id;
    auto stored = sync_redis_->get(key);

    if (!stored || stored->empty()) {
        Logger::instance().warn("Token not found for user=" + user_id + ", product=" + product_id);
        return false;
    }

    // 比较 Token（使用 timing-safe 比较防止侧信道攻击）
    if (token.size() != stored->size()) {
        return false;
    }

    // 简单的恒定时间比较
    unsigned char diff = 0;
    for (size_t i = 0; i < token.size(); ++i) {
        diff |= token[i] ^ (*stored)[i];
    }

    if (diff != 0) {
        Logger::instance().warn("Token mismatch for user=" + user_id + ", product=" + product_id);
        return false;
    }

    // Token 验证成功，删除 Token（一次性使用）
    sync_redis_->del(key);

    return true;
}

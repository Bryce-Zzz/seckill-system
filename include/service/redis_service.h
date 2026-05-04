#pragma once
// ============================================================
//  RedisService -- 基于 sw::redis++ AsyncRedis 的纯异步秒杀服务
//
//  架构演进：
//    旧版：手动连接池 + RedisConnGuard RAII + 同步 EVALSHA
//    新版：sw::redis++ AsyncRedis 自动管理连接池 + 回调事件驱动
//
//  sw::redis++ AsyncRedis 优势：
//    - 连接池内部管理，无需手动 get/return
//    - 纯异步非阻塞，网络 IO 不占用 HTTP 线程
//    - 多路复用，单连接可并发执行多个命令
//    - 基于 libuv 事件循环（无需额外线程池）
//
//  本地预扣减（v2 新增）：
//    在 C++ 进程内维护 per-product 的 atomic<int64_t> 计数器，
//    HTTP 线程在发起 Redis 异步调用前先尝试 fetch_sub(1)，
//    若扣减前值 <= 0 则直接返回售罄，不打 Redis，
//    从而大幅降低 Redis 压力（截流效果可达 80%+）。
//    若 Redis 回调结果为失败，则 fetch_add(1) 回滚本地计数。
// ============================================================

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <shared_mutex>

// sw::redis++ 异步核心头文件（包含 AsyncRedis + Future + Callback API）
#include <sw/redis++/redis++.h>
#include <sw/redis++/async_redis++.h>

class RedisService {
public:
    static RedisService& instance() {
        static RedisService inst;
        return inst;
    }

    // ---- 初始化 ----
    // host/port/password 由 Config 单例注入，pool_size 控制异步连接池大小
    bool init(const std::string& host, int port,
              const std::string& password = "",
              int db = 0, int pool_size = 100);

    // ---- 异步秒杀（核心接口）----
    // callback 在 Redis IO 线程触发，绝不阻塞 HTTP 线程
    // 返回值含义：1=成功, 0=库存不足, -2=重复购买, -3=冷却中, -1=系统错误
    using AsyncCallback = std::function<void(int result)>;
    void executeSeckillAsync(const std::string& product_id,
                              const std::string& user_id,
                              const std::string& order_data,
                              int cooldown_seconds,
                              AsyncCallback callback);

    // ---- 同步操作（给 list_products 等低频接口用）----
    std::string get(const std::string& key);
    bool set(const std::string& key, const std::string& value);
    bool setex(const std::string& key, int expire_seconds, const std::string& value);
    bool exists(const std::string& key);
    bool del(const std::string& key);
    bool incrby(const std::string& key, int64_t increment);
    bool srem(const std::string& key, const std::string& member);

    // ---- 本地预扣减（v2 新增）----
    // 初始化/刷新某个商品的本地库存（从 Redis 读取最新值）
    void initLocalStock(const std::string& product_id);
    // 初始化所有在 Redis 中存在的秒杀商品本地库存
    void initAllLocalStock();
    // 尝试本地预扣减 1 件：返回值 true = 允许继续打 Redis
    bool tryLocalPreDeduct(const std::string& product_id);
    // 回滚本地预扣减（Redis 返回失败时调用）
    void rollbackLocalPreDeduct(const std::string& product_id);

    // ---- 动态 Token（防脚本直刷）----
    // 生成秒杀 Token：MD5(user_id + product_id + salt + timestamp) 存入 Redis
    std::string generateSeckillToken(const std::string& user_id, const std::string& product_id);
    // 验证 Token：检查 Redis 中是否存在且匹配，返回 true = 有效
    bool verifySeckillToken(const std::string& token, const std::string& user_id, const std::string& product_id);

    // ---- 运行状态检查 ----
    bool isConnected() const;

    // ---- 优雅关闭 ----
    void close();

private:
    RedisService()  = default;
    ~RedisService() { close(); }

    // 禁止拷贝
    RedisService(const RedisService&)            = delete;
    RedisService& operator=(const RedisService&) = delete;

    // 预加载 Lua 脚本（同步执行一次，获取 SHA1）
    bool loadSeckillScript();

    // 同步客户端（给 get/set 等低频操作用）
    std::unique_ptr<sw::redis::Redis> sync_redis_;

    // 全异步客户端（秒杀专用，多路复用，事件驱动）
    std::unique_ptr<sw::redis::AsyncRedis> async_redis_;

    // 预加载的秒杀 Lua 脚本 SHA1 哈希值（40 字节）
    std::string seckill_sha_;

    // 配置快照（用于参考）
    std::string host_;
    int         port_ = 0;
    std::string password_;
    int         db_   = 0;

    // ---- 本地预扣减：per-product atomic 计数器 ----
    // 使用 unique_ptr<atomic<int64_t>> 包装，使 map 元素可移动（C++17 兼容）
    // 读路径（tryLocalPreDeduct）用 shared_lock 查找 + atomic fetch_sub
    // 写路径（initLocalStock）用 unique_lock 写入
    std::unordered_map<std::string,
                         std::unique_ptr<std::atomic<int64_t>>> local_stock_map_;
    mutable std::shared_mutex                         local_stock_mutex_;

    // Lua 脚本内容（eval 前先 script load 获取 sha）
    static constexpr const char* SECKILL_LUA_SCRIPT = R"(
        -- KEYS[1]=stock_key, KEYS[2]=order_set_key
        -- KEYS[3]=cooldown_key, KEYS[4]=stream_key
        -- ARGV[1]=user_id, ARGV[2]=order_data
        -- ARGV[3]=cooldown_seconds, ARGV[4]=stream_maxlen
        if redis.call('exists', KEYS[3]) == 1 then
            return -3
        end
        if tonumber(ARGV[3]) > 0 then
            redis.call('setex', KEYS[3], ARGV[3], 1)
        end
        if redis.call('sismember', KEYS[2], ARGV[1]) == 1 then
            return -2
        end
        local stock = tonumber(redis.call('get', KEYS[1]) or 0)
        if stock <= 0 then
            return 0
        end
        redis.call('decr', KEYS[1])
        redis.call('sadd', KEYS[2], ARGV[1])
        redis.call('XADD', KEYS[4], 'MAXLEN', '~', ARGV[4], '*', 'order_data', ARGV[2])
        return 1
    )";
};

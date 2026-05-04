#pragma once
// ============================================================
//  AsyncRedisExecutor — 零依赖异步秒杀执行器
//
//  设计目标：
//    HTTP 线程提交任务后 **立即返回**，不阻塞等待 Redis 响应。
//    Redis 操作在后台线程池中执行，结果通过 std::function 回调
//    传回（由调用方决定用 SSE 推送给对应用户）。
//
//  架构：
//    ┌────────────┐  submit()  ┌──────────────────────────────┐
//    │ HTTP 线程   ├──────────►│   task_queue_ (mpmc lock)    │
//    │ （不阻塞）  │           └────────────┬─────────────────┘
//    └────────────┘                        │ worker_loop()
//                                  ┌───────▼────────┐
//                                  │  线程池（N线程） │
//                                  │  RedisService   │
//                                  │  EVALSHA / Lua  │
//                                  └───────┬─────────┘
//                                          │ callback(result)
//                                  ┌───────▼──────────┐
//                                  │ SseHandler 单播推送│
//                                  └──────────────────┘
// ============================================================

#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <cstddef>

// ============================================================
//  任务描述符
// ============================================================
struct SeckillTask {
    std::string product_id;
    std::string user_id;
    std::string order_data;     // "order_id|user_id|product_id|qty|ts"
    std::string order_id;       // 用于回调时识别本次订单
    int         cooldown_seconds = 0;

    // 执行完毕后被调用：
    //   result:  1=成功, 0=售罄, -1=Redis错误, -2=已购买, -3=冷却中
    //   order_id / user_id 原样透传，方便 SSE 单播定位
    std::function<void(int result,
                       const std::string& order_id,
                       const std::string& user_id)> callback;
};

// ============================================================
//  AsyncRedisExecutor — 单例线程池
// ============================================================
class AsyncRedisExecutor {
public:
    static AsyncRedisExecutor& instance();

    // 初始化（必须在 RedisService::init() 之后调用）
    // thread_count 建议 = Redis pool_size / 2 ~ pool_size
    void init(int thread_count = 16);

    // 优雅关闭，等待队列清空后停止所有工作线程
    void shutdown();

    // 提交秒杀任务（HTTP 线程调用，O(1) 非阻塞）
    // 返回 false 表示队列已满，调用方应直接回 503
    bool submit(SeckillTask task);

    // 运行时指标
    size_t pending_tasks() const;
    int    worker_count()  const;

    // 设置最大排队深度（超过则拒绝新任务，默认 100000）
    void set_max_queue_depth(size_t depth);

    ~AsyncRedisExecutor();

private:
    AsyncRedisExecutor()  = default;

    // 禁止拷贝
    AsyncRedisExecutor(const AsyncRedisExecutor&)            = delete;
    AsyncRedisExecutor& operator=(const AsyncRedisExecutor&) = delete;

    void worker_loop();
    void execute_task(const SeckillTask& task);

    std::vector<std::thread>    workers_;
    std::queue<SeckillTask>     task_queue_;
    mutable std::mutex          queue_mutex_;
    std::condition_variable     queue_cv_;
    std::atomic<bool>           running_{false};
    size_t                      max_queue_depth_ = 100000;
};

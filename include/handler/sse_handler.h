#ifndef SECKILL_SSE_HANDLER_H
#define SECKILL_SSE_HANDLER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

// SSE 事件消息
struct SSEEvent {
    std::string event_type;
    std::string data;
    int64_t timestamp;
    
    SSEEvent(const std::string& type, const std::string& d)
        : event_type(type), data(d), 
          timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()) {}
};

// SSE Handler 类
class SSEHandler {
public:
    static SSEHandler& instance();
    
    bool start(int port = 9001);
    void stop();
    
    // 【核心改造1】单播：仅推送给指定用户（支持多端）
    void sendToUser(const std::string& user_id, 
                    const std::string& event_type, 
                    const std::string& data);
    
    // 【保留】全局广播：仅限秒杀开始/结束等全局事件
    void broadcast(const std::string& event_type, const std::string& data);
    
    // 【核心改造2】客户端连接/断开（由 HTTP server 调用）
    // parse_query_url 从 HTTP 请求 URL 中解析 user_id
    static std::string parse_query_url(const char* url);
    void addClient(const std::string& user_id, int fd);
    void removeClient(int fd);
    
    size_t active_connections() const;
    static std::string make_sse_message(const std::string& event_type, 
                                       const std::string& data);

private:
    SSEHandler();
    ~SSEHandler();
    
    SSEHandler(const SSEHandler&) = delete;
    SSEHandler& operator=(const SSEHandler&) = delete;
    
    void server_loop();
    void accept_connections();
    void send_to_all_clients(const std::string& msg);
    void process_event_queue();
    
    static void setNonBlocking(int fd);

    int server_fd_;
    int port_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    
    // 【核心改造3】Unicast 数据结构：user_id → 所有 socket fd（支持多端登录）
    std::unordered_map<std::string, std::unordered_set<int>> user_fds_;
    // 反向索引：fd → user_id（用于优雅断开时快速查找）
    std::unordered_map<int, std::string> fd_to_user_;
    mutable std::mutex mtx_;
    
    // 通用事件队列（由 server_loop 消费，驱动 sendToUser / broadcast）
    std::queue<SSEEvent> event_queue_;
    std::mutex queue_mutex_;
    
    // keepalive 相关
    std::chrono::steady_clock::time_point last_keepalive_;
    static constexpr int KEEPALIVE_INTERVAL_SEC = 15;  // 【核心改造4】50ms → 15s
};

#endif

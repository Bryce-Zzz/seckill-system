#include "handler/sse_handler.h"
#include "common/logger.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <thread>

// ==================== 辅助函数 ====================

std::string SSEHandler::make_sse_message(const std::string& event_type, 
                                         const std::string& data) {
    std::string msg = "event: ";
    msg += event_type;
    msg += "\ndata: ";
    msg += data;
    msg += "\n\n";
    return msg;
}

void SSEHandler::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 从 HTTP URL query string 中解析 user_id
// 例如：url="/sse?user_id=u123" → 返回 "u123"
std::string SSEHandler::parse_query_url(const char* url) {
    if (!url) return "";
    std::string s(url);
    auto pos = s.find("user_id=");
    if (pos == std::string::npos) return "";
    pos += 8; // skip "user_id="
    auto end = s.find('&', pos);
    if (end == std::string::npos) end = s.size();
    return s.substr(pos, end - pos);
}

// ==================== 单例与生命周期 ====================

SSEHandler& SSEHandler::instance() {
    static SSEHandler instance_;
    return instance_;
}

SSEHandler::SSEHandler() 
    : server_fd_(-1), port_(9001), running_(false),
      last_keepalive_(std::chrono::steady_clock::now()) {
}

SSEHandler::~SSEHandler() {
    stop();
}

bool SSEHandler::start(int port) {
    if (running_.load()) {
        Logger::instance().warn("SSE server already running");
        return true;
    }
    
    port_ = port;
    
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        Logger::instance().error("Failed to create SSE socket: " + std::string(strerror(errno)));
        return false;
    }
    
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::instance().error("Failed to bind SSE socket: " + std::string(strerror(errno)));
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    if (::listen(server_fd_, 128) < 0) {  // backlog 从 64 提升到 128
        Logger::instance().error("Failed to listen SSE socket: " + std::string(strerror(errno)));
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    // 服务器监听 socket 也设为非阻塞
    setNonBlocking(server_fd_);
    
    running_ = true;
    last_keepalive_ = std::chrono::steady_clock::now();
    server_thread_ = std::thread(&SSEHandler::server_loop, this);
    
    Logger::instance().info("SSE server started on port " + std::to_string(port_));
    return true;
}

void SSEHandler::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    
    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& kv : user_fds_) {
            for (int fd : kv.second) {
                ::close(fd);
            }
        }
        user_fds_.clear();
        fd_to_user_.clear();
    }
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    Logger::instance().info("SSE server stopped");
}

// ==================== 客户端管理 ====================

void SSEHandler::addClient(const std::string& user_id, int fd) {
    // 设置为非阻塞，防止后续 send 卡死
    setNonBlocking(fd);
    
    std::lock_guard<std::mutex> lock(mtx_);
    user_fds_[user_id].insert(fd);
    fd_to_user_[fd] = user_id;
    
    size_t total = 0;
    for (const auto& kv : user_fds_) total += kv.second.size();
    Logger::instance().info("SSE client connected: user_id=" + user_id + 
                           ", total_fds=" + std::to_string(total));
}

void SSEHandler::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = fd_to_user_.find(fd);
    if (it != fd_to_user_.end()) {
        std::string uid = it->second;
        auto ufit = user_fds_.find(uid);
        if (ufit != user_fds_.end()) {
            ufit->second.erase(fd);
            if (ufit->second.empty()) {
                user_fds_.erase(ufit);
            }
        }
        fd_to_user_.erase(it);
        ::close(fd);
        
        size_t total = 0;
        for (const auto& kv : user_fds_) total += kv.second.size();
        Logger::instance().info("SSE client disconnected: user_id=" + uid + 
                               ", remaining_fds=" + std::to_string(total));
    } else {
        ::close(fd);
    }
}

// ==================== 核心 TCP 循环 ====================

void SSEHandler::server_loop() {
    while (running_.load()) {
        // 1. 尝试 accept 新连接
        accept_connections();
        
        // 2. 处理事件队列（单播 + 全局广播）
        process_event_queue();
        
        // 3. Keep-alive（每 15 秒一次，不再每 50ms）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_keepalive_).count();
        if (elapsed >= KEEPALIVE_INTERVAL_SEC) {
            last_keepalive_ = now;
            std::string keepalive = ": keepalive\n\n";
            send_to_all_clients(keepalive);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void SSEHandler::accept_connections() {
    if (server_fd_ < 0) return;
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Silent for non-blocking accept
        }
        return;
    }
    
    int nodelay = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    // ★ 核心修复：先 recv HTTP 请求，解析出真实 user_id，再发响应
    char buf[1024] = {0};
    ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
    
    std::string user_id = "anon_" + std::to_string(client_fd);
    if (n > 0) {
        buf[n] = '\0';
        std::string request(buf);
        
        // 从请求行提取 URL，例如 "GET /sse?user_id=U002 HTTP/1.1"
        auto space1 = request.find(' ');
        auto space2 = request.find(' ', space1 + 1);
        if (space1 != std::string::npos && space2 != std::string::npos) {
            std::string url = request.substr(space1 + 1, space2 - space1 - 1);
            std::string parsed = parse_query_url(url.c_str());
            if (!parsed.empty()) {
                user_id = parsed;
            }
        }
        
        Logger::instance().info("SSE HTTP request from fd=" + std::to_string(client_fd) + 
                               ", parsed user_id=[" + user_id + "], raw_url=[" + 
                               std::string(buf, std::min((size_t)n, (size_t)200)) + "]");
    } else {
        // recv 失败（对方关闭或异常），直接关闭
        ::close(client_fd);
        return;
    }
    
    const char* response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    
    ssize_t sent = ::send(client_fd, response, strlen(response), 0);
    if (sent > 0) {
        // 发 connected 事件
        std::string connected = make_sse_message("connected", 
            "{\"status\":\"connected\",\"user_id\":\"" + user_id + "\"}");
        ::send(client_fd, connected.c_str(), connected.size(), MSG_NOSIGNAL);
        
        // 加入管理（addClient 里会设 non-blocking）
        addClient(user_id, client_fd);
    } else {
        ::close(client_fd);
    }
}

void SSEHandler::process_event_queue() {
    std::queue<SSEEvent> local_queue;
    
    // 一次性把队列里的事件全部取出，释放锁
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (event_queue_.empty()) return;
        local_queue = std::move(event_queue_);
        // 注意：move 后 event_queue_ 已被清空
    }
    
    // 逐个处理事件
    while (!local_queue.empty()) {
        SSEEvent event = std::move(local_queue.front());
        local_queue.pop();
        
        std::string msg = make_sse_message(event.event_type, event.data);
        
        // 如果 event.data 中包含 user_id，执行单播
        // 约定格式：data 为 JSON 字符串
        std::string target_user;
        try {
            // 简单检测：如果 JSON 中有 "user_id" 字段，尝试提取
            std::string data_str = event.data;
            auto pos = data_str.find("\"user_id\"");
            if (pos != std::string::npos) {
                auto colon = data_str.find(':', pos);
                auto start = data_str.find('"', colon) + 1;
                auto end = data_str.find('"', start);
                if (colon != std::string::npos && start != std::string::npos && end != std::string::npos && end > start) {
                    target_user = data_str.substr(start, end - start);
                }
            }
        } catch (...) {
            // 解析失败，当作全局广播处理
        }
        
        if (!target_user.empty()) {
            // 【核心改造】单播：精准推送给指定用户
            sendToUser(target_user, event.event_type, event.data);
        } else {
            // 全局广播：seckill_started / seckill_stopped 等全局事件
            send_to_all_clients(msg);
        }
    }
}

// 遍历所有客户端发送（全局广播）
void SSEHandler::send_to_all_clients(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (user_fds_.empty()) return;
    
    std::vector<int> dead_fds;
    
    for (auto& kv : user_fds_) {
        for (int fd : kv.second) {
            ssize_t n = ::send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 缓冲区满，网络慢，优雅跳过（SSE 允许丢心跳）
                    continue;
                } else {
                    dead_fds.push_back(fd);
                }
            }
        }
    }
    
    // 清理死连接
    for (int fd : dead_fds) {
        auto it = fd_to_user_.find(fd);
        if (it != fd_to_user_.end()) {
            std::string uid = it->second;
            fd_to_user_.erase(it);
            auto uit = user_fds_.find(uid);
            if (uit != user_fds_.end()) {
                uit->second.erase(fd);
                if (uit->second.empty()) user_fds_.erase(uit);
            }
        }
        ::close(fd);
    }
}

// ==================== 公共 API ====================

// 【核心改造】单播：精准推送给指定用户（支持多设备）
void SSEHandler::sendToUser(const std::string& user_id, 
                            const std::string& event_type, 
                            const std::string& data) {
    // ★ 调试日志：追踪每一次单播
    Logger::instance().info("SSE sendToUser called: user_id=[" + user_id + 
                           "], event=" + event_type + ", data=" + data);
    
    std::string msg = make_sse_message(event_type, data);
    
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = user_fds_.find(user_id);
    if (it == user_fds_.end()) {
        // 用户不在线，直接丢弃（不报错，SSE 允许丢单播消息）
        return;
    }
    
    std::vector<int> dead_fds;
    
    for (int fd : it->second) {
        // 【核心改造】非阻塞 + EAGAIN 处理，防止一条慢鱼卡死整个循环
        ssize_t n = ::send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // TCP 发送缓冲区满了，网络慢，不阻塞跳过
                // 后续消息会继续尝试，SSE 不保证顺序，前端可重连拉取最新状态
                continue;
            } else {
                // EPIPE / ECONNRESET：客户端断开
                dead_fds.push_back(fd);
            }
        }
    }
    
    // 清理死掉的 fd
    for (int fd : dead_fds) {
        it->second.erase(fd);
        fd_to_user_.erase(fd);
        ::close(fd);
    }
    if (it->second.empty()) {
        user_fds_.erase(it);
    }
}

// 广播接口：事件入队，由 server_loop 消费
void SSEHandler::broadcast(const std::string& event_type, const std::string& data) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    event_queue_.emplace(event_type, data);
}

size_t SSEHandler::active_connections() const {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t total = 0;
    for (const auto& kv : user_fds_) {
        total += kv.second.size();
    }
    return total;
}

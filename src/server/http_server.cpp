#include "server/http_server.h"
#include "common/logger.h"
#include "handler/seckill_handler.h"
#include "handler/order_handler.h"
#include "handler/admin_handler.h"
#include "handler/health_handler.h"
#include "handler/metrics_handler.h"
#include <sstream>
#include <fstream>
#include <sstream>
#include <climits>

// 静态文件服务
static int handle_static_files(HttpRequest* req, HttpResponse* resp) {
    std::string req_path = req->path;
    
    // 根据路径前缀决定web目录
    std::string web_prefix;
    std::string file_path;
    
    if (req_path == "/admin" || req_path.rfind("/admin/", 0) == 0) {
        web_prefix = "web/admin";
        file_path = req_path.substr(6);
    } else if (req_path == "/seckill" || req_path.rfind("/seckill/", 0) == 0) {
        web_prefix = "web/seckill";
        file_path = req_path.substr(8);
    } else {
        web_prefix = "web/seckill";
        file_path = req_path;
    }
    
    if (!file_path.empty() && file_path[0] == '/') {
        file_path = file_path.substr(1);
    }
    
    if (file_path.empty()) {
        file_path = "index.html";
    }
    
    // 组合完整路径
    std::string full_path = "/home/dongbai/seckill-system/" + web_prefix + "/" + file_path;
    
    Logger::instance().info("Static file: req_path=" + req_path + ", full_path=" + full_path);
    
    // 读取文件
    std::ifstream file(full_path, std::ios::binary);
    if (!file) {
        Logger::instance().error("Failed to open: " + full_path);
        resp->body = "404 Not Found: " + full_path;
        return 404;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    resp->body = buffer.str();
    
    if (full_path.find(".html") != std::string::npos) {
        resp->SetContentType("text/html");
    } else if (full_path.find(".css") != std::string::npos) {
        resp->SetContentType("text/css");
    } else if (full_path.find(".js") != std::string::npos) {
        resp->SetContentType("application/javascript");
    } else if (full_path.find(".json") != std::string::npos) {
        resp->SetContentType("application/json");
    } else if (full_path.find(".png") != std::string::npos) {
        resp->SetContentType("image/png");
    } else if (full_path.find(".jpg") != std::string::npos || full_path.find(".jpeg") != std::string::npos) {
        resp->SetContentType("image/jpeg");
    }
    
    return 200;
}

bool SeckillHttpServer::init(int port, int worker_threads) {
    port_ = port;
    
    // 自动检测 CPU 核心数
    std::cerr << "[DEBUG] HTTP server init: port=" << port << ", worker_threads=" << worker_threads << std::endl;
    
    if (worker_threads <= 0) {
        worker_threads_ = std::thread::hardware_concurrency();
        if (worker_threads_ == 0) worker_threads_ = 4;  // fallback
        std::cerr << "[DEBUG] Auto-detected " << worker_threads_ << " CPU cores" << std::endl;
        Logger::instance().info("Auto-detected " + std::to_string(worker_threads_) + " CPU cores");
    } else {
        worker_threads_ = worker_threads;
        std::cerr << "[DEBUG] Using configured worker_threads=" << worker_threads_ << std::endl;
    }
    
    server_.setThreadNum(worker_threads_);

    register_seckill_routes(&service_);
    register_order_routes(&service_);
    register_admin_routes(&service_);
    register_health_routes(&service_);
    register_metrics_routes(&service_);

    service_.GET("/admin", handle_static_files);
    service_.GET("/admin/*", handle_static_files);
    service_.GET("/seckill", handle_static_files);
    service_.GET("/seckill/*", handle_static_files);

    service_.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        (void)req;
        resp->body = "pong from seckill system";
        return 200;
    });

    Logger::instance().info("HTTP server initialized on port " + std::to_string(port_));
    return true;
}

void SeckillHttpServer::start() {
    Logger::instance().info("HTTP server starting on port " + std::to_string(port_) + "...");
    std::ostringstream oss;
    oss << ":" << port_;
    server_.run(oss.str().c_str(), false);
    Logger::instance().info("HTTP server started");
}

void SeckillHttpServer::stop() {
    Logger::instance().info("HTTP server stopping...");
    server_.stop();
    Logger::instance().info("HTTP server stopped");
}

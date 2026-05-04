#pragma once

#include "common/logger.h"
#include "common/config.h"
#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <thread>

class SeckillHttpServer {
public:
    static SeckillHttpServer& instance() {
        static SeckillHttpServer inst;
        return inst;
    }

    bool init(int port, int worker_threads);
    void start();
    void stop();

private:
    SeckillHttpServer() : server_(&service_) {}  // 构造时传入 service
    ~SeckillHttpServer() = default;
    SeckillHttpServer(const SeckillHttpServer&) = delete;
    SeckillHttpServer& operator=(const SeckillHttpServer&) = delete;

    int port_ = 8080;
    int worker_threads_ = 4;
    hv::HttpService service_;   // HTTP服务（注册路由）
    hv::HttpServer server_;     // HTTP服务器（包装service）
};

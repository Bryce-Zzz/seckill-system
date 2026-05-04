#pragma once

#include <hv/HttpService.h>

// ── 秒杀相关路由 ────────────────────────────────────────────────
// 所有 handler 使用 HttpContextPtr 异步模式：
//   函数立刻返回 0，响应在 Redis 回调中通过 ctx->send() 发出
void register_seckill_routes(hv::HttpService* service);

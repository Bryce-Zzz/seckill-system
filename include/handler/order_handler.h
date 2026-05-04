#pragma once

#include <hv/HttpService.h>

void register_seckill_routes(hv::HttpService* service);
void register_order_routes(hv::HttpService* service);
void register_admin_routes(hv::HttpService* service);

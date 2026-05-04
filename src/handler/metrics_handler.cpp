#include "handler/metrics_handler.h"
#include "common/metrics.h"

static int handle_metrics(HttpRequest* req, HttpResponse* resp) {
    (void)req;
    resp->body = Metrics::instance().dump();
    resp->SetContentType("text/plain; version=0.0.4; charset=utf-8");
    return 200;
}

void register_metrics_routes(hv::HttpService* service) {
    service->GET("/metrics", handle_metrics);
}

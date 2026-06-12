#include <cstring>

#include "hal/http_server.h"

HttpServer::HttpServer(uint16_t /*port*/) {}

void HttpServer::on(const char* uri, int method, HttpHandler handler) {
    if (routeCount_ >= cfg::net::kMaxHttpRoutes || uri == nullptr) {
        return;
    }
    Route& r = routes_[routeCount_++];
    std::strncpy(r.uri, uri, sizeof(r.uri) - 1U);
    r.uri[sizeof(r.uri) - 1U] = '\0';
    r.method                  = method;
    r.handler                 = handler;
}

Result<void> HttpServer::begin() {
    return Result<void>::ok();
}

void HttpServer::handleClient() {}

bool HttpServer::dispatchRequest(const char* uri, int method) {
    for (uint8_t i = 0; i < routeCount_; ++i) {
        if (std::strcmp(routes_[i].uri, uri) == 0 && routes_[i].method == method) {
            if (routes_[i].handler != nullptr) {
                routes_[i].handler();
            }
            return true;
        }
    }
    return false;
}

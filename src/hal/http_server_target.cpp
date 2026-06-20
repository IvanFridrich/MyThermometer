#include "hal/http_server.h"

#include <Arduino.h>
#include <WebServer.h>

// Plain-HTTP server over the Arduino WebServer (per project decision: WebServer,
// not esp_http_server). LAN-trusted, no auth (D12/FR-22). Runs on Core 0; the
// app task must call handleClient() regularly. JSON/page handlers are registered
// by the app layer via on().

HttpServer::HttpServer(uint16_t port) : server_(port) {}

void HttpServer::on(const char* uri, int method, HttpHandler handler) {
    if (uri == nullptr || handler == nullptr) {
        return;
    }
    // HAL convention: 0 = GET, 1 = POST. Map explicitly — do not cast int to the
    // HTTPMethod enum, whose underlying values differ (HTTP_HEAD sits at 1).
    const HTTPMethod m = (method == 0) ? HTTP_GET : HTTP_POST;
    server_.on(uri, m, handler);
}

Result<void> HttpServer::begin() {
    server_.begin();
    return Result<void>::ok();
}

void HttpServer::handleClient() {
    server_.handleClient();
}

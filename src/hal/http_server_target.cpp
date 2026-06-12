// Phase 4 will replace this stub with the Arduino WebServer driver.
#include "hal/http_server.h"

static uint16_t g_port;
HttpServer::HttpServer(uint16_t port) {
    g_port = port;
}

void         HttpServer::on(const char* /*uri*/, int /*method*/, HttpHandler /*handler*/) {}
Result<void> HttpServer::begin() {
    return Result<void>::ok();
}
void HttpServer::handleClient() {}

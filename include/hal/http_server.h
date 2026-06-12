#pragma once
#include <cstdint>

#include "Config.h"
#include "result.h"

// Function-pointer type for HTTP request handlers.
using HttpHandler = void (*)();

// HAL: HTTP server (plain, LAN-trusted — no auth per D12).
// Target: Arduino WebServer (Phase 4).  Fake: stores registered handlers.
class HttpServer {
  public:
    explicit HttpServer(uint16_t port);

    // Register a handler for an exact URI path and HTTP method (HTTP_GET = 0, HTTP_POST = 1).
    void         on(const char* uri, int method, HttpHandler handler);
    Result<void> begin();
    void         handleClient();

#ifdef NATIVE_BUILD
    // Test helpers
    struct Route {
        char        uri[64]{};
        int         method{0};
        HttpHandler handler{nullptr};
    };

    bool         running() const { return running_; }
    uint8_t      routeCount() const { return routeCount_; }
    const Route& route(uint8_t i) const { return routes_[i]; }

    // Simulate an incoming request (invokes the matching handler if found).
    bool dispatchRequest(const char* uri, int method);

  private:
    bool    running_{false};
    Route   routes_[cfg::net::kMaxHttpRoutes]{};
    uint8_t routeCount_{0};
#endif
};

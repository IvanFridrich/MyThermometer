// Phase 5 will replace this stub with the ESP-Mail-Client SMTP driver.
#include "hal/mailer.h"

// WARNING: g_* store the caller's pointers — each must be a string literal or
// have static storage duration that outlives this object.
static const char* g_host;
static uint16_t    g_port;
static const char* g_user;
static const char* g_password;
static const char* g_from;
Mailer::Mailer(const char* host, uint16_t port, const char* user, const char* password,
               const char* from) {
    g_host     = host;
    g_port     = port;
    g_user     = user;
    g_password = password;
    g_from     = from;
}

Result<void> Mailer::send(const char* /*to*/, const char* /*subject*/, const char* /*body*/) {
    return Result<void>::err(Status::kNotReady);
}

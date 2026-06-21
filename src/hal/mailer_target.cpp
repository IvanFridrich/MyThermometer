#include "hal/mailer.h"

#include <Arduino.h>
#include <ESP_Mail_Client.h>

#include "Config.h"

// SMTP sender over ESP-Mail-Client (Gmail SSL, app password). Credentials are
// passed in from secrets.h at wiring time and are never logged. The §7 rate-limit
// policy lives in core/email_policy; this only performs one synchronous send and
// reports success/failure.
//
// WARNING: g_* store the caller's pointers — each must be a string literal or
// have static storage duration that outlives this object.

namespace {
const char* g_host     = "";
uint16_t    g_port     = 0;
const char* g_user     = "";
const char* g_password = "";
const char* g_from     = "";
} // namespace

Mailer::Mailer(const char* host, uint16_t port, const char* user, const char* password,
               const char* from) {
    g_host     = host;
    g_port     = port;
    g_user     = user;
    g_password = password;
    g_from     = from;
}

Result<void> Mailer::send(const char* to, const char* subject, const char* body) {
    if (to == nullptr || subject == nullptr || body == nullptr) {
        return Result<void>::err(Status::kInvalidArg);
    }

    SMTPSession    smtp;
    Session_Config config;
    config.server.host_name  = g_host;
    config.server.port       = g_port;
    config.login.email       = g_user;
    config.login.password    = g_password;
    config.login.user_domain = "";
    // Bound the blocking send so a dead SMTP server cannot wedge the caller
    // (the task is WDT-unsubscribed around send; keep that window finite).
    smtp.setTCPTimeout(cfg::email::kSendTimeoutMs / 1000U);

    if (!smtp.connect(&config)) {
        return Result<void>::err(Status::kSendFailed);
    }

    SMTP_Message message;
    message.sender.name  = "Teplomer";
    message.sender.email = g_from;
    message.subject      = subject;
    message.addRecipient("", to);
    message.text.content = body;
    message.text.charSet = "utf-8";

    const bool ok = MailClient.sendMail(&smtp, &message);
    smtp.closeSession();
    return ok ? Result<void>::ok() : Result<void>::err(Status::kSendFailed);
}

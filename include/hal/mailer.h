#pragma once
#include <cstdint>

#include "result.h"

// HAL: SMTP email sender.
// Target: ESP-Mail-Client library (Phase 5).  Fake: captures last send, injectable return.
class Mailer {
  public:
    // SMTP connection params (pass from secrets.h at wiring time).
    Mailer(const char* host, uint16_t port, const char* user, const char* password,
           const char* from);

    // Send email to `to` address.  Returns kSendFailed on any SMTP error.
    Result<void> send(const char* to, const char* subject, const char* body);

#ifdef NATIVE_BUILD
    // Injection / inspection API
    void        setNextResult(Status s) { nextResult_ = s; }
    const char* lastSubject() const { return lastSubject_; }
    const char* lastBody() const { return lastBody_; }
    uint32_t    sendCount() const { return sendCount_; }

  private:
    Status   nextResult_{Status::kOk};
    char     lastSubject_[64]{};
    char     lastBody_[256]{};
    uint32_t sendCount_{0};
#endif
};

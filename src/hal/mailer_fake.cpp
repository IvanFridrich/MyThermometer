#include "hal/mailer.h"

#include <cstdint>
#include <cstring>

#include "result.h"

Mailer::Mailer(const char* /*host*/, uint16_t /*port*/, const char* /*user*/,
               const char* /*password*/, const char* /*from*/) {}

Result<void> Mailer::send(const char* /*to*/, const char* subject, const char* body) {
    if (subject != nullptr) {
        std::strncpy(lastSubject_, subject, sizeof(lastSubject_) - 1U);
        lastSubject_[sizeof(lastSubject_) - 1U] = '\0';
    }
    if (body != nullptr) {
        std::strncpy(lastBody_, body, sizeof(lastBody_) - 1U);
        lastBody_[sizeof(lastBody_) - 1U] = '\0';
    }
    ++sendCount_;
    if (nextResult_ != Status::kOk) {
        return Result<void>::err(nextResult_);
    }
    return Result<void>::ok();
}

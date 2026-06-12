// Phase 4 will replace this stub with the ESP-IDF Preferences driver.
#include "hal/nvs_store.h"

// WARNING: g_ns stores the caller's pointer — must be a string literal or have
// static storage duration that outlives this object.
static const char* g_ns;
NvsStore::NvsStore(const char* ns) {
    g_ns = ns;
}

Result<void> NvsStore::open() {
    return Result<void>::ok();
}
void NvsStore::close() {}

Result<bool> NvsStore::getBool(const char* /*key*/, bool defaultVal) const {
    return Result<bool>::ok(defaultVal);
}
Result<int16_t> NvsStore::getInt16(const char* /*key*/, int16_t defaultVal) const {
    return Result<int16_t>::ok(defaultVal);
}
Result<uint8_t> NvsStore::getUint8(const char* /*key*/, uint8_t defaultVal) const {
    return Result<uint8_t>::ok(defaultVal);
}
Result<uint32_t> NvsStore::getUint32(const char* /*key*/, uint32_t defaultVal) const {
    return Result<uint32_t>::ok(defaultVal);
}

Result<void> NvsStore::putBool(const char* /*key*/, bool /*val*/) {
    return Result<void>::ok();
}
Result<void> NvsStore::putInt16(const char* /*key*/, int16_t /*val*/) {
    return Result<void>::ok();
}
Result<void> NvsStore::putUint8(const char* /*key*/, uint8_t /*val*/) {
    return Result<void>::ok();
}
Result<void> NvsStore::putUint32(const char* /*key*/, uint32_t /*val*/) {
    return Result<void>::ok();
}

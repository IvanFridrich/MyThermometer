#include "hal/nvs_store.h"

#include <cstring>

#include <Arduino.h>
#include <Preferences.h>

// Typed NVS persistence over the ESP32 Preferences library. The namespace is
// copied at construction (NVS namespaces are <= 15 chars). Every get/put guards
// on open_ so a closed store fails with kStorageErr, matching the fake.
//
// Keys are the compile-time §6.4 NVS-key literals (never null), so they are not
// re-checked for null on each accessor.

NvsStore::NvsStore(const char* ns) {
    std::strncpy(ns_, ns != nullptr ? ns : "", sizeof(ns_) - 1U);
    ns_[sizeof(ns_) - 1U] = '\0';
}

Result<void> NvsStore::open() {
    open_ = prefs_.begin(ns_, /*readOnly=*/false);
    return open_ ? Result<void>::ok() : Result<void>::err(Status::kStorageErr);
}

void NvsStore::close() {
    if (open_) {
        prefs_.end();
        open_ = false;
    }
}

// --- Getters: return the stored value, or defaultVal when the key is absent ---

Result<bool> NvsStore::getBool(const char* key, bool defaultVal) const {
    if (!open_) {
        return Result<bool>::err(Status::kStorageErr);
    }
    return Result<bool>::ok(prefs_.getBool(key, defaultVal));
}

Result<int16_t> NvsStore::getInt16(const char* key, int16_t defaultVal) const {
    if (!open_) {
        return Result<int16_t>::err(Status::kStorageErr);
    }
    return Result<int16_t>::ok(prefs_.getShort(key, defaultVal));
}

Result<uint8_t> NvsStore::getUint8(const char* key, uint8_t defaultVal) const {
    if (!open_) {
        return Result<uint8_t>::err(Status::kStorageErr);
    }
    return Result<uint8_t>::ok(prefs_.getUChar(key, defaultVal));
}

Result<uint32_t> NvsStore::getUint32(const char* key, uint32_t defaultVal) const {
    if (!open_) {
        return Result<uint32_t>::err(Status::kStorageErr);
    }
    return Result<uint32_t>::ok(prefs_.getULong(key, defaultVal));
}

// --- Setters: putX return the byte count written (0 = failure) ---

Result<void> NvsStore::putBool(const char* key, bool val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    return (prefs_.putBool(key, val) > 0) ? Result<void>::ok()
                                          : Result<void>::err(Status::kStorageErr);
}

Result<void> NvsStore::putInt16(const char* key, int16_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    return (prefs_.putShort(key, val) > 0) ? Result<void>::ok()
                                           : Result<void>::err(Status::kStorageErr);
}

Result<void> NvsStore::putUint8(const char* key, uint8_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    return (prefs_.putUChar(key, val) > 0) ? Result<void>::ok()
                                           : Result<void>::err(Status::kStorageErr);
}

Result<void> NvsStore::putUint32(const char* key, uint32_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    return (prefs_.putULong(key, val) > 0) ? Result<void>::ok()
                                           : Result<void>::err(Status::kStorageErr);
}

#include "hal/nvs_store.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "result.h"

NvsStore::NvsStore(const char* /*ns*/) {}

Result<void> NvsStore::open() {
    open_ = true;
    return Result<void>::ok();
}

void NvsStore::close() {
    open_ = false;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const char* NvsStore::findEntry(const char* key, uint8_t& outLen) const {
    for (const auto& e : entries_) {
        if (e.used && std::strcmp(e.key, key) == 0) {
            outLen = e.dataLen;
            return reinterpret_cast<const char*>(e.data);
        }
    }
    return nullptr;
}

bool NvsStore::storeEntry(const char* key, const uint8_t* data, uint8_t len) {
    for (auto& e : entries_) {
        if (e.used && std::strcmp(e.key, key) == 0) {
            std::memcpy(e.data, data, len);
            e.dataLen = len;
            return true;
        }
    }
    for (auto& e : entries_) {
        if (!e.used) {
            std::strncpy(e.key, key, sizeof(e.key) - 1U);
            e.key[sizeof(e.key) - 1U] = '\0';
            std::memcpy(e.data, data, len);
            e.dataLen = len;
            e.used    = true;
            return true;
        }
    }
    return false;
}

void NvsStore::eraseAll() {
    for (auto& e : entries_) {
        e = Entry{};
    }
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

Result<bool> NvsStore::getBool(const char* key, bool defaultVal) const {
    if (!open_) {
        return Result<bool>::err(Status::kStorageErr);
    }
    uint8_t     len{0};
    const char* p = findEntry(key, len);
    if (p == nullptr || len < 1U) {
        return Result<bool>::ok(defaultVal);
    }
    return Result<bool>::ok(static_cast<bool>(*reinterpret_cast<const uint8_t*>(p)));
}

Result<int16_t> NvsStore::getInt16(const char* key, int16_t defaultVal) const {
    if (!open_) {
        return Result<int16_t>::err(Status::kStorageErr);
    }
    uint8_t     len{0};
    const char* p = findEntry(key, len);
    if (p == nullptr || len < 2U) {
        return Result<int16_t>::ok(defaultVal);
    }
    int16_t v{};
    std::memcpy(&v, p, 2);
    return Result<int16_t>::ok(v);
}

Result<uint8_t> NvsStore::getUint8(const char* key, uint8_t defaultVal) const {
    if (!open_) {
        return Result<uint8_t>::err(Status::kStorageErr);
    }
    uint8_t     len{0};
    const char* p = findEntry(key, len);
    if (p == nullptr || len < 1U) {
        return Result<uint8_t>::ok(defaultVal);
    }
    return Result<uint8_t>::ok(*reinterpret_cast<const uint8_t*>(p));
}

Result<uint32_t> NvsStore::getUint32(const char* key, uint32_t defaultVal) const {
    if (!open_) {
        return Result<uint32_t>::err(Status::kStorageErr);
    }
    uint8_t     len{0};
    const char* p = findEntry(key, len);
    if (p == nullptr || len < 4U) {
        return Result<uint32_t>::ok(defaultVal);
    }
    uint32_t v{};
    std::memcpy(&v, p, 4);
    return Result<uint32_t>::ok(v);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

Result<void> NvsStore::putBool(const char* key, bool val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    const auto raw = static_cast<uint8_t>(val);
    if (!storeEntry(key, &raw, 1)) {
        return Result<void>::err(Status::kStorageErr);
    }
    return Result<void>::ok();
}

Result<void> NvsStore::putInt16(const char* key, int16_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    std::array<uint8_t, 2> raw{};
    std::memcpy(raw.data(), &val, 2);
    if (!storeEntry(key, raw.data(), 2)) {
        return Result<void>::err(Status::kStorageErr);
    }
    return Result<void>::ok();
}

Result<void> NvsStore::putUint8(const char* key, uint8_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    if (!storeEntry(key, &val, 1)) {
        return Result<void>::err(Status::kStorageErr);
    }
    return Result<void>::ok();
}

Result<void> NvsStore::putUint32(const char* key, uint32_t val) {
    if (!open_) {
        return Result<void>::err(Status::kStorageErr);
    }
    std::array<uint8_t, 4> raw{};
    std::memcpy(raw.data(), &val, 4);
    if (!storeEntry(key, raw.data(), 4)) {
        return Result<void>::err(Status::kStorageErr);
    }
    return Result<void>::ok();
}

#pragma once
#include <cstdint>

#include "Config.h"
#include "result.h"

#ifndef NATIVE_BUILD
#include <Preferences.h> // ESP32 NVS wrapper — target build only
#endif

// HAL: typed NVS (flash) persistence.
// Target: ESP-IDF Preferences (Phase 4).  Fake: in-memory key-value table.
// Namespace is set at construction; call open() before any get/put.
class NvsStore {
  public:
    explicit NvsStore(const char* ns);

    Result<void> open();
    void         close();

    Result<bool>     getBool(const char* key, bool defaultVal) const;
    Result<int16_t>  getInt16(const char* key, int16_t defaultVal) const;
    Result<uint8_t>  getUint8(const char* key, uint8_t defaultVal) const;
    Result<uint32_t> getUint32(const char* key, uint32_t defaultVal) const;

    Result<void> putBool(const char* key, bool val);
    Result<void> putInt16(const char* key, int16_t val);
    Result<void> putUint8(const char* key, uint8_t val);
    Result<void> putUint32(const char* key, uint32_t val);

#ifdef NATIVE_BUILD
    // Test helper: wipe all stored entries.
    void eraseAll();

  private:
    struct Entry {
        char    key[16]{};
        uint8_t data[4]{};
        uint8_t dataLen{0};
        bool    used{false};
    };
    Entry       entries_[cfg::nvs::kMaxEntries]{};
    bool        open_{false};
    const char* findEntry(const char* key, uint8_t& outLen) const;
    bool        storeEntry(const char* key, const uint8_t* data, uint8_t len);
#endif

#ifndef NATIVE_BUILD
  private:
    mutable Preferences prefs_;       // ESP32 NVS handle (mutable: get* are const)
    char                ns_[16]{};    // namespace, copied at construction
    bool                open_{false}; // begin() succeeded
#endif
};

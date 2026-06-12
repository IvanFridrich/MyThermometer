#pragma once
#include <cassert>
#include <cstdint>

// Error codes returned by all HAL and domain operations.
enum class Status : uint8_t {
    kOk = 0,
    kSensorOpen, // no device on bus
    kOneWireErr, // CRC or bus protocol error
    kWeirdValue, // reading outside plausibility window
    kInvalidArg,
    kNotReady,
    kTimeout,
    kSendFailed, // email / network
    kStorageErr, // NVS read / write failure
    kBusy,
};

// Result<T>: lightweight discriminated union (value | error), no exceptions.
// T must be default-constructible (int16_t, uint32_t, uint64_t, bool are all fine).
template <typename T> class Result {
  public:
    static Result ok(const T& value) {
        Result r;
        r.value_  = value;
        r.status_ = Status::kOk;
        return r;
    }
    static Result err(Status s) {
        Result r;
        r.status_ = s;
        return r;
    }

    bool     isOk() const { return status_ == Status::kOk; }
    Status   status() const { return status_; }
    const T& value() const {
        assert(isOk());
        return value_;
    }
    T& value() {
        assert(isOk());
        return value_;
    }

  private:
    Result() = default;
    T      value_{};
    Status status_{Status::kOk};
};

// Specialisation for Result<void>: carries only success/failure.
template <> class Result<void> {
  public:
    static Result ok() { return Result(Status::kOk); }
    static Result err(Status s) { return Result(s); }

    bool   isOk() const { return status_ == Status::kOk; }
    Status status() const { return status_; }

  private:
    explicit Result(Status s) : status_(s) {}
    Status status_;
};

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

// Phase 1 link test: instantiate one object of every HAL fake class and call at
// least one method.  Purpose: prove that the native env links cleanly against all
// fakes and that all declared interfaces have exactly one definition.

#include "Config.h"
#include "alarm_state.h"
#include "anomaly.h"
#include "clock.h"
#include "config_model.h"
#include "event_log.h"
#include "hal/ble_advertiser.h"
#include "hal/display.h"
#include "hal/http_server.h"
#include "hal/mailer.h"
#include "hal/nvs_store.h"
#include "hal/onewire_bus.h"
#include "hal/pwm.h"
#include "hal/system_hal.h"
#include "hal/wifi_hal.h"
#include "history_buffer.h"
#include "moving_average.h"
#include "result.h"
#include "types.h"

// ---------------------------------------------------------------------------
// Result<T>
// ---------------------------------------------------------------------------
TEST_CASE("Result<T> ok / err round-trip") {
    auto r = Result<int16_t>::ok(2300);
    CHECK(r.isOk());
    CHECK(r.value() == 2300);

    auto e = Result<int16_t>::err(Status::kSensorOpen);
    CHECK_FALSE(e.isOk());
    CHECK(e.status() == Status::kSensorOpen);

    auto v = Result<void>::ok();
    CHECK(v.isOk());
    auto ve = Result<void>::err(Status::kTimeout);
    CHECK_FALSE(ve.isOk());
}

// ---------------------------------------------------------------------------
// OneWireBusFake
// ---------------------------------------------------------------------------
TEST_CASE("OneWireBusFake injectable reading") {
    OneWireBus bus(cfg::pin::kOneWireInside);
    bus.setNextReading(4500); // 45.00 °C
    auto r = bus.readCentiC();
    CHECK(r.isOk());
    CHECK(r.value() == 4500);
}

TEST_CASE("OneWireBusFake error injection") {
    OneWireBus bus(cfg::pin::kOneWireOutside);
    bus.setNextReading(0, Status::kSensorOpen);
    auto r = bus.readCentiC();
    CHECK_FALSE(r.isOk());
    CHECK(r.status() == Status::kSensorOpen);
}

TEST_CASE("OneWireBusFake readRomId") {
    OneWireBus bus(cfg::pin::kOneWireInside);
    bus.setNextRomId(0xDEADBEEF12345678ULL);
    auto r = bus.readRomId();
    CHECK(r.isOk());
    CHECK(r.value() == 0xDEADBEEF12345678ULL);
    CHECK(bus.lastQueriedRomId() == 0xDEADBEEF12345678ULL);
}

// ---------------------------------------------------------------------------
// DisplayFake
// ---------------------------------------------------------------------------
TEST_CASE("DisplayFake print and row inspection") {
    Display disp(cfg::pin::kLcdRs, cfg::pin::kLcdEn, cfg::pin::kLcdD4, cfg::pin::kLcdD5,
                 cfg::pin::kLcdD6, cfg::pin::kLcdD7);
    CHECK(disp.init().isOk());
    disp.setCursor(0, 0);
    disp.print("I:23.4 ");
    CHECK(std::string(disp.row(0)).substr(0, 7) == "I:23.4 ");
}

// ---------------------------------------------------------------------------
// PwmFake
// ---------------------------------------------------------------------------
TEST_CASE("PwmFake tone and contrast") {
    Pwm pwm;
    CHECK(pwm.initContrast(cfg::pin::kLcdContrastV0, cfg::ledc::kContrastChannel).isOk());
    CHECK(pwm.initBuzzer(cfg::pin::kBuzzer, cfg::ledc::kBuzzerChannel).isOk());
    pwm.setContrastDuty(200);
    CHECK(pwm.lastContrastDuty() == 200);
    pwm.tone(cfg::beep::kBootToneHz);
    CHECK(pwm.toneActive());
    CHECK(pwm.lastToneHz() == cfg::beep::kBootToneHz);
    pwm.noTone();
    CHECK_FALSE(pwm.toneActive());
}

// ---------------------------------------------------------------------------
// WifiHalFake
// ---------------------------------------------------------------------------
TEST_CASE("WifiHalFake connect and mDNS") {
    WifiHal wifi;
    wifi.setConnected(false);
    CHECK_FALSE(wifi.isConnected());
    CHECK(wifi.begin("SSID", "pass").isOk());
    CHECK(wifi.isConnected()); // begin() sets connected=true in fake
    CHECK(wifi.startMdns(cfg::net::kMdnsHostname).isOk());
    CHECK(std::string(wifi.mdnsHostname()) == cfg::net::kMdnsHostname);
}

// ---------------------------------------------------------------------------
// HttpServerFake
// ---------------------------------------------------------------------------
static bool g_handlerCalled = false;
static void testHandler() {
    g_handlerCalled = true;
}

TEST_CASE("HttpServerFake route registration and dispatch") {
    HttpServer server(cfg::net::kHttpPort);
    CHECK(server.begin().isOk());
    server.on("/status", 0, testHandler);
    CHECK(server.routeCount() == 1);
    g_handlerCalled = false;
    CHECK(server.dispatchRequest("/status", 0));
    CHECK(g_handlerCalled);
    CHECK_FALSE(server.dispatchRequest("/missing", 0));
}

// ---------------------------------------------------------------------------
// BleAdvertiserFake
// ---------------------------------------------------------------------------
TEST_CASE("BleAdvertiserFake payload and burst") {
    BleAdvertiser ble;
    CHECK(ble.init(cfg::ble::kDeviceName, cfg::ble::kCompanyId).isOk());
    const uint8_t payload[] = {0xFF, 0xFF, 0x01, 0xFC, 0x11, 0x08, 0x09, 0x00, 0x01};
    ble.setPayload(payload, sizeof(payload));
    CHECK(ble.lastPayloadLen() == sizeof(payload));
    CHECK(ble.burst(cfg::ble::kBurstsPerMinute, cfg::ble::kBurstSpacingMs).isOk());
    CHECK(ble.totalBurstCount() == cfg::ble::kBurstsPerMinute);
}

// ---------------------------------------------------------------------------
// MailerFake
// ---------------------------------------------------------------------------
TEST_CASE("MailerFake successful send") {
    Mailer mailer("smtp.example.com", 465, "user", "pass", "from@example.com");
    CHECK(mailer.send("to@example.com", "Fire alarm!", "Inner temp exceeded.").isOk());
    CHECK(std::string(mailer.lastSubject()) == "Fire alarm!");
    CHECK(mailer.sendCount() == 1U);
}

TEST_CASE("MailerFake injected failure") {
    Mailer mailer("smtp.example.com", 465, "user", "pass", "from@example.com");
    mailer.setNextResult(Status::kSendFailed);
    auto r = mailer.send("to@example.com", "Test", "Body");
    CHECK_FALSE(r.isOk());
    CHECK(r.status() == Status::kSendFailed);
}

// ---------------------------------------------------------------------------
// NvsStoreFake
// ---------------------------------------------------------------------------
TEST_CASE("NvsStoreFake round-trip put/get") {
    NvsStore nvs("thermo");
    CHECK(nvs.open().isOk());

    CHECK(nvs.putBool("beeper", true).isOk());
    CHECK(nvs.getBool("beeper", false).value() == true);

    CHECK(nvs.putInt16("diff_thr", 200).isOk());
    CHECK(nvs.getInt16("diff_thr", 0).value() == 200);

    CHECK(nvs.putUint8("contrast", 128).isOk());
    CHECK(nvs.getUint8("contrast", 0).value() == 128);

    CHECK(nvs.putUint32("uptime", 0xDEAD1234UL).isOk());
    CHECK(nvs.getUint32("uptime", 0).value() == 0xDEAD1234UL);

    nvs.eraseAll();
    CHECK(nvs.getBool("beeper", false).value() == false); // default after erase
    nvs.close();
}

// ---------------------------------------------------------------------------
// SystemHalFake
// ---------------------------------------------------------------------------
TEST_CASE("SystemHalFake injectable heap and WDT") {
    SystemHal sys;
    sys.setFreeHeap(150000);
    sys.setMinFreeHeap(140000);
    CHECK(sys.freeHeap() == 150000U);
    CHECK(sys.minFreeHeap() == 140000U);
    sys.wdtFeed();
    sys.wdtFeed();
    CHECK(sys.wdtFeedCount() == 2U);
    sys.restart();
    CHECK(sys.restartCount() == 1U);
}

// ---------------------------------------------------------------------------
// ClockFake
// ---------------------------------------------------------------------------
TEST_CASE("ClockFake advance and reset") {
    Clock clk;
    clk.reset();
    CHECK(clk.millis() == 0U);
    clk.advanceMs(60000);
    CHECK(clk.millis() == 60000U);
    clk.advanceMs(600000);
    CHECK(clk.millis() == 660000U);
    clk.reset();
    CHECK(clk.millis() == 0U);
}

// ---------------------------------------------------------------------------
// MovingAverage
// ---------------------------------------------------------------------------
TEST_CASE("MovingAverage basic average") {
    MovingAverage<10> avg;
    CHECK(avg.average() == kTempInvalid);
    avg.push(2300);
    avg.push(2400);
    CHECK(avg.average() == 2350);
    CHECK(avg.validCount() == 2);
    CHECK_FALSE(avg.isFull());
}

TEST_CASE("MovingAverage excludes invalid samples") {
    MovingAverage<3> avg;
    avg.push(1000);
    avg.push(kTempInvalid);
    avg.push(2000);
    CHECK(avg.average() == 1500); // only two valid samples
    CHECK(avg.validCount() == 2);
}

// ---------------------------------------------------------------------------
// HistoryBuffer
// ---------------------------------------------------------------------------
TEST_CASE("HistoryBuffer append and at") {
    HistoryBuffer buf;
    CHECK(buf.count() == 0U);
    HistoryRecord r{2300, 2100, cfg::flag::kBoot};
    buf.append(r);
    CHECK(buf.count() == 1U);
    CHECK(buf.at(0).t_inner_c100 == 2300);
    CHECK(buf.at(0).flags == cfg::flag::kBoot);
}

// ---------------------------------------------------------------------------
// AnomalyDetector
// ---------------------------------------------------------------------------
TEST_CASE("AnomalyDetector normal reading clears errors") {
    AnomalyDetector det(3);
    auto            ok = Result<Temperature>::ok(2300);
    CHECK(det.classify(ok) == 0U);
    CHECK(det.consecutiveErrors() == 0U);
}

TEST_CASE("AnomalyDetector raises SENSOR_OPEN after threshold errors") {
    AnomalyDetector det(3);
    auto            err = Result<Temperature>::err(Status::kOneWireErr);
    det.classify(err);
    det.classify(err);
    CHECK((det.classify(err) & cfg::flag::kSensorOpen) != 0U);
}

// ---------------------------------------------------------------------------
// ConfigModel
// ---------------------------------------------------------------------------
TEST_CASE("ConfigModel defaults are valid") {
    ConfigModel m = ConfigModel::defaults();
    CHECK(m.validate());
    CHECK(m.beeperEnabled);
    CHECK(m.diffThresholdC100 == 200);
}

// ---------------------------------------------------------------------------
// AlarmState
// ---------------------------------------------------------------------------
TEST_CASE("AlarmState fire rising edge") {
    ConfigModel         cfg = ConfigModel::defaults();
    AlarmState          alm(cfg);
    MeasurementSnapshot snap{4600, 4600, 2100, 0};
    AlarmEdges          edges = alm.update(snap);
    CHECK(edges.fireRising);
    CHECK(alm.isFire());
}

TEST_CASE("AlarmState diff rising edge") {
    ConfigModel cfg = ConfigModel::defaults();
    AlarmState  alm(cfg);
    // CoolRoom goal (default): alarm fires when outer is cooler than inner by > threshold.
    // diff = outerAvg - innerAvg = 2000 - 2300 = -300 centi-°C  (outer 3 °C cooler, > 2 °C thr)
    MeasurementSnapshot snap{2000, 2300, 2000, 0};
    AlarmEdges          edges = alm.update(snap);
    CHECK(edges.diffRising);
    CHECK(alm.isDiff());
}

// ---------------------------------------------------------------------------
// EventLog
// ---------------------------------------------------------------------------
TEST_CASE("EventLog append and at") {
    EventLog log;
    log.append(cfg::log::Level::kInfo, "TEST", "Hello world", 1234);
    CHECK(log.count() == 1U);
    CHECK(log.at(0).uptimeMs == 1234U);
    CHECK(std::string(log.at(0).module) == "TEST");
    CHECK(std::string(log.at(0).message) == "Hello world");
}

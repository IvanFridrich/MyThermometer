#include "hal/display.h"

#include <cstdio>

#include <Arduino.h>
#include <lvgl.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "Config.h"

// Waveshare 1.9" ST7789 IPS 170x320 over SPI, landscape (320x170), rendered by
// LVGL v9 with LovyanGFX as the flush backend (DMA). All LVGL/LGFX state lives
// here as file-statics; nothing leaks into the header (link-time seam, NFR-06).
//
// Threading: everything except setBrightness() runs on Core 1 only.

namespace {

class LGFX_Thermo final : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI      bus_;

  public:
    LGFX_Thermo() {
        auto b        = bus_.config();
        b.spi_host    = SPI2_HOST; // FSPI on ESP32-S3
        b.spi_mode    = 0;
        b.freq_write  = cfg::display::kSpiHz;
        b.spi_3wire   = false;
        b.use_lock    = false; // single-task access (Core 1 only)
        b.dma_channel = SPI_DMA_CH_AUTO;
        b.pin_sclk    = cfg::pin::kDispSclk;
        b.pin_mosi    = cfg::pin::kDispMosi;
        b.pin_miso    = -1;
        b.pin_dc      = cfg::pin::kDispDc;
        bus_.config(b);
        panel_.setBus(&bus_);

        auto p            = panel_.config();
        p.pin_cs          = cfg::pin::kDispCs;
        p.pin_rst         = cfg::pin::kDispRst;
        p.pin_busy        = -1;
        p.panel_width     = cfg::display::kPanelW; // 170
        p.panel_height    = cfg::display::kPanelH; // 320
        p.memory_width    = cfg::display::kMemW;   // 240 — ST7789 GRAM quirk
        p.memory_height   = cfg::display::kMemH;
        p.offset_x        = cfg::display::kOffsetX; // 35 — glass centred in GRAM
        p.offset_y        = 0;
        p.offset_rotation = 0;
        p.readable        = false;
        p.invert          = true; // mandatory on this IPS panel
        p.rgb_order       = false;
        p.bus_shared      = false;
        panel_.config(p);
        setPanel(&panel_);
    }
};

LGFX_Thermo s_gfx; // ctor only stores config; no HW access until init()

constexpr uint32_t kBufPx =
    static_cast<uint32_t>(cfg::display::kWidth) * cfg::display::kDrawBufLines;
// Static double buffers in internal RAM (.bss = DMA-capable; no PSRAM on board).
alignas(16) uint8_t s_buf1[kBufPx * 2U];
alignas(16) uint8_t s_buf2[kBufPx * 2U];

lv_display_t* s_disp{nullptr};
lv_obj_t*     s_lblInner{nullptr};
lv_obj_t*     s_lblStatus{nullptr};

// Layout / colors (RGB565-independent lv_color codes).
constexpr int32_t  kRowPadY      = 16;
constexpr uint32_t kColorTempHex = 0xFFFFFF; // white
constexpr uint32_t kColorFireHex = 0xFF3020; // red
constexpr uint32_t kColorWarnHex = 0xFFA020; // orange (SENSOR / WiFi DOWN)

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    // v9 removed LV_COLOR_16_SWAP; swap to panel endianness here, then DMA.
    lv_draw_sw_rgb565_swap(px_map, static_cast<uint32_t>(w) * static_cast<uint32_t>(h));
    s_gfx.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<const lgfx::swap565_t*>(px_map));
    // Safe with double buffering: LVGL renders into the other buffer while DMA
    // runs; the next pushImageDMA blocks until this transfer completes.
    lv_display_flush_ready(disp);
}

// "I 21.5°C" / "O -12.3°C" from centi-degC without float. UTF-8 ° = "\xC2\xB0".
void fmtTemp(char* out, size_t n, char prefix, Temperature c100) {
    if (c100 == kTempInvalid) {
        snprintf(out, n,
                 "%c --.-\xC2\xB0"
                 "C",
                 prefix);
        return;
    }
    const bool    neg   = c100 < 0;
    const int32_t absC  = neg ? -static_cast<int32_t>(c100) : c100;
    const int32_t whole = absC / 100;
    const int32_t tenth = (absC % 100) / 10;
    snprintf(out, n,
             "%c %s%ld.%ld\xC2\xB0"
             "C",
             prefix, neg ? "-" : "", static_cast<long>(whole), static_cast<long>(tenth));
}

} // namespace

Result<void> Display::init() {
    s_gfx.init();
    s_gfx.setRotation(cfg::display::kRotation); // 320x170 landscape
    s_gfx.startWrite();                         // hold the bus: enables DMA overlap

    // Backlight: LEDC Timer 1 (buzzer owns Timer 0 — tone() cannot corrupt duty).
    ledcSetup(cfg::ledc::kBacklightChannel, cfg::ledc::kBacklightFreqHz,
              cfg::ledc::kBacklightResBits);
    ledcAttachPin(cfg::pin::kDispBacklight, cfg::ledc::kBacklightChannel);
    ledcWrite(cfg::ledc::kBacklightChannel, cfg::ledc::kBacklightDefault);

    lv_init();
    lv_tick_set_cb(+[]() -> uint32_t { return static_cast<uint32_t>(millis()); });

    s_disp = lv_display_create(cfg::display::kWidth, cfg::display::kHeight);
    if (s_disp == nullptr) {
        return Result<void>::err(Status::kNotReady);
    }
    lv_display_set_flush_cb(s_disp, flushCb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    s_lblInner = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lblInner, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lblInner, lv_color_hex(kColorTempHex), 0);
    lv_obj_align(s_lblInner, LV_ALIGN_TOP_MID, 0, kRowPadY);
    lv_label_set_text(s_lblInner, "");

    s_lblStatus = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lblStatus, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lblStatus, lv_color_hex(kColorTempHex), 0);
    lv_obj_align(s_lblStatus, LV_ALIGN_BOTTOM_MID, 0, -kRowPadY);
    lv_label_set_text(s_lblStatus, "");

    return Result<void>::ok();
}

void Display::setBrightness(uint8_t duty) {
    ledcWrite(cfg::ledc::kBacklightChannel, duty);
}

void Display::render(const DisplayFrame& f) {
    if (!firstRender_ && f == cached_) {
        return; // no churn at 1 Hz with stable readings
    }
    firstRender_ = false;
    cached_      = f;

    char buf[20];
    fmtTemp(buf, sizeof(buf), 'I', f.innerC100);
    lv_label_set_text(s_lblInner, buf);

    switch (f.status) {
    case DisplayStatus::kFire:
        lv_obj_set_style_text_color(s_lblStatus, lv_color_hex(kColorFireHex), 0);
        lv_label_set_text(s_lblStatus, "FIRE!");
        break;
    case DisplayStatus::kSensorFault:
        lv_obj_set_style_text_color(s_lblStatus, lv_color_hex(kColorWarnHex), 0);
        lv_label_set_text(s_lblStatus, "SENSOR");
        break;
    case DisplayStatus::kWifiDown:
        lv_obj_set_style_text_color(s_lblStatus, lv_color_hex(kColorWarnHex), 0);
        lv_label_set_text(s_lblStatus, "WiFi DOWN");
        break;
    case DisplayStatus::kOuterTemp:
        lv_obj_set_style_text_color(s_lblStatus, lv_color_hex(kColorTempHex), 0);
        fmtTemp(buf, sizeof(buf), 'O', f.outerC100);
        lv_label_set_text(s_lblStatus, buf);
        break;
    }
}

void Display::tick() {
    lv_timer_handler();
}

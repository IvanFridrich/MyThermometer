/* lv_conf.h — LVGL v9 configuration.
 *
 * Sanctioned exception to the "all constants in Config.h" rule: LVGL consumes
 * these at preprocessor level, so they cannot live in cfg::. Runtime-usable
 * display constants live in cfg::display (src/Config.h) — keep the two in sync.
 *
 * Minimal-override style: lv_conf_internal.h supplies defaults for everything
 * not defined here. NFR-04: static memory pool, no heap growth.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
/* NOTE: LV_COLOR_16_SWAP no longer exists in v9 — byte swap happens in the
 * flush callback via lv_draw_sw_rgb565_swap() (src/hal/display_target.cpp). */

/* Static pool allocator (builtin) over a fixed .bss array; never grows. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (24U * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0

#define LV_USE_OS LV_OS_NONE
/* Tick source: lv_tick_set_cb(millis) at runtime (LV_TICK_CUSTOM removed in v9). */

#define LV_USE_LOG 0
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 0 /* no arcs/shadows/masks — saves flash */

/* Widgets: label only. Others default to 1 → force off to save flash. */
#define LV_USE_LABEL 1
#define LV_USE_ARC 0
#define LV_USE_ANIMIMG 0
#define LV_USE_BAR 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1 /* pixel-art icons (house / landscape / window advice) */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

/* Built-in Montserrat includes U+00B0 (°) → real "°C" glyphs. */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_24 1 /* status row */
#define LV_FONT_MONTSERRAT_48 1 /* temperatures */
#define LV_FONT_DEFAULT &lv_font_montserrat_24

#endif /* LV_CONF_H */

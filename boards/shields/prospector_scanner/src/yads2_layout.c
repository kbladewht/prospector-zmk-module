/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * YADS2 Layout for Scanner Mode
 *
 * Second YADS-style screen. The widget arrangement follows the original YADS
 * status screen from janpfischer/zmk-dongle-screen (MIT License):
 * https://github.com/janpfischer/zmk-dongle-screen/tree/main/boards/shields/dongle_screen
 * (output arrow top-right, WPM top-left, layer in the middle, NerdFont
 * modifier icons underneath, battery indicators along the bottom edge).
 *
 * The data itself comes from the scanner's BLE advertisement receiver
 * (structure prospector_keyboard_data), not from local ZMK state.
 *
 * Display: 280x240 usable area (same coordinate space as the other layouts).
 */

#include "yads2_layout.h"
#include "fonts_carrefinho.h"
#include "fonts.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(yads2_layout, CONFIG_ZMK_LOG_LEVEL);

/* LVGL built-in font used for the compact output status lines */
LV_FONT_DECLARE(lv_font_montserrat_16);

/* ========== Colors ========== */
#define YADS2_COLOR_TEXT          0xFFFFFF
#define YADS2_COLOR_DIM           0x7B7D93
#define YADS2_COLOR_USB_READY     0xFFFFFF
#define YADS2_COLOR_BLE_CONNECTED 0x00FF00
#define YADS2_COLOR_BLE_BONDED    0x4A90E2
#define YADS2_COLOR_BLE_OPEN      0xFFFFFF
#define YADS2_COLOR_BATTERY_OK    0xFFFFFF
#define YADS2_COLOR_BATTERY_LOW   0xFFC000
#define YADS2_COLOR_BATTERY_OFF   0xE63030
#define YADS2_COLOR_BAR_TRACK     0x202020
#define YADS2_COLOR_BAR_LOW_TRACK 0x584028
#define YADS2_COLOR_BAR_OFF_TRACK 0x5A2020

#define YADS2_LOW_BATTERY_THRESHOLD 20

/* ========== Geometry (280x240 coordinate space) ========== */
/* Top row: WPM (left), keyboard name (centre), output status (right) */
#define YADS2_WPM_VALUE_X 14
#define YADS2_WPM_VALUE_Y 4
#define YADS2_WPM_CAPTION_X 16
#define YADS2_WPM_CAPTION_Y 42
#define YADS2_NAME_Y 8
#define YADS2_NAME_WIDTH 120
#define YADS2_OUTPUT_X_OFFSET (-10)
#define YADS2_OUTPUT_USB_Y 6
#define YADS2_OUTPUT_BLE_Y 30

/* Centre: layer name + modifier icons */
#define YADS2_LAYER_Y (-14)
#define YADS2_MOD_Y 34

/* Bottom: battery row */
#define YADS2_BATTERY_ROW_WIDTH 260
#define YADS2_BATTERY_ROW_HEIGHT 46
#define YADS2_BATTERY_ROW_Y_OFFSET (-4)
#define YADS2_BATTERY_BAR_Y 22
#define YADS2_BATTERY_BAR_HEIGHT 6
#define YADS2_BATTERY_NAME_Y 30
#define YADS2_BATTERY_BAR_MAX_WIDTH 120
#define YADS2_BATTERY_BAR_MIN_WIDTH 46
#define YADS2_BATTERY_BAR_GAP 10

/* ========== NerdFont modifier symbols (same glyphs as the Classic screen) ========== */
static const char *mod_symbols[4] = {
    "\xf3\xb0\x98\xb4", /* Control (U+F0634) */
    "\xf3\xb0\x98\xb6", /* Shift   (U+F0636) */
    "\xf3\xb0\x98\xb5", /* Alt     (U+F0635) */
    "\xf3\xb0\x98\xb3"  /* GUI     (U+F0633) */
};

/* ========== Static text buffers ==========
 * lv_label_set_text_static() keeps LVGL from re-allocating label text on every
 * advertisement, which fragments the LVGL pool over hours of operation. */
static char stbuf_wpm[8] = "0";
static char stbuf_layer[16] = "-";
static char stbuf_name[24] = "Scanning...";
static char stbuf_usb[24] = "";
static char stbuf_ble[24] = "";
static char stbuf_mod[64] = "";
static char stbuf_battery[YADS2_MAX_BATTERIES][8] = {{"-"}, {"-"}, {"-"}, {"-"}};

/* ========== Widget state ========== */
struct yads2_battery_slot {
    lv_obj_t *pct;
    lv_obj_t *bar;
    lv_obj_t *name;
};

static lv_obj_t *layout_container = NULL;
static lv_obj_t *wpm_value_label = NULL;
static lv_obj_t *wpm_caption_label = NULL;
static lv_obj_t *name_label = NULL;
static lv_obj_t *usb_label = NULL;
static lv_obj_t *ble_label = NULL;
static lv_obj_t *layer_label = NULL;
static lv_obj_t *mod_label = NULL;
static lv_obj_t *battery_row = NULL;
static struct yads2_battery_slot battery_slots[YADS2_MAX_BATTERIES];

static bool layout_created = false;
static int battery_slot_count = 0;

/* Cached values - updates only touch LVGL when something actually changed */
static bool cached_valid = false;
static uint8_t cached_layer = 0;
static uint8_t cached_wpm = 0;
static uint8_t cached_mods = 0;
static bool cached_usb_connected = false;
static bool cached_ble_connected = false;
static bool cached_ble_bonded = false;
static uint8_t cached_ble_profile = 0;
static char cached_keyboard_name[24] = "";
static char cached_layer_name[16] = "";
static uint8_t cached_battery_level = 0;
static bool cached_battery_connected = false;
static uint8_t cached_peripheral_battery[YADS2_MAX_PERIPHERALS] = {0};
static bool cached_peripheral_connected[YADS2_MAX_PERIPHERALS] = {false};

/* Battery source names, matching the Classic screen's naming by battery count */
static const char *const *battery_names_for_count(int count) {
    static const char *const names_1[] = {""};
    static const char *const names_2[] = {"L", "R"};
    static const char *const names_3[] = {"L", "R", "Aux"};
    static const char *const names_4[] = {"L", "R", "A1", "A2"};

    switch (count) {
    case 2:
        return names_2;
    case 3:
        return names_3;
    case 4:
        return names_4;
    default:
        return names_1;
    }
}

/* ========== Battery row ========== */

/* Size and centre the battery slots for the given number of keyboards/halves */
static void yads2_apply_battery_layout(int count) {
    if (!layout_container || count < 1) {
        return;
    }
    if (count > YADS2_MAX_BATTERIES) {
        count = YADS2_MAX_BATTERIES;
    }

    int bar_width = (YADS2_BATTERY_ROW_WIDTH - (count - 1) * YADS2_BATTERY_BAR_GAP) / count;
    if (bar_width > YADS2_BATTERY_BAR_MAX_WIDTH) {
        bar_width = YADS2_BATTERY_BAR_MAX_WIDTH;
    }
    if (bar_width < YADS2_BATTERY_BAR_MIN_WIDTH) {
        bar_width = YADS2_BATTERY_BAR_MIN_WIDTH;
    }

    int total_width = count * bar_width + (count - 1) * YADS2_BATTERY_BAR_GAP;
    int start_x = (YADS2_BATTERY_ROW_WIDTH - total_width) / 2;
    const char *const *names = battery_names_for_count(count);

    for (int i = 0; i < YADS2_MAX_BATTERIES; i++) {
        struct yads2_battery_slot *slot = &battery_slots[i];
        bool visible = i < count;
        int x = start_x + i * (bar_width + YADS2_BATTERY_BAR_GAP);
        lv_obj_t *objects[3] = {slot->pct, slot->bar, slot->name};

        if (slot->pct) {
            lv_obj_set_width(slot->pct, bar_width);
            lv_obj_set_pos(slot->pct, x, 0);
        }
        if (slot->bar) {
            lv_obj_set_size(slot->bar, bar_width, YADS2_BATTERY_BAR_HEIGHT);
            lv_obj_set_pos(slot->bar, x, YADS2_BATTERY_BAR_Y);
        }
        if (slot->name) {
            lv_obj_set_width(slot->name, bar_width);
            lv_obj_set_pos(slot->name, x, YADS2_BATTERY_NAME_Y);
            lv_label_set_text(slot->name, (visible && names && names[i]) ? names[i] : "");
        }

        for (int o = 0; o < 3; o++) {
            if (!objects[o]) {
                continue;
            }
            if (visible) {
                lv_obj_clear_flag(objects[o], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(objects[o], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    battery_slot_count = count;
}

static void yads2_set_battery_slot(int slot, uint8_t level, bool connected) {
    if (slot < 0 || slot >= YADS2_MAX_BATTERIES) {
        return;
    }

    struct yads2_battery_slot *w = &battery_slots[slot];
    bool low = connected && level > 0 && level <= YADS2_LOW_BATTERY_THRESHOLD;

    uint32_t text_color, fill_color, track_color;
    if (!connected || level == 0) {
        text_color = YADS2_COLOR_BATTERY_OFF;
        fill_color = YADS2_COLOR_BATTERY_OFF;
        track_color = YADS2_COLOR_BAR_OFF_TRACK;
    } else if (low) {
        text_color = YADS2_COLOR_BATTERY_LOW;
        fill_color = YADS2_COLOR_BATTERY_LOW;
        track_color = YADS2_COLOR_BAR_LOW_TRACK;
    } else {
        text_color = YADS2_COLOR_BATTERY_OK;
        fill_color = YADS2_COLOR_BATTERY_OK;
        track_color = YADS2_COLOR_BAR_TRACK;
    }

    if (w->pct) {
        if (connected && level > 0) {
            snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]), "%u", level);
        } else {
            snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]), "-");
        }
        lv_label_set_text_static(w->pct, stbuf_battery[slot]);
        lv_obj_set_style_text_color(w->pct, lv_color_hex(text_color), LV_PART_MAIN);
    }

    if (w->bar) {
        lv_bar_set_value(w->bar, (connected && level > 0) ? level : 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(track_color), LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(fill_color), LV_PART_INDICATOR);
    }
}

/* ========== Output status (top right) ========== */

static void yads2_update_output(bool usb_connected, bool ble_connected, bool ble_bonded,
                                uint8_t ble_profile) {
    if (!usb_label || !ble_label) {
        return;
    }

    uint32_t usb_color = usb_connected ? YADS2_COLOR_USB_READY : YADS2_COLOR_DIM;
    uint32_t ble_color = ble_connected ? YADS2_COLOR_BLE_CONNECTED
                         : ble_bonded  ? YADS2_COLOR_BLE_BONDED
                                       : YADS2_COLOR_BLE_OPEN;

    /* The arrow marks the transport the keyboard is currently using: USB wins
     * when its HID endpoint is ready, otherwise the keyboard talks BLE. */
    snprintf(stbuf_usb, sizeof(stbuf_usb), "%s #%06x USB#", usb_connected ? ">" : " ",
             (unsigned int)usb_color);
    snprintf(stbuf_ble, sizeof(stbuf_ble), "%s #%06x BLE %u#", usb_connected ? " " : ">",
             (unsigned int)ble_color, (unsigned int)(ble_profile + 1));

    lv_label_set_text_static(usb_label, stbuf_usb);
    lv_label_set_text_static(ble_label, stbuf_ble);
}

/* ========== WPM / keyboard name / layer / modifiers ========== */

static void yads2_update_wpm(uint8_t wpm) {
    if (!wpm_value_label) {
        return;
    }
    snprintf(stbuf_wpm, sizeof(stbuf_wpm), "%u", wpm);
    lv_label_set_text_static(wpm_value_label, stbuf_wpm);
}

static void yads2_update_name(const char *keyboard_name) {
    if (!name_label) {
        return;
    }

    if (keyboard_name && keyboard_name[0]) {
        snprintf(stbuf_name, sizeof(stbuf_name), "%s", keyboard_name);
        lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    } else {
        snprintf(stbuf_name, sizeof(stbuf_name), "Scanning...");
        lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
    }
    lv_label_set_text_static(name_label, stbuf_name);
}

static void yads2_update_layer(uint8_t active_layer, const char *layer_name) {
    if (!layer_label) {
        return;
    }

    if (layer_name && layer_name[0]) {
        char upper[16];
        int i = 0;
        for (; layer_name[i] && i < (int)sizeof(upper) - 1; i++) {
            char c = layer_name[i];
            upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        upper[i] = '\0';
        snprintf(stbuf_layer, sizeof(stbuf_layer), "%s", upper);
    } else {
        snprintf(stbuf_layer, sizeof(stbuf_layer), "%u", active_layer);
    }

    lv_label_set_text_static(layer_label, stbuf_layer);
}

static void yads2_update_modifiers(uint8_t modifier_flags) {
    if (!mod_label) {
        return;
    }

    int pos = 0;
    stbuf_mod[0] = '\0';

    if (modifier_flags & 0x11) { /* LCTL | RCTL */
        pos += snprintf(stbuf_mod + pos, sizeof(stbuf_mod) - pos, "%s", mod_symbols[0]);
    }
    if (modifier_flags & 0x22) { /* LSFT | RSFT */
        pos += snprintf(stbuf_mod + pos, sizeof(stbuf_mod) - pos, "%s", mod_symbols[1]);
    }
    if (modifier_flags & 0x44) { /* LALT | RALT */
        pos += snprintf(stbuf_mod + pos, sizeof(stbuf_mod) - pos, "%s", mod_symbols[2]);
    }
    if (modifier_flags & 0x88) { /* LGUI | RGUI */
        pos += snprintf(stbuf_mod + pos, sizeof(stbuf_mod) - pos, "%s", mod_symbols[3]);
    }

    lv_label_set_text_static(mod_label, stbuf_mod);
}

/* ========== Create ========== */

static void yads2_create_top_row(lv_obj_t *parent) {
    /* WPM value (top left) */
    wpm_value_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wpm_value_label, &FR_Medium_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(wpm_value_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(wpm_value_label, YADS2_WPM_VALUE_X, YADS2_WPM_VALUE_Y);
    lv_label_set_text_static(wpm_value_label, stbuf_wpm);

    wpm_caption_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wpm_caption_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(wpm_caption_label, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_pos(wpm_caption_label, YADS2_WPM_CAPTION_X, YADS2_WPM_CAPTION_Y);
    lv_label_set_text_static(wpm_caption_label, "WPM");

    /* Keyboard name (top centre) */
    name_label = lv_label_create(parent);
    lv_obj_set_style_text_font(name_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(name_label, YADS2_NAME_WIDTH);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, YADS2_NAME_Y);
    lv_label_set_text_static(name_label, stbuf_name);

    /* Output status (top right) - "> USB" / "> BLE n", recolored per transport */
    usb_label = lv_label_create(parent);
    lv_obj_set_style_text_font(usb_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(usb_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_recolor(usb_label, true);
    lv_obj_align(usb_label, LV_ALIGN_TOP_RIGHT, YADS2_OUTPUT_X_OFFSET, YADS2_OUTPUT_USB_Y);
    lv_label_set_text_static(usb_label, " ");

    ble_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ble_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(ble_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_recolor(ble_label, true);
    lv_obj_align(ble_label, LV_ALIGN_TOP_RIGHT, YADS2_OUTPUT_X_OFFSET, YADS2_OUTPUT_BLE_Y);
    lv_label_set_text_static(ble_label, "BLE");
}

static void yads2_create_center(lv_obj_t *parent) {
    /* Large centred layer name */
    layer_label = lv_label_create(parent);
    lv_obj_set_style_text_font(layer_label, &DINishExpanded_Light_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(layer_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, YADS2_LAYER_Y);
    lv_label_set_text_static(layer_label, stbuf_layer);

    /* NerdFont modifier row underneath the layer name */
    mod_label = lv_label_create(parent);
    lv_obj_set_style_text_font(mod_label, &NerdFonts_Regular_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(mod_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(mod_label, LV_ALIGN_CENTER, 0, YADS2_MOD_Y);
    lv_label_set_text_static(mod_label, "");
}

/* Battery bars along the bottom edge (one slot per keyboard/half) */
static void yads2_create_battery_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    battery_row = row;
    lv_obj_set_size(row, YADS2_BATTERY_ROW_WIDTH, YADS2_BATTERY_ROW_HEIGHT);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, YADS2_BATTERY_ROW_Y_OFFSET);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < YADS2_MAX_BATTERIES; i++) {
        struct yads2_battery_slot *slot = &battery_slots[i];

        /* Percentage above the bar */
        slot->pct = lv_label_create(row);
        lv_obj_set_style_text_font(slot->pct, &FG_Medium_21, LV_PART_MAIN);
        lv_obj_set_style_text_color(slot->pct, lv_color_hex(YADS2_COLOR_BATTERY_OK), LV_PART_MAIN);
        lv_obj_set_style_text_align(slot->pct, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text_static(slot->pct, stbuf_battery[i]);

        /* Horizontal bar (upstream YADS battery gauge style) */
        slot->bar = lv_bar_create(row);
        lv_obj_set_size(slot->bar, YADS2_BATTERY_BAR_MAX_WIDTH, YADS2_BATTERY_BAR_HEIGHT);
        lv_bar_set_range(slot->bar, 0, 100);
        lv_bar_set_value(slot->bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_radius(slot->bar, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(slot->bar, 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slot->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slot->bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slot->bar, lv_color_hex(YADS2_COLOR_BAR_TRACK), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slot->bar, lv_color_hex(YADS2_COLOR_BATTERY_OK),
                                  LV_PART_INDICATOR);

        /* Source name underneath the bar (L / R / Aux / A1 / A2) */
        slot->name = lv_label_create(row);
        lv_obj_set_style_text_font(slot->name, &FG_Medium_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(slot->name, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
        lv_obj_set_style_text_align(slot->name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(slot->name, "");
    }

    yads2_apply_battery_layout(1);
}

/* ========== Public API ========== */

lv_obj_t *yads2_layout_create(lv_obj_t *parent) {
    if (layout_created) {
        LOG_WRN("YADS2 layout already created");
        return parent;
    }
    if (!parent) {
        return NULL;
    }

    layout_container = parent;

    /* Black screen, same as the upstream YADS status screen */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    yads2_create_top_row(parent);
    yads2_create_center(parent);
    yads2_create_battery_row(parent);

    /* Force a full refresh on the first update */
    cached_valid = false;

    layout_created = true;
    LOG_INF("YADS2 layout created");
    return parent;
}

void yads2_layout_update(uint8_t active_layer, const char *layer_name,
                         uint8_t battery_level, bool battery_connected,
                         const uint8_t peripheral_battery[YADS2_MAX_PERIPHERALS],
                         const bool peripheral_connected[YADS2_MAX_PERIPHERALS],
                         uint8_t wpm, uint8_t modifier_flags,
                         bool usb_connected, uint8_t ble_profile,
                         bool ble_connected, bool ble_bonded,
                         const char *keyboard_name) {
    if (!layout_created) {
        return;
    }

    const char *name = (keyboard_name != NULL) ? keyboard_name : "";
    bool have_keyboard = (name[0] != '\0');
    const char *layer = (layer_name != NULL) ? layer_name : "";

    /* Battery slots: slot 0 = keyboard, followed by every peripheral that
     * advertises data. Without a detected keyboard a single placeholder slot
     * is kept so that the bottom row does not shift around while scanning. */
    int count = 1;
    if (have_keyboard) {
        for (int i = 0; i < YADS2_MAX_PERIPHERALS; i++) {
            if (peripheral_connected[i] || peripheral_battery[i] > 0) {
                count = i + 2;
            }
        }
    }

    if (count != battery_slot_count) {
        yads2_apply_battery_layout(count);
        cached_valid = false; /* re-render texts/colors of the resized row */
    }

    if (battery_row) {
        if (have_keyboard) {
            lv_obj_clear_flag(battery_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(battery_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Keyboard name */
    if (!cached_valid || strncmp(name, cached_keyboard_name, sizeof(cached_keyboard_name)) != 0) {
        yads2_update_name(name);
        snprintf(cached_keyboard_name, sizeof(cached_keyboard_name), "%s", name);
    }

    /* Layer */
    if (!cached_valid || active_layer != cached_layer ||
        strncmp(layer, cached_layer_name, sizeof(cached_layer_name)) != 0) {
        yads2_update_layer(active_layer, layer);
        cached_layer = active_layer;
        snprintf(cached_layer_name, sizeof(cached_layer_name), "%s", layer);
    }

    /* WPM */
    if (!cached_valid || wpm != cached_wpm) {
        yads2_update_wpm(wpm);
        cached_wpm = wpm;
    }

    /* Modifiers */
    if (!cached_valid || modifier_flags != cached_mods) {
        yads2_update_modifiers(modifier_flags);
        cached_mods = modifier_flags;
    }

    /* Output status */
    if (!cached_valid || usb_connected != cached_usb_connected ||
        ble_connected != cached_ble_connected || ble_bonded != cached_ble_bonded ||
        ble_profile != cached_ble_profile) {
        yads2_update_output(usb_connected, ble_connected, ble_bonded, ble_profile);
        cached_usb_connected = usb_connected;
        cached_ble_connected = ble_connected;
        cached_ble_bonded = ble_bonded;
        cached_ble_profile = ble_profile;
    }

    /* Batteries */
    bool battery_changed = !cached_valid || battery_level != cached_battery_level ||
                           battery_connected != cached_battery_connected;
    for (int i = 0; i < YADS2_MAX_PERIPHERALS; i++) {
        if (peripheral_battery[i] != cached_peripheral_battery[i] ||
            peripheral_connected[i] != cached_peripheral_connected[i]) {
            battery_changed = true;
        }
    }

    if (battery_changed) {
        yads2_set_battery_slot(0, battery_level, battery_connected);
        for (int i = 0; i < YADS2_MAX_PERIPHERALS; i++) {
            yads2_set_battery_slot(i + 1, peripheral_battery[i], peripheral_connected[i]);
            cached_peripheral_battery[i] = peripheral_battery[i];
            cached_peripheral_connected[i] = peripheral_connected[i];
        }
        cached_battery_level = battery_level;
        cached_battery_connected = battery_connected;
    }

    cached_valid = true;
}

void yads2_layout_destroy(void) {
    if (!layout_created) {
        return;
    }

    /* Every widget lives directly on the screen / battery row */
    lv_obj_t *objects[] = {battery_row, wpm_value_label, wpm_caption_label, name_label,
                           usb_label,   ble_label,       layer_label,       mod_label};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (objects[i]) {
            lv_obj_del(objects[i]);
        }
    }

    battery_row = NULL;
    wpm_value_label = NULL;
    wpm_caption_label = NULL;
    name_label = NULL;
    usb_label = NULL;
    ble_label = NULL;
    layer_label = NULL;
    mod_label = NULL;
    memset(battery_slots, 0, sizeof(battery_slots));

    layout_container = NULL;
    layout_created = false;
    battery_slot_count = 0;
    cached_valid = false;
    cached_keyboard_name[0] = '\0';
    cached_layer_name[0] = '\0';

    LOG_INF("YADS2 layout destroyed");
}





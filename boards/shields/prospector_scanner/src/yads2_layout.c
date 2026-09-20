/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * YADS2 Layout for Scanner Mode
 *
 * Second YADS-style screen, arranged for the scanner:
 * - Left/right peripheral (hand) connection status in the top corners
 *   (left corner = left half, right corner = right half)
 * - Keyboard name at the top centre
 * - Output status (USB / BLE) right aligned below the right-hand status
 * - Active layer in the middle, NerdFont modifier icons underneath
 * - Battery level per half along the bottom edge
 *
 * The widget arrangement is derived from the original YADS status screen,
 * janpfischer/zmk-dongle-screen (MIT License):
 * https://github.com/janpfischer/zmk-dongle-screen/tree/main/boards/shields/dongle_screen
 * WPM is intentionally not shown on this layout.
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
#include <zmk/keymap.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(yads2_layout, CONFIG_ZMK_LOG_LEVEL);

/* LVGL built-in font used for arbitrary text (keyboard name, peripheral
 * status, output status) and for the LV_SYMBOL_* glyphs.
 * The Carrefinho fonts are glyph subsets - e.g. FG_Medium_20 stops at U+0060
 * and therefore has no lowercase letters, which LVGL would draw as placeholder
 * boxes (CONFIG_LV_USE_FONT_PLACEHOLDER=y). Only use subset fonts for fixed
 * uppercase/digit strings (FG_Medium_21 = "L 85%"). */
LV_FONT_DECLARE(lv_font_montserrat_16);

/* ========== Colors ==========
 * Palette follows the upstream YADS status screen
 * (janpfischer/zmk-dongle-screen): white text by default, colour only on the
 * output status and the battery widgets. */
#define YADS2_COLOR_TEXT          0xFFFFFF
#define YADS2_COLOR_DIM           0x7B7D93
#define YADS2_COLOR_USB_READY     0xFFFFFF /* upstream: white when USB HID ready */
#define YADS2_COLOR_USB_IDLE      0x7B7D93 /* upstream: 0xFF0000 (red when not ready).
                                            * Dimmed here because on the scanner
                                            * "not ready" is the normal state
                                            * while the keyboard talks BLE. */
#define YADS2_COLOR_BLE_CONNECTED 0x00FF00 /* upstream: 0x00FF00 */
#define YADS2_COLOR_BLE_BONDED    0x4A90E2 /* upstream: 0x0000FF (softer blue kept
                                            * for legibility on this panel) */
#define YADS2_COLOR_BLE_OPEN      0xFFFFFF /* upstream: white (profile free) */
#define YADS2_COLOR_BATTERY_OK    0xFFFFFF /* upstream: white */
#define YADS2_COLOR_BATTERY_LOW   0xFFC000 /* upstream: LV_PALETTE_YELLOW */
#define YADS2_COLOR_BATTERY_OFF   0xE63030 /* upstream: LV_PALETTE_RED */
#define YADS2_COLOR_BAR_TRACK     0x202020
#define YADS2_COLOR_BAR_LOW_TRACK 0x584028
#define YADS2_COLOR_BAR_OFF_TRACK 0x5A2020

/* Level at or below which the battery turns yellow (upstream YADS: <= 10%) */
#define YADS2_LOW_BATTERY_THRESHOLD 10

/* ========== Geometry (280x240 coordinate space) ========== */
/* Top row: left peripheral status (left corner), keyboard name (centre),
 * right peripheral status (right corner) */
#define YADS2_NAME_Y 8
#define YADS2_NAME_WIDTH 120
#define YADS2_PEER_LEFT_X 10
#define YADS2_PEER_RIGHT_X_OFFSET (-10)
#define YADS2_PEER_STATUS_Y 6
/* Output status (USB / BLE) - right aligned below the right-hand status */
#define YADS2_OUTPUT_X_OFFSET (-10)
#define YADS2_OUTPUT_USB_Y 34
#define YADS2_OUTPUT_BLE_Y 54

/* Centre: list of all keymap layer names, the current one highlighted.
 * The names are read from this firmware's own keymap via
 * zmk_keymap_layer_name() - locally, so the full display-name is available
 * (the status advertisement only carries 4 characters of a layer name).
 * Default (no update yet): the first layer is highlighted.
 * yads2_layout_set_layer() scrolls/highlights the current one. */
#define YADS2_LAYER_MAX_ROWS 4
#define YADS2_LAYER_ROW_HEIGHT 22
#define YADS2_LAYER_LIST_TOP_Y 66
#define YADS2_LAYER_MARKER_ACTIVE "> "
#define YADS2_LAYER_MARKER_IDLE "  "

/* NerdFont modifier row, underneath the layer list */
#define YADS2_MOD_Y 156

/* Bottom: battery row (always visible - placeholder 50% until data arrives)
 * One entry per keyboard/half: "<L|R> <level>%" line with a gauge bar below.
 * The keyboard publishes battery_level = LEFT half and peripheral_battery[0] =
 * RIGHT half (see status_advertisement.c), so a split keyboard shows two
 * entries labelled L and R. */
#define YADS2_BATTERY_ROW_WIDTH 268
#define YADS2_BATTERY_ROW_HEIGHT 44
#define YADS2_BATTERY_ROW_Y_OFFSET (-2)
#define YADS2_BATTERY_LABEL_Y 0
#define YADS2_BATTERY_BAR_Y 26
#define YADS2_BATTERY_BAR_HEIGHT 10
#define YADS2_BATTERY_BAR_MAX_WIDTH 130
#define YADS2_BATTERY_BAR_MIN_WIDTH 52
#define YADS2_BATTERY_BAR_GAP 18
/* Placeholder level shown for a slot that has not reported a level yet.
 * Set to 0 to fall back to the "--" style instead. */
#define YADS2_BATTERY_PLACEHOLDER_LEVEL 50

/* Slots previewed (and filled with the placeholder) while no keyboard data is
 * available yet - 2 = split keyboard look (L + R). */
#define YADS2_BATTERY_DEFAULT_SLOTS 2

/* Slots narrower than this drop the '%' to keep "L 85" readable */
#define YADS2_BATTERY_LABEL_NARROW_WIDTH 80

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
static char stbuf_layer_rows[YADS2_LAYER_MAX_ROWS][24] = {{""}, {""}, {""}, {""}};
static char stbuf_name[24] = "Receiver...";
static char stbuf_peer[2][8] = {{""}, {""}};
static char stbuf_usb[24] = "";
static char stbuf_ble[24] = "";
static char stbuf_mod[64] = "";
static char stbuf_battery[YADS2_MAX_BATTERIES][12] = {{"--"}, {"--"}, {"--"}, {"--"}};

/* ========== Widget state ========== */
struct yads2_battery_slot {
    lv_obj_t *label; /* "<L|R> <level>%" */
    lv_obj_t *bar;   /* gauge filled to the level */
};

static lv_obj_t *layout_container = NULL;
static lv_obj_t *peer_left_label = NULL;
static lv_obj_t *peer_right_label = NULL;
static lv_obj_t *name_label = NULL;
static lv_obj_t *usb_label = NULL;
static lv_obj_t *ble_label = NULL;
static lv_obj_t *layer_rows[YADS2_LAYER_MAX_ROWS] = {NULL};
static lv_obj_t *mod_label = NULL;
static lv_obj_t *battery_row = NULL;
static struct yads2_battery_slot battery_slots[YADS2_MAX_BATTERIES];

/* Per-slot content: label ("L", "R", "Aux", "A1", "A2"), last level and state */
static char slot_names[YADS2_MAX_BATTERIES][6] = {{""}, {""}, {""}, {""}};
static uint8_t slot_levels[YADS2_MAX_BATTERIES] = {0, 0, 0, 0};
static bool slot_connected[YADS2_MAX_BATTERIES] = {false, false, false, false};
static int slot_widths[YADS2_MAX_BATTERIES] = {0, 0, 0, 0};

static bool layout_created = false;
static int battery_slot_count = 0;

/* Layer list state: the names come from this firmware's keymap */
static uint8_t layer_count = 0;        /* number of layers in the keymap */
static uint8_t layer_current = 0;      /* highlighted layer */
static uint8_t layer_window_start = 0; /* first visible row while scrolling */

/* Cached values - updates only touch LVGL when something actually changed */
static bool cached_valid = false;
static bool cached_peer[2] = {false, false};
static uint8_t cached_mods = 0;
static bool cached_usb_connected = false;
static bool cached_ble_connected = false;
static bool cached_ble_bonded = false;
static uint8_t cached_ble_profile = 0;
static char cached_keyboard_name[24] = "";
static uint8_t cached_battery_level = 0;
static bool cached_battery_connected = false;
static uint8_t cached_peripheral_battery[YADS2_MAX_PERIPHERALS] = {0};
static bool cached_peripheral_connected[YADS2_MAX_PERIPHERALS] = {false};

/* Refresh one battery slot (label text + gauge) from the stored values */
static void yads2_render_battery_slot(int slot);

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
        lv_obj_t *objects[2] = {slot->label, slot->bar};

        if (visible) {
            const char *name = (names && names[i]) ? names[i] : "";
            snprintf(slot_names[i], sizeof(slot_names[i]), "%s", name);
            slot_widths[i] = bar_width;
        } else {
            slot_names[i][0] = '\0';
            slot_widths[i] = 0;
        }

        if (slot->label) {
            lv_obj_set_width(slot->label, bar_width);
            lv_obj_set_pos(slot->label, x, YADS2_BATTERY_LABEL_Y);
        }
        if (slot->bar) {
            lv_obj_set_size(slot->bar, bar_width, YADS2_BATTERY_BAR_HEIGHT);
            lv_obj_set_pos(slot->bar, x, YADS2_BATTERY_BAR_Y);
        }

        for (int o = 0; o < 2; o++) {
            if (!objects[o]) {
                continue;
            }
            if (visible) {
                lv_obj_clear_flag(objects[o], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(objects[o], LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (visible) {
            yads2_render_battery_slot(i);
        }
    }

    battery_slot_count = count;
}

/* Compose "<name> <level>%" (or "<name> --" while unknown) and colour one slot */
static void yads2_render_battery_slot(int slot) {
    if (slot < 0 || slot >= YADS2_MAX_BATTERIES) {
        return;
    }

    struct yads2_battery_slot *w = &battery_slots[slot];
    bool have_level = slot_connected[slot] && slot_levels[slot] > 0;
    /* Until a level arrives the placeholder (50% by default) is shown so that
     * the bottom row always looks complete. */
    uint8_t level = have_level ? slot_levels[slot] : (uint8_t)YADS2_BATTERY_PLACEHOLDER_LEVEL;
    bool low = level > 0 && level <= YADS2_LOW_BATTERY_THRESHOLD;

    uint32_t text_color, fill_color, track_color;
    if (level == 0) {
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

    if (w->label) {
        /* Drop the '%' in narrow slots so the line cannot be clipped */
        bool with_pct = slot_widths[slot] >= YADS2_BATTERY_LABEL_NARROW_WIDTH;

        if (level > 0) {
            if (slot_names[slot][0]) {
                snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]),
                         with_pct ? "%s %u%%" : "%s %u", slot_names[slot], level);
            } else {
                snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]),
                         with_pct ? "%u%%" : "%u", level);
            }
        } else if (slot_names[slot][0]) {
            snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]), "%s --",
                     slot_names[slot]);
        } else {
            snprintf(stbuf_battery[slot], sizeof(stbuf_battery[slot]), "--");
        }
        lv_label_set_text_static(w->label, stbuf_battery[slot]);
        lv_obj_set_style_text_color(w->label, lv_color_hex(text_color), LV_PART_MAIN);
    }

    if (w->bar) {
        lv_bar_set_value(w->bar, level, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(track_color), LV_PART_MAIN);
        lv_obj_set_style_bg_color(w->bar, lv_color_hex(fill_color), LV_PART_INDICATOR);
    }
}

/* Store the latest level of one slot and refresh it */
static void yads2_set_battery_slot(int slot, uint8_t level, bool connected) {
    if (slot < 0 || slot >= YADS2_MAX_BATTERIES) {
        return;
    }

    slot_levels[slot] = level;
    slot_connected[slot] = connected;
    yads2_render_battery_slot(slot);
}

/* ========== Output status (top right) ========== */

static void yads2_update_output(bool usb_connected, bool ble_connected, bool ble_bonded,
                                uint8_t ble_profile) {
    if (!usb_label || !ble_label) {
        return;
    }

    uint32_t usb_color = usb_connected ? YADS2_COLOR_USB_READY : YADS2_COLOR_USB_IDLE;
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

/* ========== Peripheral (hand) status / keyboard name / layer / modifiers ========== */

/* Left/right peripheral (hand) connection status shown in the top corners.
 * The keyboard publishes the left half as battery_level and the right half as
 * peripheral_battery[0]; ZMK reports level < 1 once a half disconnects, so a
 * valid level counts as "connected" (same rule as the upstream YADS battery
 * widget). */
static void yads2_update_peer_status(bool left_ok, bool right_ok) {
    lv_obj_t *labels[2] = {peer_left_label, peer_right_label};
    const bool ok[2] = {left_ok, right_ok};
    const char *prefix[2] = {"L ", "R "};

    for (int i = 0; i < 2; i++) {
        if (!labels[i]) {
            continue;
        }

        snprintf(stbuf_peer[i], sizeof(stbuf_peer[i]), "%s%s", prefix[i],
                 ok[i] ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_label_set_text_static(labels[i], stbuf_peer[i]);
        lv_obj_set_style_text_color(labels[i],
                                    lv_color_hex(ok[i] ? YADS2_COLOR_BLE_CONNECTED
                                                       : YADS2_COLOR_BATTERY_OFF),
                                    LV_PART_MAIN);
    }
}

static void yads2_update_name(const char *keyboard_name) {
    if (!name_label) {
        return;
    }

    if (keyboard_name && keyboard_name[0]) {
        snprintf(stbuf_name, sizeof(stbuf_name), "%s", keyboard_name);
        lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    } else {
        snprintf(stbuf_name, sizeof(stbuf_name), "Receiver...");
        lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
    }
    lv_label_set_text_static(name_label, stbuf_name);
}

/* ========== Layer list (names read from this firmware's keymap) ========== */

/* Full name of one keymap layer. Falls back to the layer number when the keymap
 * entry has no display-name (same rule as the upstream YADS layer widget). */
static void yads2_layer_name(uint8_t index, char *out, size_t out_len) {
    const char *name = (index < layer_count) ? zmk_keymap_layer_name(index) : NULL;

    if (name != NULL && name[0] != '\0') {
        snprintf(out, out_len, "%s", name);
    } else {
        snprintf(out, out_len, "%u", (unsigned int)index);
    }
}

/* Draw the visible slice of the layer list, scrolling so that the highlighted
 * layer always stays on screen */
static void yads2_render_layer_rows(void) {
    if (layer_count == 0 || layer_rows[0] == NULL) {
        return;
    }

    uint8_t visible = (layer_count < YADS2_LAYER_MAX_ROWS) ? layer_count
                                                           : (uint8_t)YADS2_LAYER_MAX_ROWS;

    if (layer_current < layer_window_start) {
        layer_window_start = layer_current;
    } else if (layer_current >= (uint8_t)(layer_window_start + visible)) {
        layer_window_start = (uint8_t)(layer_current - visible + 1);
    }

    for (uint8_t row = 0; row < YADS2_LAYER_MAX_ROWS; row++) {
        lv_obj_t *label = layer_rows[row];
        if (label == NULL) {
            continue;
        }

        uint8_t index = (uint8_t)(layer_window_start + row);
        if (row >= visible || index >= layer_count) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        char name[20];
        bool current = (index == layer_current);

        yads2_layer_name(index, name, sizeof(name));
        snprintf(stbuf_layer_rows[row], sizeof(stbuf_layer_rows[row]), "%s%s",
                 current ? YADS2_LAYER_MARKER_ACTIVE : YADS2_LAYER_MARKER_IDLE, name);

        lv_label_set_text_static(label, stbuf_layer_rows[row]);
        lv_obj_set_style_text_color(label,
                                    lv_color_hex(current ? YADS2_COLOR_TEXT : YADS2_COLOR_DIM),
                                    LV_PART_MAIN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

void yads2_layout_set_layer(uint8_t index) {
    if (!layout_created || layer_count == 0) {
        return;
    }

    if (index >= layer_count) {
        index = (uint8_t)(layer_count - 1);
    }

    layer_current = index;
    yads2_render_layer_rows();
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
    /* Left/right peripheral status (top corners). The position alone identifies
     * the half: left corner = left hand, right corner = right hand. */
    peer_left_label = lv_label_create(parent);
    lv_obj_set_style_text_font(peer_left_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(peer_left_label, lv_color_hex(YADS2_COLOR_BATTERY_OFF),
                                LV_PART_MAIN);
    lv_obj_set_pos(peer_left_label, YADS2_PEER_LEFT_X, YADS2_PEER_STATUS_Y);
    lv_label_set_text_static(peer_left_label, "L " LV_SYMBOL_CLOSE);

    peer_right_label = lv_label_create(parent);
    lv_obj_set_style_text_font(peer_right_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(peer_right_label, lv_color_hex(YADS2_COLOR_BATTERY_OFF),
                                LV_PART_MAIN);
    lv_obj_align(peer_right_label, LV_ALIGN_TOP_RIGHT, YADS2_PEER_RIGHT_X_OFFSET,
                 YADS2_PEER_STATUS_Y);
    lv_label_set_text_static(peer_right_label, "R " LV_SYMBOL_CLOSE);

    /* Keyboard name (top centre) - built-in font: the name is arbitrary text
     * and the subset fonts do not cover all letters */
    name_label = lv_label_create(parent);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(YADS2_COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(name_label, YADS2_NAME_WIDTH);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, YADS2_NAME_Y);
    lv_label_set_text_static(name_label, stbuf_name);

    /* Output status (below the right-hand status) - "> USB" / "> BLE n",
     * recolored per transport */
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
    /* One row per keymap layer; the text and highlight are filled in by
     * yads2_layout_set_layer() */
    for (int row = 0; row < YADS2_LAYER_MAX_ROWS; row++) {
        layer_rows[row] = lv_label_create(parent);
        lv_obj_set_style_text_font(layer_rows[row], &FG_Medium_21, LV_PART_MAIN);
        lv_obj_set_style_text_color(layer_rows[row], lv_color_hex(YADS2_COLOR_DIM),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_align(layer_rows[row], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(layer_rows[row], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(layer_rows[row], 250);
        lv_obj_align(layer_rows[row], LV_ALIGN_TOP_MID, 0,
                     YADS2_LAYER_LIST_TOP_Y + row * YADS2_LAYER_ROW_HEIGHT);
        lv_obj_add_flag(layer_rows[row], LV_OBJ_FLAG_HIDDEN);
    }

    /* NerdFont modifier row underneath the layer list */
    mod_label = lv_label_create(parent);
    lv_obj_set_style_text_font(mod_label, &NerdFonts_Regular_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(mod_label, lv_color_hex(YADS2_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(mod_label, LV_ALIGN_TOP_MID, 0, YADS2_MOD_Y);
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

        /* "<L|R> <level>%" line above the gauge (one line per half/keyboard) */
        slot->label = lv_label_create(row);
        lv_obj_set_style_text_font(slot->label, &FG_Medium_21, LV_PART_MAIN);
        lv_obj_set_style_text_color(slot->label, lv_color_hex(YADS2_COLOR_BATTERY_OK),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_align(slot->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text_static(slot->label, stbuf_battery[i]);

        /* Gauge filled to the reported level (upstream YADS battery gauge) */
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
    }

    /* Preview the split layout (L + R) until real data arrives */
    yads2_apply_battery_layout(YADS2_BATTERY_DEFAULT_SLOTS);
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

    /* Layer list: the names come from this firmware's keymap; the first layer
     * is highlighted until the current layer is supplied through
     * yads2_layout_set_layer() */
    layer_count = (uint8_t)ZMK_KEYMAP_LAYERS_LEN;
    layer_current = 0;
    layer_window_start = 0;

    layout_created = true;
    yads2_layout_set_layer(0);

    LOG_INF("YADS2 layout created (%u keymap layers)", (unsigned int)layer_count);
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

    ARG_UNUSED(wpm);          /* WPM is intentionally not shown in this layout */
    ARG_UNUSED(active_layer); /* layer list is driven by yads2_layout_set_layer() */
    ARG_UNUSED(layer_name);   /* layer names come from this firmware's own keymap */

    const char *name = (keyboard_name != NULL) ? keyboard_name : "";
    bool have_keyboard = (name[0] != '\0');

    /* Battery slots: slot 0 = keyboard, followed by every peripheral that
     * advertises data. While no keyboard has been detected yet the split
     * layout (L + R) is previewed with the placeholder level so the bottom row
     * already shows what a connected keyboard will look like. */
    int count = YADS2_BATTERY_DEFAULT_SLOTS;
    if (have_keyboard) {
        count = 1;
        for (int i = 0; i < YADS2_MAX_PERIPHERALS; i++) {
            if (peripheral_connected[i] || peripheral_battery[i] > 0) {
                count = i + 2;
            }
        }
        if (count < 1) {
            count = 1;
        }
    }

    if (count != battery_slot_count) {
        yads2_apply_battery_layout(count);
        cached_valid = false; /* re-render texts/colors of the resized row */
    }

    /* The battery row stays visible even before a keyboard is detected: slots
     * without data show the placeholder level (50%) instead of an empty half. */

    /* Keyboard name */
    if (!cached_valid || strncmp(name, cached_keyboard_name, sizeof(cached_keyboard_name)) != 0) {
        yads2_update_name(name);
        snprintf(cached_keyboard_name, sizeof(cached_keyboard_name), "%s", name);
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

    /* Peripheral (hand) connection status for the two top corners. A half counts
     * as connected when it reports a level; while nothing has been received yet
     * (and the battery placeholder is active) the preview state is shown so the
     * layout looks complete. */
    bool peer_ok[2];
    peer_ok[0] = slot_connected[0] && slot_levels[0] > 0;
    peer_ok[1] = slot_connected[1] && slot_levels[1] > 0;
    if (!have_keyboard && (YADS2_BATTERY_PLACEHOLDER_LEVEL > 0)) {
        peer_ok[0] = true;
        peer_ok[1] = true;
    }

    if (!cached_valid || peer_ok[0] != cached_peer[0] || peer_ok[1] != cached_peer[1]) {
        yads2_update_peer_status(peer_ok[0], peer_ok[1]);
        cached_peer[0] = peer_ok[0];
        cached_peer[1] = peer_ok[1];
    }

    cached_valid = true;
}

void yads2_layout_destroy(void) {
    if (!layout_created) {
        return;
    }

    /* Every widget lives directly on the screen / battery row */
    lv_obj_t *objects[] = {battery_row, peer_left_label, peer_right_label, name_label,
                           usb_label,   ble_label,       mod_label};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (objects[i]) {
            lv_obj_del(objects[i]);
        }
    }

    for (int row = 0; row < YADS2_LAYER_MAX_ROWS; row++) {
        if (layer_rows[row]) {
            lv_obj_del(layer_rows[row]);
            layer_rows[row] = NULL;
        }
    }

    battery_row = NULL;
    peer_left_label = NULL;
    peer_right_label = NULL;
    name_label = NULL;
    usb_label = NULL;
    ble_label = NULL;
    mod_label = NULL;
    memset(battery_slots, 0, sizeof(battery_slots));
    memset(slot_levels, 0, sizeof(slot_levels));
    memset(slot_connected, 0, sizeof(slot_connected));
    memset(cached_peer, 0, sizeof(cached_peer));
    for (int i = 0; i < YADS2_MAX_BATTERIES; i++) {
        slot_names[i][0] = '\0';
        snprintf(stbuf_battery[i], sizeof(stbuf_battery[i]), "--");
    }

    layout_container = NULL;
    layout_created = false;
    battery_slot_count = 0;
    cached_valid = false;
    cached_keyboard_name[0] = '\0';
    layer_count = 0;
    layer_current = 0;
    layer_window_start = 0;

    LOG_INF("YADS2 layout destroyed");
}





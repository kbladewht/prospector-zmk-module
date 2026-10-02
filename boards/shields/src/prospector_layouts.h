/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * YADS2 scanner display interface.
 */

#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Keyboard data for display (from Periodic ADV)
 */
struct prospector_keyboard_data {
    /* Dynamic data (from dynamic packet) */
    uint8_t active_layer;
    char current_layer_name[8];
    uint8_t modifier_flags;
    uint8_t wpm_value;
    uint8_t battery_level;
    uint8_t peripheral_battery[3];
    uint8_t profile_slot;
    uint8_t connection_count;
    uint8_t indicator_flags;
    bool ble_connected;
    bool ble_bonded;
    bool usb_connected;

    /* Static data (from static packet) */
    char keyboard_name[24];
    uint8_t layer_count;
    char layer_names[10][8];
    int8_t peripheral_rssi[3];

    /* Validity flags */
    bool has_dynamic_data;
    bool has_static_data;
};

/**
 * @brief Initialize prospector layouts module
 * @param parent Parent LVGL object for layouts
 */
void prospector_layouts_init(lv_obj_t *parent);

/**
 * @brief Destroy prospector layouts and free resources
 */
void prospector_layouts_destroy(void);

/**
 * @brief Update display with new keyboard data
 * @param data Keyboard data from Periodic ADV
 */
void prospector_layouts_update(const struct prospector_keyboard_data *data);

#ifdef __cplusplus
}
#endif

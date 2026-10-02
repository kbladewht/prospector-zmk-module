/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * YADS2 scanner display data adapter.
 */

#include "prospector_layouts.h"
#include "yads2_layout.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(prospector_layouts, CONFIG_ZMK_LOG_LEVEL);

static bool initialized = false;

/* Keep a short grace window for recently valid peripheral readings so a transient
 * 0%/no-report sample does not instantly flip a still-live split half into
 * "disconnected" on the screen. */
static uint8_t last_good_peripheral_battery[YADS2_MAX_PERIPHERALS] = {0};
static bool last_good_peripheral_connected[YADS2_MAX_PERIPHERALS] = {false};
static uint32_t last_good_peripheral_time[YADS2_MAX_PERIPHERALS] = {0};
#define PROSPECTOR_PERIPHERAL_GRACE_MS 4000U

void prospector_layouts_init(lv_obj_t *parent) {
    if (initialized || parent == NULL) {
        LOG_WRN("Prospector layouts already initialized");
        return;
    }

    yads2_layout_create(parent);
    initialized = true;
    LOG_INF("YADS2 layout initialized");
}

void prospector_layouts_destroy(void) {
    if (!initialized) {
        return;
    }

    yads2_layout_destroy();
    initialized = false;
    LOG_INF("YADS2 layout destroyed");
}

void prospector_layouts_update(const struct prospector_keyboard_data *data) {
    if (!initialized || data == NULL) {
        return;
    }

    uint8_t active_layer = data->active_layer;
    const char *layer_name = data->current_layer_name[0] ? data->current_layer_name : "BASE";
    uint8_t battery_level = data->battery_level;
    bool battery_connected = data->has_dynamic_data && battery_level > 0;

    uint8_t peripheral_battery[YADS2_MAX_PERIPHERALS];
    bool peripheral_connected[YADS2_MAX_PERIPHERALS];
    const uint32_t now_ms = k_uptime_get_32();
    for (int i = 0; i < YADS2_MAX_PERIPHERALS; i++) {
        peripheral_battery[i] = data->peripheral_battery[i];
        peripheral_connected[i] = data->has_dynamic_data && peripheral_battery[i] > 0;

        if (!peripheral_connected[i] && last_good_peripheral_connected[i] &&
            (now_ms - last_good_peripheral_time[i]) < PROSPECTOR_PERIPHERAL_GRACE_MS) {
            peripheral_connected[i] = true;
            peripheral_battery[i] = last_good_peripheral_battery[i];
        }

        if (peripheral_connected[i] && peripheral_battery[i] > 0) {
            last_good_peripheral_battery[i] = peripheral_battery[i];
            last_good_peripheral_connected[i] = true;
            last_good_peripheral_time[i] = now_ms;
        }
    }

    yads2_layout_update(active_layer, layer_name, battery_level, battery_connected,
                        peripheral_battery, peripheral_connected, data->wpm_value,
                        data->modifier_flags, data->usb_connected, data->profile_slot,
                        data->ble_connected, data->ble_bonded, data->keyboard_name);
}

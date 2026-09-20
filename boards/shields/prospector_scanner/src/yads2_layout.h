/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * YADS2 Layout for Scanner Mode
 *
 * A second YADS-style layout, modelled on the original YADS status screen from
 * janpfischer/zmk-dongle-screen (MIT License):
 * https://github.com/janpfischer/zmk-dongle-screen/tree/main/boards/shields/dongle_screen
 *
 * Widget arrangement (adapted to the 280x240 scanner panel and to scan-mode
 * data coming from the keyboard's BLE advertisement):
 * - Left/right peripheral (hand) connection status in the top corners
 *   (left corner = left half, right corner = right half; green tick =
 *   connected, red cross = not connected)
 * - Keyboard name (top centre)
 * - Output status (USB / BLE) right aligned below the right-hand status
 * - Active layer (centre, large)
 * - Modifier icons (NerdFont row below the layer name)
 * - Battery level per half along the bottom edge
 *
 * WPM is intentionally not shown on this layout.
 *
 * Unlike the Classic (YADS) screen this layout is self-contained: it does not
 * touch the loose widget set in custom_status_screen.c but follows the
 * create/update/destroy contract of the other Prospector Display layouts.
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

/* Battery row capacity: slot 0 = keyboard (central), slots 1..3 = peripherals */
#define YADS2_MAX_PERIPHERALS 3
#define YADS2_MAX_BATTERIES (YADS2_MAX_PERIPHERALS + 1)

/* YADS2 layout API */
lv_obj_t *yads2_layout_create(lv_obj_t *parent);
void yads2_layout_update(uint8_t active_layer, const char *layer_name,
                         uint8_t battery_level, bool battery_connected,
                         const uint8_t peripheral_battery[YADS2_MAX_PERIPHERALS],
                         const bool peripheral_connected[YADS2_MAX_PERIPHERALS],
                         uint8_t wpm, uint8_t modifier_flags,
                         bool usb_connected, uint8_t ble_profile,
                         bool ble_connected, bool ble_bonded,
                         const char *keyboard_name);
void yads2_layout_destroy(void);

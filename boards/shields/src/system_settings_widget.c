/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "system_settings_widget.h"
#include "display_settings.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ========== Runtime Channel Storage (backed by NVS via display_settings) ==========

static bool runtime_channel_initialized = false;
static uint8_t runtime_scanner_channel = 0;

static void init_runtime_channel(void) {
    if (!runtime_channel_initialized) {
        runtime_scanner_channel = display_settings_get_channel();
        runtime_channel_initialized = true;
        LOG_INF("Runtime channel initialized to %d (from NVS)", runtime_scanner_channel);
    }
}

uint8_t scanner_get_runtime_channel(void) {
    init_runtime_channel();
    return runtime_scanner_channel;
}

void scanner_set_runtime_channel(uint8_t channel) {
    init_runtime_channel();
    runtime_scanner_channel = channel;
    display_settings_set_channel(channel);
    LOG_INF("Scanner channel set to %d (%s, saved to NVS)",
            channel, channel == 0 ? "All" : "Filtered");
}

/* ---------------------------------------------------------------------------
 * 下面是原来给触摸专属“Quick Actions”页用的 LVGL 设置面板（按钮 + 标签）。
 * 触摸功能（CONFIG_PROSPECTOR_TOUCH_ENABLED）已整体移除，该面板没有任何调用
 * 方，LVGL 的 button 控件也已在 prospector_e73.conf 里关掉，所以整块删除。
 * 上面的频道 get/set 函数保留：custom_status_screen.c 仍以强符号链接它们
 * （它自己那份是 __attribute__((weak))）。
 * ------------------------------------------------------------------------- */
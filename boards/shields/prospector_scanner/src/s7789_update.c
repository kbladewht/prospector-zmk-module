/*
 * s7789_update.c - ST7789 状态屏的"本机"数据源（替代原 scanner_core.c）
 *
 * 背景
 * ----
 * 老固件（scanner 模式）通过 status_scanner.c 被动扫描其它键盘广播出来的
 * Prospector 26 字节私有数据，再由 scanner_core.c 记账后交给显示线程。
 * 现在这台设备不再做接收端（模块侧的 status_scanner.c / scanner_core.c /
 * status_advertisement.c 已移除），显示所需数据改为直接读取本机 ZMK 状态。
 *
 * 本文件把原来 scanner_* 的接口按 ble_* 命名重新实现，作为临时（本机）数据源，
 * 字段填充仍沿用原来的 26 字节结构，方便以后重新接回真正的 BLE 广播方案。
 *
 * 与旧接口的对应关系见 s7789_update.h 顶部注释。
 *
 * 注意
 * ----
 * 1. 本机模式没有 RSSI / 速率来源，ble_is_signal_pending() 恒为 false。
 * 2. 只有"槽位 0"（本机自己）这一台设备。
 * 3. 电量（本机电量 + 左右手缓存）都在 s7789_update_battery.c 里，本文件只读缓存：
 *      ble_battery_left  -> battery_level         -> 显示端 "L" 槽位（左手）
 *      ble_battery_right -> peripheral_battery[0] -> 显示端 "R" 槽位（右手）
 *    左右手由 app/src/battery_cb.c 刷新后调用 ble_battery_update() 推过来。
 *    本机（dongle）自己没电池，界面上就只有左右手两格；旧代码里的"接收端电量"
 *    通道（ble_get_pending_battery / ble_scanner_battery_level）默认关闭
 *    （CONFIG_PROSPECTOR_BATTERY_SUPPORT 未启用），这里不涉及。
 * 4. 所有读取函数都在显示线程（LVGL 定时器，100ms 一次）上下文被调用。
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "s7789_update.h"
#include "fault_recovery.h"

#include <zmk/status_advertisement.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/wpm.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 采样周期：显示端定时器 100ms 调用一次，这里限制为最快 100ms 采样一次 */
#define BLE_POLL_INTERVAL_MS 100

/* 显示端用来过滤键盘的运行时频道（定义在 system_settings_widget.c，
 * custom_status_screen.c 里也有一份 weak 兜底实现）。 */
extern uint8_t scanner_get_runtime_channel(void) __attribute__((weak));
extern uint8_t qmk_display_active_layer(void) __attribute__((weak));

/* ========== 内部状态（仅显示线程访问） ========== */

static struct pending_display_data s_snapshot; /* 最近一次发布的快照 */
static volatile bool s_update_pending;         /* 待刷新标志 */
static volatile bool s_force_update;           /* 强制下一次刷新（屏幕切换后） */
static uint32_t s_last_poll_ms;                /* 上次采样时间 */
static int s_selected_keyboard = 0;            /* 本机模式下只有槽位 0 */

volatile int8_t ble_signal_rssi = 0;
volatile int32_t ble_signal_rate_x100 = -1; /* 负值 => 界面显示 "-.--Hz" */

/* ========== 看门狗喂狗（系统工作队列） ========== */

/* 原来 scanner_core.c 用 100ms 的 process_work 喂 fault_recovery.c 的 "core"
 * 看门狗通道；该文件移除后必须由这里接手，否则 30s 超时就会自动重启。
 * 跑在系统工作队列上，因此仍然能检测出系统工作队列卡死。 */
#define BLE_CORE_ALIVE_INTERVAL_MS 250

static void ble_core_alive_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    ble_core_process_alive();

    k_work_reschedule(dwork, K_MSEC(BLE_CORE_ALIVE_INTERVAL_MS));
}

static K_WORK_DELAYABLE_DEFINE(ble_core_alive_work, ble_core_alive_work_handler);

static int s7789_update_init(void) {
    k_work_schedule(&ble_core_alive_work, K_MSEC(BLE_CORE_ALIVE_INTERVAL_MS));
    LOG_INF("本机数据源已就绪（ble_* 接口，scanner 已移除）");
    return 0;
}

SYS_INIT(s7789_update_init, APPLICATION, 96);

/* ========== 本机状态读取 ========== */

/* 本机当前频道：跟随显示端设置，保证频道过滤始终命中本机 */
static uint8_t ble_local_channel(void) {
    if (scanner_get_runtime_channel != NULL) {
        return scanner_get_runtime_channel();
    }
    return 10; /* CHANNEL_ALL：显示全部 */
}

/* 读 HID 修饰键位图（位定义与 ZMK_MOD_FLAG_* 一致） */
static uint8_t ble_local_modifiers(void) {
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    return (report != NULL) ? report->body.modifiers : 0;
}

/* The corne_dongle firmware handles MO() in the embedded QMK keymap. Its
 * layer_state is independent from ZMK's keymap state, so use it for the
 * display whenever the QMK compatibility layer is part of this build. */
static uint8_t ble_local_active_layer(void) {
    if (qmk_display_active_layer != NULL) {
        return qmk_display_active_layer();
    }

    return (uint8_t)zmk_keymap_highest_layer_active();
}


/* 把本机状态填进原来的 26 字节数据结构（字段布局与旧协议保持一致） */
static void ble_fill_adv_data(struct zmk_status_adv_data *d) {
    memset(d, 0, sizeof(*d));

    /* 签名：与旧协议一致，便于以后直接拿去广播 */
    d->manufacturer_id[0] = 0xFF;
    d->manufacturer_id[1] = 0xFF;
    d->service_uuid[0] = 0xAB;
    d->service_uuid[1] = 0xCD;

    d->version = PROSPECTOR_ENCODE_VERSION();

    /* 电量字段统一在下方"左右手电量"处填充（本机自身电量不占键盘电量格子） */

    const uint8_t layer_index = ble_local_active_layer();
    d->active_layer = layer_index;

    uint8_t profile = 0;
    bool ble_connected = false;
    
#if IS_ENABLED(CONFIG_ZMK_BLE)
bool ble_bonded = false;
    profile = (uint8_t)zmk_ble_active_profile_index();
    ble_connected = zmk_ble_active_profile_is_connected();
    ble_bonded = !zmk_ble_active_profile_is_open();
#endif
    d->profile_slot = PROSPECTOR_ENCODE_PROFILE_SLOT(profile);
    d->connection_count = ble_connected ? 1 : 0;

    uint8_t flags = 0;
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_powered()) {
        flags |= ZMK_STATUS_FLAG_USB_CONNECTED;
    }
    if (zmk_usb_is_hid_ready()) {
        flags |= ZMK_STATUS_FLAG_USB_HID_READY;
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (ble_connected) {
        flags |= ZMK_STATUS_FLAG_BLE_CONNECTED;
    }
    if (ble_bonded) {
        flags |= ZMK_STATUS_FLAG_BLE_BONDED;
    }
#endif
    d->status_flags = flags;

    d->device_role = ZMK_DEVICE_ROLE_STANDALONE;
    d->device_index = 0;

    /* 左右手电量：左手 -> battery_level（显示端 "L" 槽位），
     * 右手 -> peripheral_battery[0]（显示端 "R" 槽位）。
     * 只读 s7789_update_battery.c 里那份缓存（app/src/battery_cb.c 定时刷新后
     * 通过 ble_battery_update() 推过来）：显示端不碰槽位 / identifier，
     * 也不去调 app 侧的函数。
     *
     * 26 字节旧协议的电量字段（显示端拉平成 bat[0..3]，本机只填前两格）：
     *   battery_level         -> bat[0]：第 1 格，屏幕上的 L
     *   peripheral_battery[0] -> bat[1]：第 2 格，屏幕上的 R
     * 旧协议里代表第 3、4 个键盘设备的 peripheral_battery[1] / [2] 本机用不到，
     * 这里不赋值（memset 已清零，显示端按 0 = 无数据处理）。
     * 本机（dongle）自己没电池、也只有左右手两台设备：界面上就是 L、R 两格。
     */
    d->battery_level = ble_battery_left;
    d->peripheral_battery[0] = ble_battery_right;

    /* The display layout renders the complete local layer name. This short
     * field is retained for layouts that use the legacy advertisement data. */
    const char *lname = zmk_keymap_layer_name(layer_index);
    memset(d->layer_name, ' ', sizeof(d->layer_name));
    if (lname != NULL) {
        for (size_t i = 0; i < sizeof(d->layer_name) && lname[i] != '\0'; i++) {
            d->layer_name[i] = lname[i];
        }
    }

    /* 键盘 ID：取键盘名前 4 字节（与模块在无 HWINFO 时的做法一致） */
    const char *kname = CONFIG_ZMK_KEYBOARD_NAME;
    for (size_t i = 0; i < sizeof(d->keyboard_id) && kname[i] != '\0'; i++) {
        d->keyboard_id[i] = (uint8_t)kname[i];
    }

    d->modifier_flags = ble_local_modifiers();

#if IS_ENABLED(CONFIG_ZMK_WPM)
    const int wpm = zmk_wpm_get_state();
    d->wpm_value = (wpm > 0 && wpm <= 255) ? (uint8_t)wpm : 0;
#else
    d->wpm_value = 0;
#endif

    d->channel = ble_local_channel();
}

/* 采样本机状态；有变化时更新快照并返回 true */
static bool ble_poll_local_state(void) {
    struct pending_display_data next;

    /* 先整体清零，后面 memcmp 比较才不会被填充字节干扰 */
    memset(&next, 0, sizeof(next));

    snprintf(next.device_name, sizeof(next.device_name), "%s", CONFIG_ZMK_KEYBOARD_NAME);

    struct zmk_status_adv_data adv;
    ble_fill_adv_data(&adv);

    next.layer = adv.active_layer;
    memcpy(next.layer_name, adv.layer_name, sizeof(adv.layer_name));
    next.wpm = adv.wpm_value;
    next.modifiers = adv.modifier_flags;
    /* 本机只有左右手两台设备：bat[0]/bat[1] 有值，bat[2]/bat[3] 保持 0（前面已 memset） */
    next.bat[0] = adv.battery_level;
    next.bat[1] = adv.peripheral_battery[0];
    next.usb_ready = (adv.status_flags & ZMK_STATUS_FLAG_USB_HID_READY) != 0;
    next.ble_connected = (adv.status_flags & ZMK_STATUS_FLAG_BLE_CONNECTED) != 0;
    next.ble_bonded = (adv.status_flags & ZMK_STATUS_FLAG_BLE_BONDED) != 0;
    next.profile = PROSPECTOR_DECODE_PROFILE(adv.profile_slot);

    /* 本机固件版本（原先是键盘端广播过来的版本号） */
    next.kb_version_major = PROSPECTOR_MODULE_VERSION_MAJOR;
    next.kb_version_minor = PROSPECTOR_MODULE_VERSION_MINOR;
    next.kb_version_patch = PROSPECTOR_MODULE_VERSION_PATCH;
    next.kb_version_dev = (PROSPECTOR_MODULE_VERSION_DEV != 0);
    next.kb_version_valid = true;

    /* 本机自己就是数据源，永远有数据，不会出现"全部键盘超时" */
    next.no_keyboards = false;

    /* 信号无来源；"接收端自身电量"就是本机（dongle）自己的电量
     * （adv.battery_level 现在是左手电量，不能再当接收端电量用） */
    next.rssi = 0;
    next.rate_hz = 0.0f;
    next.scanner_battery = ble_scanner_battery_level();
    next.scanner_battery_pending = false;
    next.signal_update_pending = false;
    next.update_pending = false;

    if (memcmp(&next, &s_snapshot, sizeof(next)) == 0) {
        return false;
    }

    s_snapshot = next;
    return true;
}

/* ========== ble_* 接口（显示线程调用） ========== */

bool ble_get_pending_update(struct pending_display_data *out) {
    if (out == NULL) {
        return false;
    }

    const uint32_t now = k_uptime_get_32();
    if ((now - s_last_poll_ms) >= BLE_POLL_INTERVAL_MS) {
        s_last_poll_ms = now;
        if (ble_poll_local_state()) {
            s_update_pending = true;
        }
    }

    if (!s_update_pending && !s_force_update) {
        return false;
    }

    s_update_pending = false;
    s_force_update = false;
    *out = s_snapshot;
    return true;
}

bool ble_is_signal_pending(void) {
    /* 本机模式没有 RSSI / 速率来源，信号栏保持初始显示 */
    return false;
}

bool ble_get_kb_version(uint8_t *major, uint8_t *minor, uint8_t *patch, bool *is_dev, char *name,
                        size_t name_len) {
    /* 本机模式下"键盘版本"即本机固件版本 + 本机键盘名 */
    if (major != NULL) {
        *major = PROSPECTOR_MODULE_VERSION_MAJOR;
    }
    if (minor != NULL) {
        *minor = PROSPECTOR_MODULE_VERSION_MINOR;
    }
    if (patch != NULL) {
        *patch = PROSPECTOR_MODULE_VERSION_PATCH;
    }
    if (is_dev != NULL) {
        *is_dev = (PROSPECTOR_MODULE_VERSION_DEV != 0);
    }
    if (name != NULL && name_len > 0) {
        snprintf(name, name_len, "%s", CONFIG_ZMK_KEYBOARD_NAME);
    }
    return true;
}

int ble_get_selected_keyboard(void) { return s_selected_keyboard; }

int ble_set_selected_keyboard(int index) {
    /* 本机模式只有槽位 0 这一台"键盘" */
    if (index != 0) {
        return -EINVAL;
    }

    s_selected_keyboard = index;
    s_force_update = true;
    return 0;
}

bool ble_copy_keyboard_status(int index, struct zmk_keyboard_status *out) {
    if (out == NULL || index != 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->active = true;
    out->last_seen = k_uptime_get_32();
    ble_fill_adv_data(&out->data);
    out->rssi = 0; /* 本机模式没有对端信号强度 */
    snprintf(out->ble_name, sizeof(out->ble_name), "%s", CONFIG_ZMK_KEYBOARD_NAME);
    /* ble_addr / ble_addr_type 保持 0：本机没有对端 MAC 地址 */
    return true;
}

int ble_msg_send_display_refresh(void) {
    /* 屏幕切换后强制下一次刷新 */
    s_force_update = true;
    return 0;
}

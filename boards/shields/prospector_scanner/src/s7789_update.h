/*
 * s7789_update.h - ST7789 状态屏的"本机"数据源接口
 *
 * 原来这些数据由模块的 scanner_core.c / status_scanner.c 通过被动扫描其它
 * 键盘的 BLE 广播提供，接口名统一为 scanner_*。当前固件不再使用 scanner，
 * 改由 s7789_update.c 直接读取本机 ZMK 状态，接口名统一改为 ble_*。
 *
 * 命名对应关系（旧 -> 新）：
 *   scanner_get_pending_update        -> ble_get_pending_update
 *   scanner_is_signal_pending         -> ble_is_signal_pending
 *   scanner_signal_rssi / _rate_x100  -> ble_signal_rssi / ble_signal_rate_x100
 *   scanner_get_pending_battery       -> ble_get_pending_battery
 *   scanner_get_kb_version            -> ble_get_kb_version
 *   scanner_get_selected_keyboard     -> ble_get_selected_keyboard
 *   scanner_set_selected_keyboard     -> ble_set_selected_keyboard
 *   zmk_status_scanner_copy_keyboard  -> ble_copy_keyboard_status
 *   scanner_msg_send_display_refresh  -> ble_msg_send_display_refresh
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* struct pending_display_data 的唯一定义处，禁止在本文件重复定义 */
#include <zmk/scanner_core.h>
/* struct zmk_keyboard_status（多键盘列表界面使用） */
#include <zmk/status_scanner.h>

/**
 * @brief 取一份待显示数据快照（仅显示线程 / LVGL 定时器调用）
 *
 * @param out 输出快照
 * @return true 表示有变化需要刷新显示
 */
bool ble_get_pending_update(struct pending_display_data *out);

/**
 * @brief 是否有信号栏更新待处理
 *
 * 本机模式没有 RSSI / 速率来源，恒为 false（信号栏保持初始显示）。
 */
bool ble_is_signal_pending(void);

/**
 * @brief 取本机自身电量变化（顶部右侧电量图标）
 *
 * @param level 输出电量百分比
 * @return true 表示电量有变化
 */
bool ble_get_pending_battery(int *level);

/**
 * @brief 取"键盘"（本机）固件版本信息
 *
 * 本机模式下返回的是本机固件版本与键盘名。
 */
bool ble_get_kb_version(uint8_t *major, uint8_t *minor, uint8_t *patch, bool *is_dev,
                        char *name, size_t name_len);

/**
 * @brief 取/设置当前选中的键盘槽位（本机模式只有槽位 0）
 */
int ble_get_selected_keyboard(void);
int ble_set_selected_keyboard(int index);

/**
 * @brief 复制键盘状态快照（本机模式只有槽位 0 有效）
 */
bool ble_copy_keyboard_status(int index, struct zmk_keyboard_status *out);

/**
 * @brief 请求下一次刷新立即执行（屏幕切换后调用）
 */
int ble_msg_send_display_refresh(void);

/* 信号强度 / 速率：本机模式无来源，保持 0 与负值（界面显示 "-.--Hz"） */
extern volatile int8_t ble_signal_rssi;
extern volatile int32_t ble_signal_rate_x100;

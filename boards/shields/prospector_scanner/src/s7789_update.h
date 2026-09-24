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
 *
 * 电量相关（本机电量 + 左右手电量缓存）在 s7789_update_battery.c 里实现。
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
 * @brief 取本机（接收端）自身电量
 *
 * 定义在 s7789_update_battery.c。用于状态快照的 scanner_battery 字段
 * （经典界面显示"接收端电量"）；与 ble_get_pending_battery() 的区别是
 * 这里不做变化检测，直接返回当前值。
 */
uint8_t ble_scanner_battery_level(void);

/**
 * @brief 更新本模块缓存的左右手电量（定义在 s7789_update_battery.c，由 app/src/battery_cb.c 调用）
 *
 * 显示端只认自己这份缓存：模块提供入口 + 缓存，不反向 extern app 侧的函数。
 * "哪只手是哪只手"（dongle 模式下靠从机 BLS 上报的 Battery Identifier 认手）
 * 由 app 侧判断好后再推过来。0 表示未连接 / 未知，界面据此显示"未连接"。
 *
 * 调用上下文：系统工作队列（app 侧定时刷新）；两个字节各自原子写入，
 * 极端情况下界面可能出现一帧新旧混合，下一次刷新（1s 内）即一致。
 *
 * @param left  左手电量（显示端 "L" 槽位）
 * @param right 右手电量（显示端 "R" 槽位）
 */
void ble_battery_update(uint8_t left, uint8_t right);

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

/* 左右手电量缓存（定义在 s7789_update_battery.c，由 app/src/battery_cb.c 通过
 * ble_battery_update() 写入；s7789_update.c 只读这份值）0 = 未连接 / 未知 */
extern volatile uint8_t ble_battery_left;  /* 左手电量（显示端 "L" 槽位） */
extern volatile uint8_t ble_battery_right; /* 右手电量（显示端 "R" 槽位） */

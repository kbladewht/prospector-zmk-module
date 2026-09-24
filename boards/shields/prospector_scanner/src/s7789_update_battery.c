/*
 * s7789_update_battery.c - 电量相关（本机电量 + 左右手电量缓存）
 *
 * 从 s7789_update.c 里拆出来的部分（那边的文件只负责"本机其它状态"）：
 *   1. ble_scanner_battery_level()：本机（接收端）自身电量取值，
 *      给状态快照的 scanner_battery 字段用；
 *   2. ble_get_pending_battery()：本机电量的变化检测，经典界面右上角
 *      "接收端电量"用它（显示端 100ms 调一次，所以只在变化时返回 true）；
 *   3. ble_battery_update() + ble_battery_left / ble_battery_right：
 *      左右手电量缓存，由 app/src/battery_cb.c 刷新后推过来（模块不反向依赖 app 的
 *      函数），显示端在 s7789_update.c 的 ble_fill_adv_data() 里读这份缓存。
 *
 * 这里不碰 split / BLS：哪只手是哪只手（dongle 靠从机 BLS 上报的 Battery Identifier
 * 认手）由 app 侧判断好再推过来。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "s7789_update.h"

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ========== 本机（接收端）自身电量 ========== */

/* 本机电量；没开电量上报的构建按 0 处理 */
static int ble_local_battery_soc(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    return zmk_battery_state_of_charge();
#else
    return 0;
#endif
}

/* 本机自己就是数据源，"接收端自身电量"就是本机（dongle）自己的电量 */
uint8_t ble_scanner_battery_level(void) { return (uint8_t)ble_local_battery_soc(); }

/* 上次上报给显示端的本机电量（发送者的初始值 0 也可能是有效值，用 -1 表示"没上报过"） */
static int s_last_battery = -1;

bool ble_get_pending_battery(int *level) {
    const int soc = ble_local_battery_soc();

    if (soc == s_last_battery) {
        return false;
    }
    s_last_battery = soc;

    if (level != NULL) {
        *level = soc;
    }
    return true;
}

/* ========== 左右手电量缓存（由 app/src/battery_cb.c 推送） ========== */

/* 0 = 未连接 / 未知，界面据此显示"未连接"。两个字节各自原子写入：
 * 极端情况下界面可能出现一帧新旧混合，下一次刷新（app 侧 1s 一次）即一致。 */
volatile uint8_t ble_battery_left;  /* 左手电量（显示端 "L" 槽位） */
volatile uint8_t ble_battery_right; /* 右手电量（显示端 "R" 槽位） */

void ble_battery_update(uint8_t left, uint8_t right) {
    ble_battery_left = left;
    ble_battery_right = right;
}

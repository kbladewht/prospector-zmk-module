/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * qf_display_log.c - 把 Zephyr 日志画到 7789(ST7789V) 屏上（不经过 LVGL）
 *
 * 用途
 * ====
 * 不接 USB/串口时也想看设备日志，就编译一个"日志屏"固件：本文件自己维护
 * 一个字符网格 + 一条 16 像素高的 RGB565 行缓冲，用 Zephyr 标准 display
 * API（display_write）把日志一行行刷到屏上。
 *
 * 整条链路不依赖 LVGL，也不用 ZMK_DISPLAY 的状态屏，所以日志固件里可以把
 * CONFIG_LVGL / CONFIG_ZMK_DISPLAY 全关掉（省掉 LVGL 内存池与状态屏代码，
 * 这是最省 RAM 的看日志方式）。构建命令见
 * modules/prospector-zmk-module/Kconfig 里 PROSPECTOR_DISPLAY_LOG 的 help。
 *
 * 日志从哪里来
 * ============
 * 1) 默认（CONFIG_LOG_MODE_DEFERRED，可继续配合 -S zmk-usb-logging）：
 *    这里注册一个额外的日志后端（LOG_BACKEND_DEFINE + autostart），与 USB
 *    日志后端并存 —— 屏上和 USB 上是同一批日志。后端只把字符写进本文件的
 *    行缓冲，时间戳/等级/模块名等格式化交给 Zephyr 的 log_output 完成。
 * 2) CONFIG_LOG_MODE_MINIMAL=y：minimal 模式没有后端，LOG_* 宏直接走
 *    printk，于是改为接管 printk 的字符出口（覆写 weak 的
 *    arch_printk_char_out()，并在初始化时 __printk_hook_install() 盖掉
 *    USB console 的 hook）。代价是 USB/串口日志同时消失（二选一）。
 *
 * 线程模型
 * ========
 * 字符接收 qf_log_char() 可能来自任何上下文（日志处理线程、系统工作队列、
 * 中断），所以临界区一律用 irq_lock()，里面只做内存拷贝，绝不调用 display
 * API。真正写屏放在系统工作队列的 k_work_delayable 里，默认
 * CONFIG_PROSPECTOR_DISPLAY_LOG_REFRESH_MS 一次，且只有内容变化
 * （s_dirty）时才刷 —— 空闲时几乎不占 SPI。
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#if IS_ENABLED(CONFIG_LOG_MODE_MINIMAL)
#include <zephyr/sys/printk-hooks.h>
#endif
#include <string.h>

LOG_MODULE_REGISTER(qf_display_log, LOG_LEVEL_INF);

/* 屏只有一个主人：日志屏与 ZMK 状态屏不能同时存在 */
#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
#error "PROSPECTOR_DISPLAY_LOG 与 ZMK_DISPLAY 冲突：日志固件请加 -DCONFIG_ZMK_DISPLAY=n -DCONFIG_LVGL=n"
#endif

#if !DT_HAS_CHOSEN(zephyr_display)
#error "DT 里没有 chosen zephyr,display（由 prospector_e73 shield 提供 ST7789V 节点）"
#endif

/* ========== 字体（数据在 qf_display_log_font.c，来源见该文件头） ========== */
#define QF_LOG_FONT_W 10
#define QF_LOG_FONT_H 16
#define QF_LOG_FONT_FIRST 0x20
#define QF_LOG_FONT_LAST 0x7e
#define QF_LOG_FONT_CW (QF_LOG_FONT_W * QF_LOG_FONT_H / 8) /* 20 字节/字形 */
#define QF_LOG_FONT_GLYPHS 95

extern const uint8_t qf_display_log_font[QF_LOG_FONT_GLYPHS][QF_LOG_FONT_CW];

/* ========== 文本缓冲（显示线程之外的写者只碰这些） ========== */
#define QF_LOG_COLS CONFIG_PROSPECTOR_DISPLAY_LOG_MAX_COLS
#define QF_LOG_LINES CONFIG_PROSPECTOR_DISPLAY_LOG_LINES

static char s_lines[QF_LOG_LINES][QF_LOG_COLS + 1];
static uint8_t s_line_level[QF_LOG_LINES];
static char s_cur[QF_LOG_COLS + 1]; /* 正在拼装的当前行 */
static uint8_t s_cur_len;
static uint8_t s_cur_level = LOG_LEVEL_INF;
static uint16_t s_head;   /* s_lines 中最新一行的槽位 */
static uint16_t s_filled; /* 已写入行数（不足一屏时屏顶留空） */
static volatile bool s_dirty;

/* ========== 字符入口（任意上下文，含中断） ========== */

/* 在 irq_lock 保护区内调用 */
static void qf_commit_line(void) {
    s_head++;
    if (s_head >= QF_LOG_LINES) {
        s_head = 0;
    }

    memcpy(s_lines[s_head], s_cur, s_cur_len);
    s_lines[s_head][s_cur_len] = '\0';
    s_line_level[s_head] = s_cur_level;

    if (s_filled < QF_LOG_LINES) {
        s_filled++;
    }

    s_cur_len = 0;
    s_dirty = true;
}

/* 在 irq_lock 保护区内调用：超宽自动折行，和串口终端一样 */
static void qf_append_char(char c) {
    if (s_cur_len >= QF_LOG_COLS) {
        qf_commit_line();
    }

    s_cur[s_cur_len] = c;
    s_cur_len++;
}

/**
 * @brief 接收一个日志字符（日志后端的 char_out / printk 出口都汇到这里）
 *
 * 只做内存拷贝，供任意上下文调用。
 */
static void qf_log_char(char c) {
    unsigned int key = irq_lock();

    if (c == '\n') {
        qf_commit_line();
    } else if (c == '\r') {
        /* CRLF 里的 CR：忽略（行结束由 \n 提交） */
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            qf_append_char(' ');
        }
    } else if (c >= QF_LOG_FONT_FIRST && c <= QF_LOG_FONT_LAST) {
        qf_append_char(c);
    }
    /* 其余控制字符直接丢弃 */

    irq_unlock(key);
}

#if IS_ENABLED(CONFIG_LOG_MODE_MINIMAL)
static int qf_printk_char_out(int c);
#endif

/* ========== 渲染：文本行 -> RGB565 像素行 -> display_write ========== */

/* 颜色用 RGB565；背景纯黑，前景按日志等级着色 */
#define QF_LOG_COLOR_BG 0x0000
#define QF_LOG_COLOR_ERR 0xf800
#define QF_LOG_COLOR_WRN 0xffe0
#define QF_LOG_COLOR_INF 0x07e0
#define QF_LOG_COLOR_DBG 0x7bef
#define QF_LOG_COLOR_OTHER 0xffff

static const struct device *s_display;
static uint16_t s_cols;      /* 屏上每行字符数 */
static uint16_t s_row_px;    /* 一行字符占的像素宽度 */
static uint16_t s_disp_rows; /* 屏上能显示的行数 */
static volatile bool s_ready;

/* 一行字符展开后的像素缓冲（RGB565，pitch == width == s_row_px） */
static uint16_t s_px[QF_LOG_COLS * QF_LOG_FONT_W * QF_LOG_FONT_H];

/* 刷屏前在临界区里做的行快照，避免读到半更新的行 */
static char s_snap[QF_LOG_LINES][QF_LOG_COLS + 1];
static uint8_t s_snap_level[QF_LOG_LINES];

static uint16_t qf_color_for_level(uint8_t level) {
#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_LOG_COLORS)
    switch (level) {
    case LOG_LEVEL_ERR:
        return QF_LOG_COLOR_ERR;
    case LOG_LEVEL_WRN:
        return QF_LOG_COLOR_WRN;
    case LOG_LEVEL_INF:
        return QF_LOG_COLOR_INF;
    case LOG_LEVEL_DBG:
        return QF_LOG_COLOR_DBG;
    default:
        return QF_LOG_COLOR_OTHER;
    }
#else
    ARG_UNUSED(level);
    return QF_LOG_COLOR_OTHER;
#endif
}

/* 把一行文本展开进 s_px：字形是 vpacked + LSB 在上的 10x16 位图 */
static void qf_render_line(const char *text, uint16_t fg) {
    const uint16_t fg_be = sys_cpu_to_be16(fg);
    const uint16_t bg_be = sys_cpu_to_be16((uint16_t)QF_LOG_COLOR_BG);
    size_t len = strlen(text);

    for (uint16_t col = 0; col < s_cols; col++) {
        const uint8_t *glyph = NULL;
        char c = (col < len) ? text[col] : ' ';

        if (c >= QF_LOG_FONT_FIRST && c <= QF_LOG_FONT_LAST) {
            glyph = qf_display_log_font[c - QF_LOG_FONT_FIRST];
        }

        for (uint16_t gx = 0; gx < QF_LOG_FONT_W; gx++) {
            uint16_t x = col * QF_LOG_FONT_W + gx;

            for (uint16_t gy = 0; gy < QF_LOG_FONT_H; gy++) {
                bool on = false;

                if (glyph != NULL) {
                    uint8_t bits = glyph[gx * (QF_LOG_FONT_H / 8) + gy / 8];

                    on = (bits & BIT(gy % 8)) != 0;
                }

                s_px[(size_t)gy * s_row_px + x] = on ? fg_be : bg_be;
            }
        }
    }
}

/* 重画整个可见区域：屏第 0 行对应环形缓冲里最旧的一行 */
static void qf_flush(void) {
    struct display_buffer_descriptor desc = {
        .buf_size = (size_t)s_row_px * QF_LOG_FONT_H * sizeof(uint16_t),
        .width = s_row_px,
        .height = QF_LOG_FONT_H,
        .pitch = s_row_px,
    };
    unsigned int key;
    uint16_t head;
    uint16_t filled;
    bool was_dirty;

    key = irq_lock();
    head = s_head;
    filled = s_filled;
    was_dirty = s_dirty;
    s_dirty = false;

    for (uint16_t row = 0; row < s_disp_rows; row++) {
        uint16_t back = s_disp_rows - 1 - row; /* 从最新一行往回数 */

        if (back < filled) {
            uint16_t slot = (head + QF_LOG_LINES - back) % QF_LOG_LINES;

            memcpy(s_snap[row], s_lines[slot], QF_LOG_COLS + 1);
            s_snap_level[row] = s_line_level[slot];
        } else {
            s_snap[row][0] = '\0';
            s_snap_level[row] = LOG_LEVEL_INF;
        }
    }
    irq_unlock(key);

    if (!was_dirty) {
        return;
    }

    for (uint16_t row = 0; row < s_disp_rows; row++) {
        qf_render_line(s_snap[row], qf_color_for_level(s_snap_level[row]));

        if (display_write(s_display, 0, row * QF_LOG_FONT_H, &desc, s_px) < 0) {
            /* 屏没就绪或 SPI 出错：静默放弃，不要反过来扰乱日志系统 */
            return;
        }
    }
}

static void qf_refresh_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    if (s_ready) {
        qf_flush();
    }

    k_work_reschedule(dwork, K_MSEC(CONFIG_PROSPECTOR_DISPLAY_LOG_REFRESH_MS));
}

static K_WORK_DELAYABLE_DEFINE(qf_refresh_work, qf_refresh_handler);

/* 初始化时先把整屏涂黑，避免上电残留的雪花画面 */
static int qf_blank_screen(void) {
    struct display_buffer_descriptor desc = {
        .buf_size = (size_t)s_row_px * QF_LOG_FONT_H * sizeof(uint16_t),
        .width = s_row_px,
        .height = QF_LOG_FONT_H,
        .pitch = s_row_px,
    };
    struct display_capabilities cap;
    uint16_t y;

    memset(s_px, 0, sizeof(s_px));
    display_get_capabilities(s_display, &cap);

    for (y = 0; (y + QF_LOG_FONT_H) <= cap.y_resolution; y += QF_LOG_FONT_H) {
        if (display_write(s_display, 0, y, &desc, s_px) < 0) {
            return -EIO;
        }
    }

    /* 宽度除不尽时右侧会留一条窄边（280x240 屏配 10 像素宽字体正好整除） */
    return 0;
}

static int qf_display_log_init(void) {
    struct display_capabilities cap;

    s_display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(s_display)) {
        LOG_ERR("log display not ready");
        return -ENODEV;
    }

    display_get_capabilities(s_display, &cap);

    s_cols = MIN(cap.x_resolution / QF_LOG_FONT_W, (uint16_t)QF_LOG_COLS);
    s_disp_rows = MIN(cap.y_resolution / QF_LOG_FONT_H, (uint16_t)QF_LOG_LINES);

    if (s_cols == 0 || s_disp_rows == 0) {
        LOG_ERR("display too small for %dx%d font", QF_LOG_FONT_W, QF_LOG_FONT_H);
        return -EINVAL;
    }

    s_row_px = s_cols * QF_LOG_FONT_W;

    (void)display_blanking_off(s_display);
    (void)qf_blank_screen();

    s_ready = true;
    k_work_schedule(&qf_refresh_work, K_NO_WAIT);

#if IS_ENABLED(CONFIG_LOG_MODE_MINIMAL)
    /* minimal 模式没有日志后端，LOG_* 直接走 printk：接管字符出口
     * （在 APPLICATION 阶段安装，盖掉 USB console 早先装的 hook） */
    __printk_hook_install(qf_printk_char_out);
#endif

    LOG_INF("log display ready: %ux%u chars", s_cols, s_disp_rows);
    return 0;
}

SYS_INIT(qf_display_log_init, APPLICATION, 90);

/* ========== 日志采集 ========== */
#if !IS_ENABLED(CONFIG_LOG_MODE_MINIMAL)

static uint8_t s_out_buf[64];

/* log_output 的分块回调：把格式化好的文本喂给行缓冲 */
static int qf_output_func(uint8_t *buf, size_t size, void *ctx) {
    ARG_UNUSED(ctx);

    for (size_t i = 0; i < size; i++) {
        qf_log_char((char)buf[i]);
    }

    return (int)size;
}

LOG_OUTPUT_DEFINE(qf_display_log_output, qf_output_func, s_out_buf, sizeof(s_out_buf));

/* 新日志行的颜色按其等级决定（由后端在每条消息前设置） */
static void qf_set_level(uint8_t level) {
    unsigned int key = irq_lock();

    s_cur_level = level;
    irq_unlock(key);
}

static void qf_backend_process(const struct log_backend *const backend,
                               union log_msg_generic *msg) {
    /* 不传 LOG_OUTPUT_FLAG_COLORS：屏上要的是纯文本，不要 ANSI 转义序列 */
    uint32_t flags = LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_CRLF_LFONLY;

    ARG_UNUSED(backend);

    if (IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_LOG_TIMESTAMP)) {
        flags |= LOG_OUTPUT_FLAG_TIMESTAMP;
    }

    qf_set_level(log_msg_get_level(&msg->log));
    log_output_msg_process(&qf_display_log_output, &msg->log, flags);
}

static void qf_backend_panic(const struct log_backend *const backend) {
    ARG_UNUSED(backend);

    log_backend_std_panic(&qf_display_log_output);
}

static void qf_backend_dropped(const struct log_backend *const backend, uint32_t cnt) {
    ARG_UNUSED(backend);

    log_backend_std_dropped(&qf_display_log_output, cnt);
}

static const struct log_backend_api qf_backend_api = {
    .process = qf_backend_process,
    .panic = qf_backend_panic,
    .dropped = qf_backend_dropped,
};

/* 与 USB/UART 后端并存：屏上和串口上是同一批日志 */
LOG_BACKEND_DEFINE(qf_display_log_backend, qf_backend_api, true);

#else /* CONFIG_LOG_MODE_MINIMAL */

/* minimal 模式下 LOG_* 直接走 printk，所以这里就是日志的唯一出口 */
static int qf_printk_char_out(int c) {
    qf_log_char((char)c);
    return c;
}

/* zephyr/lib/os/printk.c 里的 arch_printk_char_out() 是 weak 的，直接覆写 */
int arch_printk_char_out(int c) {
    return qf_printk_char_out(c);
}

#endif /* CONFIG_LOG_MODE_MINIMAL */

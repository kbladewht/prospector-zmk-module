#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(zephyr_status_screen, CONFIG_LOG_DEFAULT_LEVEL);

#define SCREEN_MAX_WIDTH 320
#define STRIP_HEIGHT 8
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_NAME 0x4E7E
#define COLOR_GREEN 0x07E0
#define COLOR_RED 0xF800
#define COLOR_AMBER 0xFD20
#define COLOR_DIM 0x7B72
#define COLOR_TRACK_GREEN 0x02E4
#define COLOR_TRACK_AMBER 0x4B01
#define COLOR_TRACK_RED 0x4802

static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint16_t pixels[SCREEN_MAX_WIDTH * STRIP_HEIGHT];
static uint16_t screen_width;
static uint16_t screen_height;
static uint16_t strip_y;
static uint8_t strip_rows;

static const uint8_t text_rows[42][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {15, 16, 16, 16, 16, 16, 15}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {15, 16, 16, 19, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 2, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
    {0, 0, 0, 14, 0, 0, 0}, {0, 4, 0, 0, 4, 0, 0},
    {0, 0, 0, 0, 0, 4, 8}, {17, 2, 4, 8, 17, 0, 0},
    {0, 0, 0, 0, 0, 0, 31}, {0, 0, 0, 0, 0, 0, 0},
};

static const uint8_t large_text_rows[42][9] = {
    {30, 33, 35, 37, 41, 49, 33, 33, 30}, {12, 28, 12, 12, 12, 12, 12, 12, 30},
    {30, 33, 1, 1, 2, 4, 8, 16, 63}, {30, 33, 1, 1, 14, 1, 1, 33, 30},
    {2, 6, 10, 18, 34, 63, 2, 2, 2}, {63, 32, 32, 32, 62, 1, 1, 33, 30},
    {14, 16, 32, 32, 62, 33, 33, 33, 30}, {63, 1, 2, 4, 8, 8, 16, 16, 16},
    {30, 33, 33, 33, 30, 33, 33, 33, 30}, {30, 33, 33, 33, 31, 1, 1, 2, 28},
    {12, 30, 51, 51, 63, 51, 51, 51, 51}, {62, 51, 51, 62, 51, 51, 51, 51, 62},
    {15, 24, 48, 48, 48, 48, 48, 24, 15}, {60, 54, 51, 51, 51, 51, 51, 54, 60},
    {63, 48, 48, 48, 62, 48, 48, 48, 63}, {63, 48, 48, 48, 62, 48, 48, 48, 48},
    {15, 24, 48, 48, 55, 51, 51, 27, 15}, {51, 51, 51, 51, 63, 51, 51, 51, 51},
    {30, 12, 12, 12, 12, 12, 12, 12, 30}, {7, 3, 3, 3, 3, 3, 35, 35, 30},
    {51, 54, 60, 56, 60, 54, 51, 51, 51}, {48, 48, 48, 48, 48, 48, 48, 48, 63},
    {33, 51, 63, 63, 45, 33, 33, 33, 33}, {33, 49, 57, 61, 55, 51, 49, 33, 33},
    {30, 33, 51, 51, 51, 51, 51, 33, 30}, {62, 51, 51, 51, 62, 48, 48, 48, 48},
    {30, 33, 51, 51, 51, 55, 54, 33, 31}, {62, 51, 51, 51, 62, 60, 54, 51, 51},
    {15, 24, 48, 48, 30, 3, 3, 6, 60}, {63, 12, 12, 12, 12, 12, 12, 12, 12},
    {51, 51, 51, 51, 51, 51, 51, 51, 30}, {51, 51, 51, 51, 51, 51, 51, 30, 12},
    {33, 33, 33, 45, 45, 63, 51, 51, 33}, {51, 51, 30, 12, 12, 30, 51, 51, 51},
    {51, 51, 51, 30, 12, 12, 12, 12, 12}, {63, 3, 6, 12, 12, 24, 48, 48, 63},
    {0, 0, 0, 0, 30, 0, 0, 0, 0}, {0, 12, 12, 0, 0, 12, 12, 0, 0},
    {0, 0, 0, 0, 0, 0, 12, 12, 24}, {33, 3, 6, 12, 24, 48, 33, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 63}, {0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static const uint8_t layer_text_rows[36][12] = {
    {60, 102, 102, 102, 110, 118, 102, 102, 102, 102, 102, 60},
    {24, 56, 24, 24, 24, 24, 24, 24, 24, 24, 24, 126},
    {60, 102, 6, 6, 12, 24, 48, 96, 96, 102, 102, 126},
    {124, 102, 6, 6, 28, 6, 6, 6, 6, 102, 102, 60},
    {12, 28, 44, 76, 76, 126, 12, 12, 12, 12, 12, 12},
    {126, 96, 96, 96, 124, 102, 6, 6, 6, 102, 102, 60},
    {28, 48, 96, 96, 124, 102, 102, 102, 102, 102, 102, 60},
    {126, 102, 6, 12, 12, 24, 24, 48, 48, 48, 48, 48},
    {60, 102, 102, 102, 60, 102, 102, 102, 102, 102, 102, 60},
    {60, 102, 102, 102, 102, 102, 62, 6, 6, 12, 24, 56},
    {24, 60, 102, 102, 102, 126, 102, 102, 102, 102, 102, 102},
    {124, 102, 102, 102, 124, 102, 102, 102, 102, 102, 102, 124},
    {60, 102, 96, 96, 96, 96, 96, 96, 96, 102, 102, 60},
    {120, 108, 102, 102, 102, 102, 102, 102, 102, 102, 108, 120},
    {126, 96, 96, 96, 124, 96, 96, 96, 96, 96, 96, 126},
    {126, 96, 96, 96, 124, 96, 96, 96, 96, 96, 96, 96},
    {60, 102, 96, 96, 96, 110, 102, 102, 102, 102, 102, 60},
    {102, 102, 102, 102, 102, 126, 102, 102, 102, 102, 102, 102},
    {60, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 60},
    {30, 12, 12, 12, 12, 12, 12, 12, 108, 108, 108, 56},
    {102, 108, 120, 112, 120, 108, 102, 102, 102, 102, 102, 102},
    {96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 126},
    {99, 119, 127, 107, 99, 99, 99, 99, 99, 99, 99, 99},
    {99, 99, 115, 123, 111, 103, 99, 99, 99, 99, 99, 99},
    {60, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 60},
    {124, 102, 102, 102, 102, 124, 96, 96, 96, 96, 96, 96},
    {60, 102, 102, 102, 102, 102, 102, 102, 110, 108, 60, 14},
    {124, 102, 102, 102, 102, 124, 108, 102, 102, 102, 102, 102},
    {60, 102, 96, 96, 60, 6, 6, 6, 6, 102, 102, 60},
    {126, 90, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24},
    {102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 60},
    {102, 102, 102, 102, 102, 102, 102, 102, 60, 60, 24, 24},
    {99, 99, 99, 99, 99, 99, 107, 107, 127, 119, 99, 99},
    {102, 102, 102, 60, 24, 24, 60, 102, 102, 102, 102, 102},
    {102, 102, 102, 102, 60, 24, 24, 24, 24, 24, 24, 24},
    {126, 6, 6, 12, 12, 24, 24, 48, 48, 96, 96, 126},
};

static const char text_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:.%_ ";
static const uint8_t blank_glyph[7] = {0};
static const uint8_t blank_large_glyph[9] = {0};

struct screen_state {
    char previous_layer[16];
    char layer_name[16];
    char next_layer[16];
    uint8_t layer;
    uint8_t left_battery;
    uint8_t right_battery;
    uint8_t modifiers;
    int8_t rssi;
    bool rssi_valid;
    bool has_previous_layer;
    bool has_next_layer;
};

static struct screen_state last_state;
static bool have_last_state;
static volatile uint8_t left_battery;
static volatile uint8_t right_battery;
static volatile int8_t link_rssi;
static volatile bool link_rssi_valid;

void ble_battery_update(uint8_t left, uint8_t right);
void ble_signal_update(int8_t rssi, int32_t rate_x100);

extern uint8_t qmk_display_active_layer(void) __attribute__((weak));

void ble_battery_update(uint8_t left, uint8_t right) {
    left_battery = left;
    right_battery = right;
}

void ble_signal_update(int8_t rssi, int32_t rate_x100) {
    ARG_UNUSED(rate_x100);
    link_rssi = rssi;
    link_rssi_valid = true;
}

static uint16_t get_color(uint16_t color) {
    return sys_cpu_to_be16(color);
}

static const uint8_t *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(text_chars) - 1; i++) {
        if (text_chars[i] == c) {
            return text_rows[i];
        }
    }
    return blank_glyph;
}

static const uint8_t *large_glyph_for(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(text_chars) - 1; i++) {
        if (text_chars[i] == c) {
            return large_text_rows[i];
        }
    }
    return blank_large_glyph;
}

static void draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= screen_width || y < strip_y || y >= strip_y + strip_rows || y >= screen_height) {
        return;
    }
    pixels[(y - strip_y) * screen_width + x] = get_color(color);
}

static void draw_rect(int x, int y, int width, int height, uint16_t color) {
    for (int row = y; row < y + height; row++) {
        for (int col = x; col < x + width; col++) {
            draw_pixel(col, row, color);
        }
    }
}

static void draw_text(const char *text, int x, int y, int scale, uint16_t color) {
    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        const uint8_t *glyph = glyph_for(*text++);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1U << (4 - col))) {
                    draw_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static void draw_text_centered(const char *text, int y, int scale, uint16_t color) {
    while ((int)strlen(text) * 6 * scale - scale > screen_width - 24 && scale > 1) {
        scale--;
    }
    const int text_width = (int)strlen(text) * 6 * scale - scale;
    draw_text(text, MAX(0, ((int)screen_width - text_width) / 2), y, scale, color);
}

static const uint8_t *layer_glyph_for(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= '0' && c <= '9') {
        return layer_text_rows[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return layer_text_rows[10 + c - 'A'];
    }
    return NULL;
}

static void draw_layer_text(const char *text, int x, int y, uint16_t color) {
    while (*text != '\0') {
        const uint8_t *glyph = layer_glyph_for(*text);
        if (glyph != NULL) {
            for (int out_y = 0; out_y < 18; out_y++) {
                const int src_y0 = (out_y * 2) / 3;
                const int src_y1 = (out_y * 2 + 1) / 3;
                for (int out_x = 0; out_x < 12; out_x++) {
                    const int src_x0 = (out_x * 2) / 3;
                    const int src_x1 = (out_x * 2 + 1) / 3;
                    uint8_t coverage = 0;

                    coverage += !!(glyph[src_y0] & (1U << (7 - src_x0)));
                    coverage += !!(glyph[src_y0] & (1U << (7 - src_x1)));
                    coverage += !!(glyph[src_y1] & (1U << (7 - src_x0)));
                    coverage += !!(glyph[src_y1] & (1U << (7 - src_x1)));
                    if (coverage != 0) {
                        uint8_t alpha = (uint8_t)(coverage * 255 / 4);
                        uint8_t red = ((color >> 11) & 0x1F) * alpha / 255;
                        uint8_t green = ((color >> 5) & 0x3F) * alpha / 255;
                        uint8_t blue = (color & 0x1F) * alpha / 255;
                        uint16_t antialiased = (red << 11) | (green << 5) | blue;
                        draw_pixel(x + out_x, y + out_y, antialiased);
                    }
                }
            }
        } else {
            const uint8_t *fallback = large_glyph_for(*text);
            for (int row = 0; row < 9; row++) {
                for (int col = 0; col < 6; col++) {
                    if (fallback[row] & (1U << (5 - col))) {
                        draw_rect(x + col * 2, y + row * 2, 2, 2, color);
                    }
                }
            }
        }
        x += 14;
        text++;
    }
}

static void draw_layer_text_centered(const char *text, int y, uint16_t color) {
    const int text_width = (int)strlen(text) * 14 - 2;
    draw_layer_text(text, MAX(0, ((int)screen_width - text_width) / 2), y, color);
}

static uint16_t battery_color(uint8_t level) {
    if (level > 50) {
        return COLOR_GREEN;
    }
    if (level > 10) {
        return COLOR_AMBER;
    }
    return COLOR_RED;
}

static void draw_battery(const char *label, uint8_t level, int x, int y, int width) {
    char value[12];
    uint16_t track_color = COLOR_TRACK_RED;

    if (level == 0) {
        snprintf(value, sizeof(value), "%s --", label);
    } else {
        if (level > 100) {
            level = 100;
        }
        snprintf(value, sizeof(value), "%s %u%%", label, level);
        if (level > 50) {
            track_color = COLOR_TRACK_GREEN;
        } else if (level > 10) {
            track_color = COLOR_TRACK_AMBER;
        }
    }
    draw_text(value, x, y, 2, level == 0 ? COLOR_RED : battery_color(level));
    draw_rect(x, y + 18, width, 8, track_color);
    if (level > 0) {
        draw_rect(x, y + 18, width * level / 100, 8, battery_color(level));
    }
}

static void draw_state(const struct screen_state *state) {
    char modifiers[5];
    char signal[16];

    const char *left_peer = state->left_battery > 0 ? "L OK" : "L NO";
    const char *right_peer = state->right_battery > 0 ? "R OK" : "R NO";
    draw_text(left_peer, 20, 6, 1, state->left_battery > 0 ? COLOR_GREEN : COLOR_RED);
    draw_text("BLE 1", 20, 28, 1, COLOR_WHITE);
    draw_text(right_peer, screen_width - (int)strlen(right_peer) * 6 - 20, 6, 1,
              state->right_battery > 0 ? COLOR_GREEN : COLOR_RED);
    draw_text("BLE 2", screen_width - 5 * 6 - 20, 28, 1, COLOR_WHITE);

    draw_layer_text_centered("PROSPECTOR", 6, COLOR_NAME);
    draw_layer_text_centered("RECEIVER RS", 30, COLOR_NAME);

    if (state->has_previous_layer) {
        draw_layer_text_centered(state->previous_layer, 55, COLOR_DIM);
    }
    draw_layer_text_centered(state->layer_name, 90, COLOR_WHITE);
    if (state->has_next_layer) {
        draw_layer_text_centered(state->next_layer, 125, COLOR_DIM);
    }

    size_t mod_count = 0;
    if (state->modifiers & 0x11) modifiers[mod_count++] = 'C';
    if (state->modifiers & 0x22) modifiers[mod_count++] = 'S';
    if (state->modifiers & 0x44) modifiers[mod_count++] = 'A';
    if (state->modifiers & 0x88) modifiers[mod_count++] = 'G';
    modifiers[mod_count] = '\0';
    draw_text_centered(modifiers, 160, 3, COLOR_WHITE);

    if (state->rssi_valid) {
        snprintf(signal, sizeof(signal), "%dDBM", state->rssi);
    } else {
        snprintf(signal, sizeof(signal), "--DBM");
    }
    uint16_t signal_color = COLOR_RED;
    if (state->rssi_valid && state->rssi >= -60) {
        signal_color = COLOR_GREEN;
    } else if (state->rssi_valid && state->rssi >= -75) {
        signal_color = COLOR_AMBER;
    } else if (!state->rssi_valid) {
        signal_color = COLOR_DIM;
    }
    draw_text(signal, screen_width - (int)strlen(signal) * 12 - 8, 176, 2, signal_color);

    const int battery_width = (screen_width - 36) / 2;
    draw_battery("L", state->left_battery, 12, 202, battery_width);
    draw_battery("R", state->right_battery, 24 + battery_width, 202, battery_width);
}

static int render_frame(const struct screen_state *state) {
    for (uint16_t y = 0; y < screen_height; y += STRIP_HEIGHT) {
        struct display_buffer_descriptor desc;
        strip_y = y;
        strip_rows = MIN(STRIP_HEIGHT, screen_height - y);
        const uint16_t black = get_color(COLOR_BLACK);

        for (size_t i = 0; i < (size_t)screen_width * strip_rows; i++) {
            pixels[i] = black;
        }

        draw_state(state);
        desc.buf_size = (uint32_t)screen_width * strip_rows * sizeof(uint16_t);
        desc.width = screen_width;
        desc.height = strip_rows;
        desc.pitch = screen_width;
        desc.frame_incomplete = y + strip_rows < screen_height;

        int err = display_write(display, 0, y, &desc, pixels);
        if (err < 0) {
            LOG_ERR("Display write failed: %d", err);
            return err;
        }
    }
    return 0;
}

static void read_layer_name(uint8_t index, char *out, size_t out_len) {
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    if (name != NULL && name[0] != '\0') {
        snprintf(out, out_len, "%s", name);
    } else {
        snprintf(out, out_len, "%u", index);
    }
}

static void read_state(struct screen_state *state) {
    memset(state, 0, sizeof(*state));
    state->layer = qmk_display_active_layer != NULL
                       ? qmk_display_active_layer()
                       : (uint8_t)zmk_keymap_highest_layer_active();
    const uint8_t layer_count = ZMK_KEYMAP_LAYERS_LEN;
    if (state->layer >= layer_count) {
        state->layer = 0;
    }
    read_layer_name(state->layer, state->layer_name, sizeof(state->layer_name));
    state->has_previous_layer = state->layer > 0;
    state->has_next_layer = state->layer + 1 < layer_count;
    if (state->has_previous_layer) {
        read_layer_name(state->layer - 1, state->previous_layer,
                        sizeof(state->previous_layer));
    }
    if (state->has_next_layer) {
        read_layer_name(state->layer + 1, state->next_layer, sizeof(state->next_layer));
    }

    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    state->modifiers = report != NULL ? report->body.modifiers : 0;
    state->left_battery = left_battery;
    state->right_battery = right_battery;
    state->rssi = link_rssi;
    state->rssi_valid = link_rssi_valid;
}

static void refresh_screen(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(refresh_work, refresh_screen);

static void refresh_screen(struct k_work *work) {
    ARG_UNUSED(work);

    struct screen_state state;
    read_state(&state);
    if (!have_last_state || memcmp(&state, &last_state, sizeof(state)) != 0) {
        last_state = state;
        have_last_state = true;
        render_frame(&state);
    }
    k_work_reschedule(&refresh_work, K_MSEC(500));
}

static int zephyr_status_screen_init(void) {
    if (!device_is_ready(display)) {
        LOG_ERR("ST7789 display is not ready");
        return -ENODEV;
    }

    struct display_capabilities caps;
    display_get_capabilities(display, &caps);
    if (caps.x_resolution > SCREEN_MAX_WIDTH) {
        LOG_ERR("Unsupported display width: %u", caps.x_resolution);
        return -EINVAL;
    }

    screen_width = caps.x_resolution;
    screen_height = caps.y_resolution;
    if (!(caps.supported_pixel_formats & PIXEL_FORMAT_RGB_565)) {
        LOG_ERR("ST7789 does not support RGB565");
        return -ENOTSUP;
    }

    display_blanking_off(display);
    k_work_schedule(&refresh_work, K_NO_WAIT);
    LOG_INF("Raw Zephyr status screen ready: %ux%u", screen_width, screen_height);
    return 0;
}

SYS_INIT(zephyr_status_screen_init, APPLICATION, 95);

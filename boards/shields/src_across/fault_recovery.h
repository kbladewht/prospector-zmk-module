/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/** What the fatal-error handler recorded before rebooting. */
struct fault_record {
    uint32_t reason;      /* Zephyr K_ERR_* fatal reason code */
    uint32_t pc;          /* Faulting PC (0 if no exception frame) */
    uint32_t lr;          /* Link register at fault */
    uint32_t uptime_ms;   /* Uptime when the fault happened */
    uint32_t consecutive; /* Faults in a row without a healthy boot between */
    char thread[16];      /* Name of the faulting thread, or <isr>/<unnamed> */
};

/**
 * @brief Get the fault record from the crash that preceded this boot
 * @return true if this boot followed a crash-recovery reboot
 */
bool fault_recovery_get_last(struct fault_record *out);

/** @brief Raw hwinfo reset cause bits captured at boot (RESET_SOFTWARE etc.) */
uint32_t fault_recovery_reset_cause(void);

/* Synthetic "reason" codes recorded by the software watchdog (distinct from
 * Zephyr's small K_ERR_* fatal codes) so the boot log can tell a hang from
 * a fault. */
#define FAULT_REASON_HANG_DISPLAY 0xDEAD0001u /* LVGL display thread stopped ticking */
#define FAULT_REASON_HANG_CORE    0xDEAD0002u /* scanner core process_work stopped running */

/**
 * @brief Feed the display-thread watchdog channel
 *
 * Call from the periodic LVGL timer on the display thread, before any
 * early return. If this stops being called for the watchdog period the
 * device records FAULT_REASON_HANG_DISPLAY and reboots.
 */
void fault_recovery_display_alive(void);

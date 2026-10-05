/**
 * @file watchdog_tpl5010.c
 * @brief Zero-Heap TI TPL5010 Nanopower Hardware Watchdog & Multi-Task Health Coordinator.
 */

#include "watchdog_tpl5010.h"
#include <string.h>

void watchdog_tpl5010_init(watchdog_tpl5010_t *wd, uint32_t boot_timestamp_ms) {
    if (!wd) return;
    memset(wd, 0, sizeof(watchdog_tpl5010_t));

    wd->last_kick_timestamp_ms = boot_timestamp_ms;
    wd->last_valid_feed_ms = boot_timestamp_ms;
    wd->done_pin_state = false;
    wd->pulse_active = false;
    wd->hardware_reset_tripped = false;
    wd->task_health_mask = 0;
    wd->successful_kicks_count = 0;
    wd->suppressed_kicks_count = 0;
}

void watchdog_tpl5010_report_healthy(watchdog_tpl5010_t *wd, uint8_t task_mask) {
    if (!wd) return;
    /* Atomic bitwise OR of task confirmation */
    wd->task_health_mask |= task_mask;
}

bool watchdog_tpl5010_check_and_kick(watchdog_tpl5010_t *wd, uint32_t current_time_ms) {
    if (!wd) return false;

    /* Rule: Assert a 20 ms HIGH pulse on TPL5010_DONE IF AND ONLY IF task_health_mask == 0x0F */
    if (wd->task_health_mask == WATCHDOG_ALL_TASKS_MASK) {
        /* All critical tasks verified healthy */
        wd->done_pin_state = true;
        wd->pulse_active = true;
        wd->done_pulse_start_ms = current_time_ms;
        wd->last_kick_timestamp_ms = current_time_ms;
        wd->last_valid_feed_ms = current_time_ms;
        wd->successful_kicks_count++;

        /* Clear mask for the next 20-second evaluation window */
        wd->task_health_mask = 0;
        return true;
    } else {
        /* Task starvation or freeze detected! Suppress kick to let TPL5010 reset device */
        wd->done_pin_state = false;
        wd->pulse_active = false;
        wd->suppressed_kicks_count++;
        return false;
    }
}

void watchdog_tpl5010_update_gpio(watchdog_tpl5010_t *wd, uint32_t current_time_ms) {
    if (!wd) return;

    /* De-assert DONE pin after exactly 20 ms */
    if (wd->pulse_active) {
        if ((current_time_ms - wd->done_pulse_start_ms) >= TPL5010_DONE_PULSE_WIDTH_MS) {
            wd->done_pin_state = false;
            wd->pulse_active = false;
        }
    }

    /* Model external TI TPL5010 30-second hardware reset condition */
    if ((current_time_ms - wd->last_valid_feed_ms) >= TPL5010_RESET_WINDOW_MS) {
        wd->hardware_reset_tripped = true;
    }
}

bool watchdog_tpl5010_is_reset_tripped(const watchdog_tpl5010_t *wd) {
    return wd ? wd->hardware_reset_tripped : false;
}

uint32_t watchdog_tpl5010_get_time_until_reset_ms(const watchdog_tpl5010_t *wd,
                                                  uint32_t current_time_ms) {
    if (!wd) return 0;

    uint32_t elapsed = current_time_ms - wd->last_valid_feed_ms;
    if (elapsed >= TPL5010_RESET_WINDOW_MS) {
        return 0;
    }
    return (TPL5010_RESET_WINDOW_MS - elapsed);
}

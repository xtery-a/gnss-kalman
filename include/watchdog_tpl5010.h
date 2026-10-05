/**
 * @file watchdog_tpl5010.h
 * @brief Zero-Heap TI TPL5010 Nanopower Hardware Watchdog & Multi-Task Health Coordinator.
 *
 * Operational Profile:
 *  - External TI TPL5010 timer configured for a 30-second reset window via resistance to ground.
 *  - Firmware timer executes watchdog_tpl5010_check_and_kick() every 20 seconds.
 *  - Multi-Thread Health Mask requires bits from ALL critical tasks:
 *      * Bit 0: GNSS DMA Tokenizer & Fix Task
 *      * Bit 1: CAN-FD Differential Bus Task
 *      * Bit 2: Memory-in-Pixel Display Flush Task
 *      * Bit 3: Electrochemical Power Supervisor Task
 *  - Asserts a 20 ms HIGH pulse on TPL5010_DONE IF AND ONLY IF task_health_mask == 0x0F.
 *  - Clears task_health_mask = 0 upon each successful kick.
 *  - If any task starves for > 20 seconds, DONE pulse is immediately suppressed.
 *    At 30 seconds, TPL5010 drops RSTn / power output to force hardware cold reboot.
 */

#ifndef WATCHDOG_TPL5010_H
#define WATCHDOG_TPL5010_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Hardware Timing Constants                                                  */
/* -------------------------------------------------------------------------- */
#define TPL5010_KICK_INTERVAL_MS        (20000U) /**< 20 seconds between periodic kicks */
#define TPL5010_RESET_WINDOW_MS         (30000U) /**< 30 seconds hard hardware reset window */
#define TPL5010_DONE_PULSE_WIDTH_MS     (20U)    /**< 20 ms HIGH pulse duration on DONE pin */

/* -------------------------------------------------------------------------- */
/* Multi-Task Health Bitmask Definitions                                      */
/* -------------------------------------------------------------------------- */
#define WATCHDOG_TASK_GNSS              (1U << 0) /**< Bit 0: GNSS DMA Tokenizer Task */
#define WATCHDOG_TASK_CANFD             (1U << 1) /**< Bit 1: Split-Node CAN-FD Bus Task */
#define WATCHDOG_TASK_DISPLAY           (1U << 2) /**< Bit 2: Sharp MIP Display Flush Task */
#define WATCHDOG_TASK_POWER             (1U << 3) /**< Bit 3: Power Supervisor Task */

#define WATCHDOG_ALL_TASKS_MASK         (0x0FU)   /**< 0b00001111: All 4 tasks required */

/* -------------------------------------------------------------------------- */
/* Watchdog State Structure (Zero Heap)                                       */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint8_t  task_health_mask;       /**< Accumulated health bits reported by threads */
    uint32_t last_kick_timestamp_ms; /**< Timestamp of last emitted DONE pulse */
    uint32_t last_valid_feed_ms;     /**< Timestamp when all tasks were last confirmed healthy */
    bool     done_pin_state;         /**< Current logical output on TPL5010 DONE GPIO pin */
    uint32_t done_pulse_start_ms;    /**< Timestamp when active DONE pulse was asserted */
    bool     pulse_active;           /**< True while 20 ms DONE pulse is in progress */
    bool     hardware_reset_tripped; /**< True if 30s elapsed without valid kick */
    uint32_t successful_kicks_count; /**< Number of valid DONE pulses issued */
    uint32_t suppressed_kicks_count; /**< Number of cycles suppressed due to missing tasks */
} watchdog_tpl5010_t;

/* -------------------------------------------------------------------------- */
/* Function Prototypes                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the TPL5010 watchdog state machine.
 *
 * @param wd Pointer to watchdog state.
 * @param boot_timestamp_ms Current system boot tick in milliseconds.
 */
void watchdog_tpl5010_init(watchdog_tpl5010_t *wd, uint32_t boot_timestamp_ms);

/**
 * @brief Report a thread/task as alive and healthy.
 *
 * Thread-safe: performs atomic bitwise OR into the health mask.
 *
 * @param wd Pointer to watchdog state.
 * @param task_mask Bit flag corresponding to reporting task.
 */
void watchdog_tpl5010_report_healthy(watchdog_tpl5010_t *wd, uint8_t task_mask);

/**
 * @brief Periodic 20-second watchdog evaluation routine.
 *
 * Inspects task_health_mask. If exactly 0x0F, initiates a 20 ms HIGH pulse on DONE
 * and clears the health mask. If any task is missing, suppresses the kick.
 *
 * @param wd Pointer to watchdog state.
 * @param current_time_ms Current system tick in milliseconds.
 * @return true if a DONE pulse was initiated, false if suppressed.
 */
bool watchdog_tpl5010_check_and_kick(watchdog_tpl5010_t *wd, uint32_t current_time_ms);

/**
 * @brief Fast GPIO state updater (called from 1 kHz tick / timer interrupt).
 *
 * Manages de-assertion of the 20 ms DONE pulse and models TPL5010 30-second timeout.
 *
 * @param wd Pointer to watchdog state.
 * @param current_time_ms Current system tick in milliseconds.
 */
void watchdog_tpl5010_update_gpio(watchdog_tpl5010_t *wd, uint32_t current_time_ms);

/**
 * @brief Check if external TPL5010 hardware reset condition has been tripped.
 */
bool watchdog_tpl5010_is_reset_tripped(const watchdog_tpl5010_t *wd);

/**
 * @brief Calculate milliseconds remaining until TPL5010 hardware reset fires.
 */
uint32_t watchdog_tpl5010_get_time_until_reset_ms(const watchdog_tpl5010_t *wd,
                                                  uint32_t current_time_ms);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_TPL5010_H */

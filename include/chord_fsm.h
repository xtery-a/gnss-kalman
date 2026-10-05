/**
 * @file chord_fsm.h
 * @brief Tactical 4-Button Chording Input State Machine for Extreme Alpine Gloves.
 *
 * Physical Interface:
 *  - 4 discrete active-low micro-switches (SW1, SW2, SW3, SW4).
 *  - Deterministic 100 Hz (10 ms) polling tick.
 *  - 50 ms temporal debouncing integrator (rejects mechanical contact bounce).
 *  - Distinguishes Short Release (< 800 ms), Long Hold (>= 800 ms), and
 *    Tactical Emergency Beacon (>= 3000 ms).
 */

#ifndef CHORD_FSM_H
#define CHORD_FSM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 4-Switch Hardware Bitmasks                                                 */
/* -------------------------------------------------------------------------- */
#define CHORD_SW1_MASK              (0x01U)  /* Bit 0 */
#define CHORD_SW2_MASK              (0x02U)  /* Bit 1 */
#define CHORD_SW3_MASK              (0x04U)  /* Bit 2 */
#define CHORD_SW4_MASK              (0x08U)  /* Bit 3 */
#define CHORD_ALL_SW_MASK           (0x0FU)

/* Timing thresholds in milliseconds */
#define CHORD_DEBOUNCE_MS           (50U)    /* 50 ms contact stability */
#define CHORD_LONG_HOLD_MS          (800U)   /* 800 ms long press */
#define CHORD_EMERGENCY_HOLD_MS     (3000U)  /* 3000 ms SOS beacon press */

/* -------------------------------------------------------------------------- */
/* Event & Action Types                                                       */
/* -------------------------------------------------------------------------- */
typedef enum {
    CHORD_EVT_NONE = 0,
    CHORD_EVT_SHORT_RELEASE,   /**< Released after 50ms <= t < 800ms */
    CHORD_EVT_LONG_HOLD,       /**< Held continuously for t >= 800ms */
    CHORD_EVT_EMERGENCY_HOLD   /**< Held continuously for t >= 3000ms */
} chord_event_type_t;

typedef enum {
    CHORD_ACTION_NONE = 0,
    CHORD_ACTION_PAN_UP_NAV_NEXT,    /**< 0x01 (SW1) */
    CHORD_ACTION_PAN_DOWN_NAV_PREV,  /**< 0x08 (SW4) */
    CHORD_ACTION_ZOOM_IN,            /**< 0x03 (SW1 + SW2) */
    CHORD_ACTION_ZOOM_OUT,           /**< 0x0C (SW3 + SW4) */
    CHORD_ACTION_LAYER_TOGGLE,       /**< 0x06 (SW2 + SW3) */
    CHORD_ACTION_MARK_WAYPOINT,      /**< 0x09 (SW1 + SW4) short release */
    CHORD_ACTION_EMERGENCY_BEACON,   /**< 0x09 (SW1 + SW4) 3s hold */
    CHORD_ACTION_UNKNOWN_CHORD       /**< Other valid chord combinations */
} chord_action_t;

typedef struct {
    chord_event_type_t event_type;
    uint8_t            chord_mask;    /**< Combined 4-bit switch mask (0x01..0x0F) */
    chord_action_t     action;        /**< Decoded tactical action */
    uint32_t           duration_ms;   /**< Total active duration in milliseconds */
} chord_event_t;

/* -------------------------------------------------------------------------- */
/* FSM State Context                                                          */
/* -------------------------------------------------------------------------- */
typedef enum {
    CHORD_STATE_IDLE = 0,
    CHORD_STATE_ACTIVE_PRESS,
    CHORD_STATE_HOLD_ACTIVE
} chord_state_t;

typedef struct {
    chord_state_t state;
    uint8_t       raw_prev;           /**< Raw switch input from preceding tick */
    uint8_t       stable_mask;        /**< Debounced switch mask */
    uint8_t       accumulated_chord;  /**< Accumulated chord mask across press window */
    uint16_t      debounce_ms;        /**< Stable counter accumulator */
    uint32_t      press_duration_ms;  /**< Active chord duration */
    bool          hold_emitted;       /**< Prevents duplicate hold events */
    bool          emergency_emitted;  /**< Prevents duplicate emergency events */
} chord_fsm_t;

/* -------------------------------------------------------------------------- */
/* Public API Functions                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initializes the chording input FSM state.
 */
void chord_fsm_init(chord_fsm_t *fsm);

/**
 * @brief Periodic 100 Hz (10 ms) clock tick handler.
 * Feeds raw 4-bit switch state into debounce integrator and evaluates chording logic.
 *
 * @param fsm FSM context
 * @param raw_sw_mask Raw switch input (1 = pressed, 0 = released). Active-low pins must be inverted before passing.
 * @param delta_ms Elapsed time since last tick (typically 10 ms)
 * @param out_evt Pointer to receive emitted event (if function returns true)
 * @return true if a tactical event was generated, false otherwise
 */
bool chord_fsm_tick(chord_fsm_t *fsm, uint8_t raw_sw_mask, uint32_t delta_ms, chord_event_t *out_evt);

/**
 * @brief Resolves a 4-bit chord mask and event type to a pre-defined tactical action.
 */
chord_action_t chord_resolve_action(uint8_t chord_mask, chord_event_type_t evt_type);

/**
 * @brief Returns human-readable string representation of a tactical action.
 */
const char *chord_action_name(chord_action_t action);

#ifdef __cplusplus
}
#endif

#endif /* CHORD_FSM_H */

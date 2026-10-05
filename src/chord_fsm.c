/**
 * @file chord_fsm.c
 * @brief Tactical 4-Button Chording Input State Machine for Extreme Alpine Gloves.
 */

#include "chord_fsm.h"
#include <string.h>

void chord_fsm_init(chord_fsm_t *fsm) {
    if (!fsm) return;
    memset(fsm, 0, sizeof(chord_fsm_t));
    fsm->state = CHORD_STATE_IDLE;
}

chord_action_t chord_resolve_action(uint8_t chord_mask, chord_event_type_t evt_type) {
    chord_mask &= CHORD_ALL_SW_MASK;

    switch (chord_mask) {
        case CHORD_SW1_MASK: /* 0x01 */
            return CHORD_ACTION_PAN_UP_NAV_NEXT;

        case CHORD_SW4_MASK: /* 0x08 */
            return CHORD_ACTION_PAN_DOWN_NAV_PREV;

        case (CHORD_SW1_MASK | CHORD_SW2_MASK): /* 0x03 */
            return CHORD_ACTION_ZOOM_IN;

        case (CHORD_SW3_MASK | CHORD_SW4_MASK): /* 0x0C */
            return CHORD_ACTION_ZOOM_OUT;

        case (CHORD_SW2_MASK | CHORD_SW3_MASK): /* 0x06 */
            return CHORD_ACTION_LAYER_TOGGLE;

        case (CHORD_SW1_MASK | CHORD_SW4_MASK): /* 0x09 */
            if (evt_type == CHORD_EVT_EMERGENCY_HOLD) {
                return CHORD_ACTION_EMERGENCY_BEACON;
            } else {
                return CHORD_ACTION_MARK_WAYPOINT;
            }

        default:
            return (chord_mask != 0) ? CHORD_ACTION_UNKNOWN_CHORD : CHORD_ACTION_NONE;
    }
}

const char *chord_action_name(chord_action_t action) {
    switch (action) {
        case CHORD_ACTION_PAN_UP_NAV_NEXT:   return "PAN_UP / NAV_NEXT";
        case CHORD_ACTION_PAN_DOWN_NAV_PREV: return "PAN_DOWN / NAV_PREV";
        case CHORD_ACTION_ZOOM_IN:           return "ZOOM_IN";
        case CHORD_ACTION_ZOOM_OUT:          return "ZOOM_OUT";
        case CHORD_ACTION_LAYER_TOGGLE:      return "LAYER_TOGGLE (Map/Sky/Ele)";
        case CHORD_ACTION_MARK_WAYPOINT:     return "MARK_WAYPOINT";
        case CHORD_ACTION_EMERGENCY_BEACON:  return "EMERGENCY_BEACON (SOS 3s Hold)";
        case CHORD_ACTION_UNKNOWN_CHORD:     return "UNKNOWN_CHORD";
        default:                             return "NONE";
    }
}

bool chord_fsm_tick(chord_fsm_t *fsm, uint8_t raw_sw_mask, uint32_t delta_ms, chord_event_t *out_evt) {
    if (!fsm || !out_evt) return false;

    raw_sw_mask &= CHORD_ALL_SW_MASK;
    out_evt->event_type = CHORD_EVT_NONE;
    out_evt->chord_mask = 0;
    out_evt->action = CHORD_ACTION_NONE;
    out_evt->duration_ms = 0;

    /* ---------------------------------------------------------------------- */
    /* Temporal Debouncing Integrator (50 ms threshold)                       */
    /* ---------------------------------------------------------------------- */
    if (raw_sw_mask == fsm->raw_prev) {
        if (fsm->debounce_ms < 0xFFFF - delta_ms) {
            fsm->debounce_ms = (uint16_t)(fsm->debounce_ms + delta_ms);
        }
        if (fsm->debounce_ms >= CHORD_DEBOUNCE_MS) {
            fsm->stable_mask = raw_sw_mask;
        }
    } else {
        fsm->raw_prev = raw_sw_mask;
        fsm->debounce_ms = (uint16_t)delta_ms;
    }

    /* ---------------------------------------------------------------------- */
    /* Tactical Chording State Machine                                        */
    /* ---------------------------------------------------------------------- */
    switch (fsm->state) {
        case CHORD_STATE_IDLE:
            if (fsm->stable_mask != 0) {
                fsm->state = CHORD_STATE_ACTIVE_PRESS;
                fsm->accumulated_chord = fsm->stable_mask;
                fsm->press_duration_ms = 0;
                fsm->hold_emitted = false;
                fsm->emergency_emitted = false;
            }
            break;

        case CHORD_STATE_ACTIVE_PRESS:
        case CHORD_STATE_HOLD_ACTIVE:
            if (fsm->stable_mask != 0) {
                /* Accumulate chord mask during the multi-switch press gesture */
                fsm->accumulated_chord |= fsm->stable_mask;
                fsm->press_duration_ms += delta_ms;

                /* Check 3000 ms Emergency Beacon Hold for SW1 + SW4 (0x09) */
                if (fsm->accumulated_chord == (CHORD_SW1_MASK | CHORD_SW4_MASK)) {
                    if (fsm->press_duration_ms >= CHORD_EMERGENCY_HOLD_MS && !fsm->emergency_emitted) {
                        fsm->emergency_emitted = true;
                        fsm->state = CHORD_STATE_HOLD_ACTIVE;

                        out_evt->event_type = CHORD_EVT_EMERGENCY_HOLD;
                        out_evt->chord_mask = fsm->accumulated_chord;
                        out_evt->action = CHORD_ACTION_EMERGENCY_BEACON;
                        out_evt->duration_ms = fsm->press_duration_ms;
                        return true;
                    }
                }

                /* Check Standard 800 ms Long Hold */
                if (fsm->press_duration_ms >= CHORD_LONG_HOLD_MS && !fsm->hold_emitted) {
                    /* For SW1+SW4, do not emit short hold if user is aiming for 3s SOS beacon */
                    if (fsm->accumulated_chord != (CHORD_SW1_MASK | CHORD_SW4_MASK)) {
                        fsm->hold_emitted = true;
                        fsm->state = CHORD_STATE_HOLD_ACTIVE;

                        out_evt->event_type = CHORD_EVT_LONG_HOLD;
                        out_evt->chord_mask = fsm->accumulated_chord;
                        out_evt->action = chord_resolve_action(fsm->accumulated_chord, CHORD_EVT_LONG_HOLD);
                        out_evt->duration_ms = fsm->press_duration_ms;
                        return true;
                    }
                }
            } else {
                /* Switches released (stable_mask == 0) */
                if (!fsm->hold_emitted && !fsm->emergency_emitted && fsm->press_duration_ms < CHORD_LONG_HOLD_MS) {
                    /* Emits short release event */
                    out_evt->event_type = CHORD_EVT_SHORT_RELEASE;
                    out_evt->chord_mask = fsm->accumulated_chord;
                    out_evt->action = chord_resolve_action(fsm->accumulated_chord, CHORD_EVT_SHORT_RELEASE);
                    out_evt->duration_ms = fsm->press_duration_ms;

                    fsm->state = CHORD_STATE_IDLE;
                    return true;
                }

                fsm->state = CHORD_STATE_IDLE;
            }
            break;

        default:
            fsm->state = CHORD_STATE_IDLE;
            break;
    }

    return false;
}

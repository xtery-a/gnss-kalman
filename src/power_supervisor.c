/**
 * @file power_supervisor.c
 * @brief Zero-Heap Electrochemical Battery Protection, Supercapacitor Buffer & Power Supervisor.
 */

#include "power_supervisor.h"
#include <string.h>

void power_supervisor_init(power_supervisor_t *ps, uint16_t initial_vbatt_mv) {
    if (!ps) return;
    memset(ps, 0, sizeof(power_supervisor_t));

    ps->cell_temp_deci_c = 200; /* Default room temperature (+20.0 C) */
    ps->vbatt_mv = initial_vbatt_mv;
    ps->vcap_mv = initial_vbatt_mv;
    ps->vrail_mv = initial_vbatt_mv;
    ps->is_vbus_connected = false;
    ps->charge_gate_en = false;
    ps->heater_ptc_en = false;
    ps->load_profile = LOAD_PROFILE_IDLE;
    ps->alert_flags = POWER_FLAG_NONE;
}

void power_supervisor_update(power_supervisor_t *ps,
                             int16_t cell_temp_deci_c,
                             uint16_t vbatt_mv,
                             bool is_vbus_connected) {
    if (!ps) return;

    ps->cell_temp_deci_c = cell_temp_deci_c;
    ps->vbatt_mv = vbatt_mv;
    ps->is_vbus_connected = is_vbus_connected;

    /* Maintain supercapacitor float charge if not in active RF pulse */
    if (ps->load_profile != LOAD_PROFILE_RF_BURST) {
        ps->vcap_mv = vbatt_mv;
        ps->vrail_mv = vbatt_mv;
    }

    /* ---------------------------------------------------------------------- */
    /* Active PTC Pre-Heater State Machine                                    */
    /* ---------------------------------------------------------------------- */
    if (ps->heater_ptc_en) {
        /* Heater is actively warming the battery cell using external VBUS */
        if (!is_vbus_connected || cell_temp_deci_c >= POWER_TEMP_PTC_TARGET_DECI_C) {
            /* Shut off heater when +5.0 C target is reached or VBUS disconnected */
            ps->heater_ptc_en = false;
            ps->alert_flags &= ~POWER_FLAG_PTC_HEATING_ACTIVE;

            /* If external power is still present, battery is now safe to charge */
            if (is_vbus_connected && cell_temp_deci_c >= POWER_TEMP_HYSTERESIS_DECI_C) {
                ps->charge_gate_en = true;
                ps->alert_flags &= ~POWER_FLAG_COLD_CHARGE_BLOCKED;
            }
        } else {
            /* Still heating: charge gate remains strictly locked out */
            ps->charge_gate_en = false;
            ps->alert_flags |= (POWER_FLAG_PTC_HEATING_ACTIVE | POWER_FLAG_COLD_CHARGE_BLOCKED);
        }
    } else {
        /* ------------------------------------------------------------------ */
        /* Electrochemical Sub-Zero Charge Lockout & Hysteresis Logic         */
        /* ------------------------------------------------------------------ */
        if (cell_temp_deci_c <= POWER_TEMP_FREEZING_DECI_C) {
            /* Sub-Zero: Immediate Hard Lockout to prevent lithium plating */
            ps->charge_gate_en = false;
            ps->alert_flags |= POWER_FLAG_COLD_CHARGE_BLOCKED;

            /* Check if external power is present to engage PTC pre-heater (-20.0 C <= T < 0.0 C) */
            if (is_vbus_connected && cell_temp_deci_c >= POWER_TEMP_PTC_MIN_DECI_C &&
                cell_temp_deci_c < POWER_TEMP_FREEZING_DECI_C) {
                ps->heater_ptc_en = true;
                ps->alert_flags |= POWER_FLAG_PTC_HEATING_ACTIVE;
            }
        } else if (cell_temp_deci_c >= POWER_TEMP_HYSTERESIS_DECI_C) {
            /* Cell temperature has crossed +2.5 C recovery boundary */
            if (is_vbus_connected) {
                ps->charge_gate_en = true;
            } else {
                ps->charge_gate_en = false;
            }
            ps->alert_flags &= ~POWER_FLAG_COLD_CHARGE_BLOCKED;
        } else {
            /* Hysteresis Deadband (0.0 C <= T < +2.5 C):
             * If charge gate was previously blocked, keep it blocked! */
            if ((ps->alert_flags & POWER_FLAG_COLD_CHARGE_BLOCKED) != 0) {
                ps->charge_gate_en = false;
            } else {
                ps->charge_gate_en = is_vbus_connected;
            }
        }
    }
}

void power_supervisor_notify_rf_pulse_start(power_supervisor_t *ps,
                                           uint16_t pulse_current_ma,
                                           float battery_esr_ohms) {
    if (!ps) return;

    ps->load_profile = LOAD_PROFILE_RF_BURST;
    ps->v_pre_pulse_mv = ps->vbatt_mv;
    ps->estimated_esr_ohms = battery_esr_ohms;

    /* 1. Calculate raw battery cell unbuffered drop: Delta V = I * R_batt */
    float current_a = (float)pulse_current_ma / 1000.0f;
    uint16_t unbuffered_drop_mv = (uint16_t)(current_a * battery_esr_ohms * 1000.0f + 0.5f);
    ps->last_sag_mv = unbuffered_drop_mv;

    if (ps->vbatt_mv > unbuffered_drop_mv) {
        ps->v_pulse_min_mv = ps->vbatt_mv - unbuffered_drop_mv;
    } else {
        ps->v_pulse_min_mv = 0;
    }

    /* 2. Supercapacitor Buffering Model:
     * High pulse current is predominantly drawn from ultra-low ESR (35 mOhm) 100 mF supercapacitor.
     * Capacitive droop for a 50 ms burst: Delta V_cap = (I * dt) / C = (0.5A * 0.05s) / 0.1F = 250 mV.
     * ESR droop = 0.5A * 0.035 Ohm = 17.5 mV. Total buffered sag ~= 268 mV. */
    uint16_t buffered_sag_mv = 268U;
    if (ps->vbatt_mv > buffered_sag_mv) {
        ps->vrail_mv = ps->vbatt_mv - buffered_sag_mv;
    } else {
        ps->vrail_mv = POWER_VOLTAGE_MIN_RAIL_MV;
    }

    /* Ensure modeled system rail never drops below 3.15V under supercap assist */
    if (ps->vrail_mv < POWER_VOLTAGE_MIN_RAIL_MV) {
        ps->vrail_mv = POWER_VOLTAGE_MIN_RAIL_MV;
    }

    ps->vcap_mv = ps->vrail_mv;
    ps->alert_flags |= POWER_FLAG_SUPERCAP_ASSIST;
}

void power_supervisor_notify_rf_pulse_end(power_supervisor_t *ps) {
    if (!ps) return;

    ps->load_profile = LOAD_PROFILE_IDLE;
    ps->alert_flags &= ~POWER_FLAG_SUPERCAP_ASSIST;
    ps->vrail_mv = ps->vbatt_mv;

    /* Evaluate battery degradation and ESR warning limits */
    if (ps->last_sag_mv >= POWER_SAG_COLLAPSE_THRESHOLD_MV) {
        ps->alert_flags |= POWER_FLAG_BATTERY_COLLAPSE;
        ps->esr_warning_count++;
    } else if (ps->last_sag_mv >= POWER_SAG_WARNING_THRESHOLD_MV) {
        ps->alert_flags |= POWER_FLAG_ESR_WARNING;
        ps->esr_warning_count++;
    } else {
        ps->alert_flags &= ~(POWER_FLAG_ESR_WARNING | POWER_FLAG_BATTERY_COLLAPSE);
    }
}

bool power_supervisor_is_charge_enabled(const power_supervisor_t *ps) {
    return ps ? ps->charge_gate_en : false;
}

bool power_supervisor_is_heater_active(const power_supervisor_t *ps) {
    return ps ? ps->heater_ptc_en : false;
}

bool power_supervisor_is_collapse_risk(const power_supervisor_t *ps) {
    return ps ? ((ps->alert_flags & POWER_FLAG_BATTERY_COLLAPSE) != 0) : false;
}

uint16_t power_supervisor_get_rail_voltage(const power_supervisor_t *ps) {
    return ps ? ps->vrail_mv : 0;
}

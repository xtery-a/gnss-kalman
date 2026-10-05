#include "battery_thermal_guard.h"
#include <math.h>

static batt_ntc_config_t g_ntc_cfg = {
    .ntc_r0_ohms = 10000.0f,
    .ntc_beta_k = 3950.0f,
    .pullup_r_ohms = 10000.0f,
    .vref_mv = 3300.0f
};

static bool g_charge_locked_latch = false;

void batt_guard_init(const batt_ntc_config_t *config) {
    if (config) {
        g_ntc_cfg = *config;
    }
    g_charge_locked_latch = false;
}

float batt_guard_calc_temperature_c(uint16_t adc_raw, uint16_t adc_max) {
    if (adc_raw == 0) return -50.0f;
    if (adc_raw >= adc_max) return 100.0f;

    float ratio = (float)adc_raw / (float)adc_max;
    float v_meas = ratio * g_ntc_cfg.vref_mv;

    /* R_ntc = R_pullup * V_meas / (V_ref - V_meas) */
    float v_drop = g_ntc_cfg.vref_mv - v_meas;
    if (v_drop <= 0.001f) return 100.0f;

    float r_ntc = g_ntc_cfg.pullup_r_ohms * (v_meas / v_drop);
    if (r_ntc <= 0.0f) return -50.0f;

    /* Beta model: 1/T = 1/T0 + (1/Beta) * ln(R/R0) */
    float t0_kelvin = 298.15f; /* 25 C */
    float inv_t = (1.0f / t0_kelvin) + (1.0f / g_ntc_cfg.ntc_beta_k) * logf(r_ntc / g_ntc_cfg.ntc_r0_ohms);
    float t_celsius = (1.0f / inv_t) - 273.15f;

    return t_celsius;
}

void batt_guard_update(
    float temp_c,
    uint16_t v_batt_mv,
    bool is_vbus_connected,
    batt_guard_status_t *status
) {
    if (!status) return;

    status->current_temp_c = temp_c;
    status->v_idle_mv = v_batt_mv;

    /* 1. Strict Electrochemical Charge Prohibition Logic (< 0 C) with Hysteresis */
    if (temp_c < BATT_CHARGE_LOCKOUT_TEMP_C || temp_c > BATT_OVERHEAT_LOCKOUT_TEMP_C) {
        g_charge_locked_latch = true;
    } else if (temp_c >= BATT_CHARGE_RESUME_TEMP_C && temp_c <= 45.0f) {
        g_charge_locked_latch = false;
    }

    status->charge_enable_allowed = !g_charge_locked_latch;

    /* 2. Operational State Transitions */
    if (temp_c > BATT_OVERHEAT_LOCKOUT_TEMP_C) {
        status->state = BATT_STATE_OVERHEAT_LOCKOUT;
        status->rf_power_throttle_active = true;
        status->heater_enable_request = false;
    } else if (temp_c < BATT_CRITICAL_SHUTDOWN_TEMP_C) {
        status->state = BATT_STATE_CRITICAL_SHUTDOWN;
        status->rf_power_throttle_active = true;
        status->heater_enable_request = is_vbus_connected;
    } else if (temp_c < BATT_THROTTLE_TEMP_C) {
        status->state = BATT_STATE_SUBZERO_THROTTLED;
        status->rf_power_throttle_active = true;
        status->heater_enable_request = is_vbus_connected;
    } else if (temp_c < BATT_CHARGE_LOCKOUT_TEMP_C) {
        status->state = BATT_STATE_COLD_NO_CHARGE;
        status->rf_power_throttle_active = false;
        status->heater_enable_request = is_vbus_connected;
    } else {
        status->state = BATT_STATE_NORMAL;
        status->rf_power_throttle_active = false;
        status->heater_enable_request = false;
    }
}

bool batt_guard_record_burst_sag(
    uint16_t v_idle_mv,
    uint16_t v_burst_mv,
    uint16_t burst_current_ma,
    batt_guard_status_t *status
) {
    if (!status) return false;

    status->v_idle_mv = v_idle_mv;
    status->v_loaded_mv = v_burst_mv;

    if (v_idle_mv > v_burst_mv && burst_current_ma > 0) {
        uint32_t delta_v = (uint32_t)(v_idle_mv - v_burst_mv);
        /* ESR (mOhm) = (delta_v_mV * 1000) / current_mA */
        status->estimated_esr_mohm = (uint16_t)((delta_v * 1000U) / (uint32_t)burst_current_ma);
    }

    if (v_burst_mv <= BATT_SAG_THRESHOLD_CRIT_MV) {
        status->sag_events_count++;
        status->rf_power_throttle_active = true;
        return true; /* Critical imminent brownout */
    }

    if (v_burst_mv <= BATT_SAG_THRESHOLD_WARN_MV) {
        status->sag_events_count++;
    }

    return false;
}

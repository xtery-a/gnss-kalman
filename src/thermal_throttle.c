/**
 * @file thermal_throttle.c
 * @brief Dynamic 4-Tier Thermal & Low-Voltage Throttling State Machine.
 */

#include "thermal_throttle.h"
#include <string.h>

static const throttle_profile_t TIER_PROFILES[4] = {
    /* TIER 0: Nominal (T > +5.0 C) */
    {
        .tier = THROTTLE_TIER_0_NOMINAL,
        .core_clock_mhz = 160,
        .lora_rf_power_dbm = 22,
        .display_refresh_hz = 5.0f,
        .gnss_fix_rate_hz = 5.0f,
        .gnss_fix_interval_ms = 200,
        .supercap_precharge_check = false,
        .dirty_line_only_display = false,
        .gnss_duty_cycling = false
    },
    /* TIER 1: Cold Caution (-10.0 C <= T <= +5.0 C) */
    {
        .tier = THROTTLE_TIER_1_COLD_CAUTION,
        .core_clock_mhz = 160,
        .lora_rf_power_dbm = 22,
        .display_refresh_hz = 2.0f,
        .gnss_fix_rate_hz = 2.0f,
        .gnss_fix_interval_ms = 500,
        .supercap_precharge_check = true,
        .dirty_line_only_display = false,
        .gnss_duty_cycling = false
    },
    /* TIER 2: Sub-Zero Throttling (-25.0 C <= T < -10.0 C) */
    {
        .tier = THROTTLE_TIER_2_SUBZERO,
        .core_clock_mhz = 64,
        .lora_rf_power_dbm = 14,
        .display_refresh_hz = 1.0f,
        .gnss_fix_rate_hz = 1.0f,
        .gnss_fix_interval_ms = 1000,
        .supercap_precharge_check = true,
        .dirty_line_only_display = true,
        .gnss_duty_cycling = false
    },
    /* TIER 3: Survival Mode (T < -25.0 C or V_batt < 3300 mV) */
    {
        .tier = THROTTLE_TIER_3_SURVIVAL,
        .core_clock_mhz = 32,
        .lora_rf_power_dbm = 10,
        .display_refresh_hz = 0.2f,
        .gnss_fix_rate_hz = 0.033333f,
        .gnss_fix_interval_ms = 30000,
        .supercap_precharge_check = true,
        .dirty_line_only_display = true,
        .gnss_duty_cycling = true
    }
};

void thermal_throttle_init(thermal_throttle_t *tt) {
    if (!tt) return;
    memset(tt, 0, sizeof(thermal_throttle_t));

    tt->current_tier = THROTTLE_TIER_0_NOMINAL;
    tt->current_profile = TIER_PROFILES[THROTTLE_TIER_0_NOMINAL];
    tt->last_temp_deci_c = 200; /* +20.0 C */
    tt->last_vbatt_mv = 3800;   /* 3.80 V */
    tt->tier_transitions_count = 0;
}

throttle_tier_t thermal_throttle_update(thermal_throttle_t *tt,
                                        int16_t temp_deci_c,
                                        uint16_t vbatt_mv) {
    if (!tt) return THROTTLE_TIER_0_NOMINAL;

    tt->last_temp_deci_c = temp_deci_c;
    tt->last_vbatt_mv = vbatt_mv;

    throttle_tier_t target_tier;

    /* Rule 4: TIER 3 (Survival Mode: T < -25.0 C or V_batt < 3300 mV) */
    if (temp_deci_c < THROTTLE_TEMP_TIER2_MIN_DECI_C || vbatt_mv < THROTTLE_VBATT_SURVIVAL_MV) {
        target_tier = THROTTLE_TIER_3_SURVIVAL;
    }
    /* Rule 3: TIER 2 (Sub-Zero Throttling: -25.0 C <= T < -10.0 C) */
    else if (temp_deci_c < THROTTLE_TEMP_TIER1_MIN_DECI_C) {
        target_tier = THROTTLE_TIER_2_SUBZERO;
    }
    /* Rule 2: TIER 1 (Cold Caution: -10.0 C <= T <= +5.0 C) */
    else if (temp_deci_c <= THROTTLE_TEMP_TIER0_MIN_DECI_C) {
        target_tier = THROTTLE_TIER_1_COLD_CAUTION;
    }
    /* Rule 1: TIER 0 (Nominal: T > +5.0 C) */
    else {
        target_tier = THROTTLE_TIER_0_NOMINAL;
    }

    if (target_tier != tt->current_tier) {
        tt->current_tier = target_tier;
        tt->current_profile = TIER_PROFILES[target_tier];
        tt->tier_transitions_count++;
    }

    return tt->current_tier;
}

const throttle_profile_t* thermal_throttle_get_profile(const thermal_throttle_t *tt) {
    return tt ? &tt->current_profile : &TIER_PROFILES[0];
}

const char* thermal_throttle_tier_name(throttle_tier_t tier) {
    switch (tier) {
        case THROTTLE_TIER_0_NOMINAL:      return "TIER 0 (Nominal: 160MHz, +22dBm, 5Hz Display/GNSS)";
        case THROTTLE_TIER_1_COLD_CAUTION: return "TIER 1 (Cold Caution: 160MHz, Supercap-Check, 2Hz Display/GNSS)";
        case THROTTLE_TIER_2_SUBZERO:      return "TIER 2 (Sub-Zero: 64MHz, +14dBm, 1Hz Dirty-Line, 1Hz GNSS)";
        case THROTTLE_TIER_3_SURVIVAL:     return "TIER 3 (Survival: 32MHz, +10dBm, 0.2Hz Inverted, 1 Fix/30s)";
        default:                           return "UNKNOWN";
    }
}

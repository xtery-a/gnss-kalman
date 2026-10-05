/**
 * @file thermal_throttle.h
 * @brief Dynamic 4-Tier Thermal & Low-Voltage Throttling State Machine.
 *
 * Operational Tiers:
 *  - TIER 0 (Nominal: T > +5.0 C):
 *      160 MHz clock, +22 dBm LoRa, 5 Hz display refresh, 5 Hz GNSS.
 *  - TIER 1 (Cold Caution: -10.0 C <= T <= +5.0 C):
 *      160 MHz clock, Supercap pre-charge check, 2 Hz display, 2 Hz GNSS.
 *  - TIER 2 (Sub-Zero Throttling: -25.0 C <= T < -10.0 C):
 *      64 MHz clock, +14 dBm LoRa (<250 mA pulse), 1 Hz dirty-line display, 1 Hz GNSS.
 *  - TIER 3 (Survival Mode: T < -25.0 C or V_batt < 3300 mV):
 *      32 MHz clock, +10 dBm emergency bursts, 0.2 Hz display (inverted data), 1 fix/30s GNSS.
 */

#ifndef THERMAL_THROTTLE_H
#define THERMAL_THROTTLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Thermal Throttling Boundaries (in deci-Celsius: 0.1 deg C)                 */
/* -------------------------------------------------------------------------- */
#define THROTTLE_TEMP_TIER0_MIN_DECI_C  (50)    /**< +5.0 deg C */
#define THROTTLE_TEMP_TIER1_MIN_DECI_C  (-100)  /**< -10.0 deg C */
#define THROTTLE_TEMP_TIER2_MIN_DECI_C  (-250)  /**< -25.0 deg C */

#define THROTTLE_VBATT_SURVIVAL_MV      (3300U) /**< 3.30 V Low-Battery Survival Threshold */

/* -------------------------------------------------------------------------- */
/* Throttling Tier Enumeration & Profile Structure                            */
/* -------------------------------------------------------------------------- */
typedef enum {
    THROTTLE_TIER_0_NOMINAL = 0,    /**< Full performance */
    THROTTLE_TIER_1_COLD_CAUTION,  /**< Cold caution: reduced display/GNSS rate */
    THROTTLE_TIER_2_SUBZERO,       /**< Sub-zero: 64 MHz, +14 dBm LoRa */
    THROTTLE_TIER_3_SURVIVAL       /**< Deep freeze / low battery: 32 MHz, 1 fix / 30s */
} throttle_tier_t;

typedef struct {
    throttle_tier_t tier;
    uint16_t        core_clock_mhz;           /**< CPU Core frequency: 160, 64, or 32 MHz */
    int8_t          lora_rf_power_dbm;        /**< RF TX Power: +22, +14, or +10 dBm */
    float           display_refresh_hz;       /**< MIP display refresh: 5.0, 2.0, 1.0, or 0.2 Hz */
    float           gnss_fix_rate_hz;         /**< GNSS Fix rate: 5.0, 2.0, 1.0, or 0.0333 Hz */
    uint32_t        gnss_fix_interval_ms;     /**< Fix interval: 200, 500, 1000, or 30000 ms */
    bool            supercap_precharge_check; /**< Verify supercap ready prior to RF burst */
    bool            dirty_line_only_display;  /**< Restrict display DMA to dirty lines only */
    bool            gnss_duty_cycling;        /**< Power-down GNSS between 30-second fixes */
} throttle_profile_t;

typedef struct {
    throttle_tier_t    current_tier;
    throttle_profile_t current_profile;
    int16_t            last_temp_deci_c;
    uint16_t           last_vbatt_mv;
    uint32_t           tier_transitions_count;
} thermal_throttle_t;

/* -------------------------------------------------------------------------- */
/* Function Prototypes                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the thermal throttling state machine.
 */
void thermal_throttle_init(thermal_throttle_t *tt);

/**
 * @brief Evaluate temperature and battery voltage to update operational profile.
 *
 * @param tt Pointer to state machine.
 * @param temp_deci_c Cell or ambient temperature in 0.1 deg C (e.g. -152 = -15.2 C).
 * @param vbatt_mv Current battery voltage in millivolts.
 * @return Newly active operational tier.
 */
throttle_tier_t thermal_throttle_update(thermal_throttle_t *tt,
                                        int16_t temp_deci_c,
                                        uint16_t vbatt_mv);

/**
 * @brief Get pointer to currently active operational profile.
 */
const throttle_profile_t* thermal_throttle_get_profile(const thermal_throttle_t *tt);

/**
 * @brief Get human-readable string for an operational tier.
 */
const char* thermal_throttle_tier_name(throttle_tier_t tier);

#ifdef __cplusplus
}
#endif

#endif /* THERMAL_THROTTLE_H */

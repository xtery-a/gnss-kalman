/**
 * @file power_supervisor.h
 * @brief Zero-Heap Electrochemical Battery Protection, Supercapacitor Buffer & Power Supervisor.
 *
 * Physical & Electrochemical Safeguards:
 *  - Sub-zero lithium plating & dendrite formation prevention (Hard lockout T_cell < 0 deg C).
 *  - +2.5 deg C hysteresis before charge gate re-enablement.
 *  - Active PTC heating controller (-20 deg C <= T < 0 deg C -> heat until +5.0 deg C).
 *  - Real-time voltage sag (Delta V_sag = V_pre - V_pulse) and ESR estimation during RF transmit bursts.
 *  - Supercapacitor peak load buffer maintaining system rail voltage >= 3.15 V under sub-zero battery ESR spikes.
 */

#ifndef POWER_SUPERVISOR_H
#define POWER_SUPERVISOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Physical & Electrochemical Constants                                       */
/* -------------------------------------------------------------------------- */
#define POWER_TEMP_FREEZING_DECI_C      (0)     /**< 0.0 deg C (Cutoff threshold) */
#define POWER_TEMP_HYSTERESIS_DECI_C    (25)    /**< +2.5 deg C (Re-enablement threshold) */
#define POWER_TEMP_PTC_MIN_DECI_C       (-200)  /**< -20.0 deg C (Pre-heater lower limit) */
#define POWER_TEMP_PTC_TARGET_DECI_C    (50)    /**< +5.0 deg C (Pre-heater target shutoff) */

#define POWER_VOLTAGE_MIN_RAIL_MV       (3150U) /**< 3.15 V Minimum brownout-safe system rail */
#define POWER_VOLTAGE_COLLAPSE_MV       (3300U) /**< 3.30 V Emergency survival power threshold */

#define POWER_SAG_WARNING_THRESHOLD_MV  (400U)  /**< 400 mV Sag warning threshold */
#define POWER_SAG_COLLAPSE_THRESHOLD_MV (600U)  /**< 600 mV Battery collapse risk trip */

#define POWER_SUPERCAP_CAPACITANCE_MF   (100U)  /**< 100 mF (0.1 F) Supercapacitor buffer */
#define POWER_SUPERCAP_ESR_MOHM         (35U)   /**< 35 mOhm Supercap ultra-low ESR */

/* -------------------------------------------------------------------------- */
/* Enums and Alert Flags                                                      */
/* -------------------------------------------------------------------------- */
typedef enum {
    LOAD_PROFILE_IDLE = 0,      /**< Baseline low-power sleep/idle (~15 mA) */
    LOAD_PROFILE_GNSS_ACTIVE,   /**< Multi-band GNSS tracking (~65 mA) */
    LOAD_PROFILE_RF_BURST       /**< High-power LoRa / Iridium TX pulse (up to 500 mA) */
} power_load_profile_t;

#define POWER_FLAG_NONE                 (0x0000U)
#define POWER_FLAG_COLD_CHARGE_BLOCKED  (0x0001U) /**< Lithium plating lockout active */
#define POWER_FLAG_PTC_HEATING_ACTIVE   (0x0002U) /**< Active PTC pre-heater enabled */
#define POWER_FLAG_ESR_WARNING          (0x0004U) /**< Pulse sag >= 400 mV */
#define POWER_FLAG_BATTERY_COLLAPSE     (0x0008U) /**< Critical pulse sag >= 600 mV */
#define POWER_FLAG_SUPERCAP_ASSIST      (0x0010U) /**< Supercapacitor actively buffering load */

/* -------------------------------------------------------------------------- */
/* State Structure (Zero Heap - Static / BSS Allocation)                      */
/* -------------------------------------------------------------------------- */
typedef struct {
    int16_t              cell_temp_deci_c;   /**< Battery cell temperature in 0.1 deg C */
    uint16_t             vbatt_mv;           /**< Open-circuit / battery terminal voltage in mV */
    uint16_t             vcap_mv;            /**< Supercapacitor terminal voltage in mV */
    uint16_t             vrail_mv;           /**< System supply rail voltage in mV */
    bool                 is_vbus_connected;  /**< True if external 5V USB/charger is connected */
    bool                 charge_gate_en;     /**< Hardware charge FET gate enable */
    bool                 heater_ptc_en;      /**< Hardware PTC pre-heater MOSFET enable */
    power_load_profile_t load_profile;       /**< Active load profile */

    /* Pulse Sag & ESR Estimation */
    uint16_t             v_pre_pulse_mv;     /**< Battery voltage immediately prior to pulse */
    uint16_t             v_pulse_min_mv;     /**< Lowest battery terminal voltage during pulse */
    uint16_t             last_sag_mv;        /**< Delta V_sag = V_pre - V_pulse */
    float                estimated_esr_ohms; /**< Computed internal resistance R_batt */
    uint32_t             esr_warning_count;  /**< Counter of sag warnings */

    uint32_t             alert_flags;        /**< Active alert / status bitmask */
} power_supervisor_t;

/* -------------------------------------------------------------------------- */
/* Function Prototypes                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the power supervisor engine.
 *
 * @param ps Pointer to supervisor state.
 * @param initial_vbatt_mv Initial measured battery voltage in millivolts.
 */
void power_supervisor_init(power_supervisor_t *ps, uint16_t initial_vbatt_mv);

/**
 * @brief Periodic supervisor step update (typically called at 10 Hz to 50 Hz).
 *
 * Evaluates electrochemical safety boundaries, thermal hysteresis, PTC heater logic,
 * and supercapacitor float charge.
 *
 * @param ps Pointer to supervisor state.
 * @param cell_temp_deci_c Current cell temperature from 10k NTC (in 0.1 deg C).
 * @param vbatt_mv Unloaded battery voltage in mV.
 * @param is_vbus_connected True if external charging source is present.
 */
void power_supervisor_update(power_supervisor_t *ps,
                             int16_t cell_temp_deci_c,
                             uint16_t vbatt_mv,
                             bool is_vbus_connected);

/**
 * @brief Hook invoked immediately before triggering an RF transmit burst.
 *
 * Captures pre-pulse voltage and models supercapacitor current sharing.
 *
 * @param ps Pointer to supervisor state.
 * @param pulse_current_ma Expected burst current in mA (e.g. 500 mA).
 * @param battery_esr_ohms Modeled or measured battery ESR (e.g. 0.90 Ohm at -20 deg C).
 */
void power_supervisor_notify_rf_pulse_start(power_supervisor_t *ps,
                                           uint16_t pulse_current_ma,
                                           float battery_esr_ohms);

/**
 * @brief Hook invoked immediately after an RF transmit burst completes.
 *
 * Computes Delta V_sag, evaluates ESR degradation, checks collapse limits.
 *
 * @param ps Pointer to supervisor state.
 */
void power_supervisor_notify_rf_pulse_end(power_supervisor_t *ps);

/**
 * @brief Check if battery charging is actively allowed by hardware gate.
 */
bool power_supervisor_is_charge_enabled(const power_supervisor_t *ps);

/**
 * @brief Check if active PTC pre-heater is currently engaged.
 */
bool power_supervisor_is_heater_active(const power_supervisor_t *ps);

/**
 * @brief Check if battery collapse risk flag is tripped.
 */
bool power_supervisor_is_collapse_risk(const power_supervisor_t *ps);

/**
 * @brief Retrieve effective system rail voltage in mV.
 */
uint16_t power_supervisor_get_rail_voltage(const power_supervisor_t *ps);

#ifdef __cplusplus
}
#endif

#endif /* POWER_SUPERVISOR_H */

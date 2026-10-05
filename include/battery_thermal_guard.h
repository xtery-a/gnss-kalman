#ifndef BATTERY_THERMAL_GUARD_H
#define BATTERY_THERMAL_GUARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sub-Zero Battery Thermal & Voltage Sag Guard
 * Enforces electrochemical safety boundaries:
 * 1. Absolute hardware/firmware charge lockout at T < 0 deg C (prevents dendrite lithium plating)
 * 2. Peak current and RF TX throttling at T < -15 deg C (prevents ESR voltage sag brownout)
 * 3. Steinhart-Hart / Beta model NTC temperature conversion
 * 4. Supercapacitor buffering & ESR estimation
 */

#define BATT_CHARGE_LOCKOUT_TEMP_C      0.0f
#define BATT_CHARGE_RESUME_TEMP_C       2.5f    /* Hysteresis for charge recovery */
#define BATT_THROTTLE_TEMP_C           -15.0f
#define BATT_CRITICAL_SHUTDOWN_TEMP_C   -30.0f
#define BATT_OVERHEAT_LOCKOUT_TEMP_C    50.0f

#define BATT_SAG_THRESHOLD_WARN_MV      3300U   /* Warn if loaded voltage dips below 3.3V */
#define BATT_SAG_THRESHOLD_CRIT_MV      3050U   /* Imminent MCU brownout threshold */

typedef enum {
    BATT_STATE_NORMAL = 0,          /* T >= 0 C, Charging allowed, full RF power */
    BATT_STATE_COLD_NO_CHARGE,      /* -15 C <= T < 0 C, CHARGE LOCKED, full discharge */
    BATT_STATE_SUBZERO_THROTTLED,   /* -30 C <= T < -15 C, CHARGE LOCKED, RF power capped */
    BATT_STATE_CRITICAL_SHUTDOWN,   /* T < -30 C, Emergency safe sleep */
    BATT_STATE_OVERHEAT_LOCKOUT     /* T > 50 C, Thermal runaway prevention */
} batt_operational_state_t;

typedef struct {
    float ntc_r0_ohms;              /* Nominal resistance at 25 C (default: 10000.0) */
    float ntc_beta_k;               /* Beta coefficient (default: 3950.0) */
    float pullup_r_ohms;            /* Voltage divider pullup resistor (default: 10000.0) */
    float vref_mv;                  /* ADC reference voltage in mV (default: 3300.0) */
} batt_ntc_config_t;

typedef struct {
    float current_temp_c;
    batt_operational_state_t state;
    bool charge_enable_allowed;     /* Signal controlling hardware charge FET */
    bool rf_power_throttle_active;  /* Limits LoRa/GNSS peak transmission current */
    bool heater_enable_request;     /* Engage PTC/flex heater if external VBUS present */
    uint16_t v_idle_mv;
    uint16_t v_loaded_mv;
    uint16_t estimated_esr_mohm;
    uint32_t sag_events_count;
} batt_guard_status_t;

/**
 * @brief Initialize battery guard subsystem with NTC configuration
 */
void batt_guard_init(const batt_ntc_config_t *config);

/**
 * @brief Converts raw ADC reading of NTC voltage divider to Celsius
 * @param adc_raw Raw ADC code
 * @param adc_max Maximum ADC code (e.g. 4095 for 12-bit)
 * @return Temperature in Celsius
 */
float batt_guard_calc_temperature_c(uint16_t adc_raw, uint16_t adc_max);

/**
 * @brief Update battery monitor state machine with latest temperature and voltage readings
 * @param temp_c Temperature in degrees Celsius
 * @param v_batt_mv Current battery terminal voltage under nominal load
 * @param is_vbus_connected True if external charging source (solar / USB) is present
 * @param status Pointer to output status struct
 */
void batt_guard_update(
    float temp_c,
    uint16_t v_batt_mv,
    bool is_vbus_connected,
    batt_guard_status_t *status
);

/**
 * @brief Records loaded voltage dip during burst transmission to evaluate ESR and predict brownouts
 * @param v_idle_mv Battery voltage immediately prior to burst
 * @param v_burst_mv Battery voltage at peak of burst transmission
 * @param burst_current_ma Nominal burst current in mA
 * @param status Pointer to status struct
 * @return True if voltage sag is dangerous (nearing brownout)
 */
bool batt_guard_record_burst_sag(
    uint16_t v_idle_mv,
    uint16_t v_burst_mv,
    uint16_t burst_current_ma,
    batt_guard_status_t *status
);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_THERMAL_GUARD_H */

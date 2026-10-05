/**
 * @file trn_validator.h
 * @brief Zero-Heap Baro-TRN (Terrain-Referenced Navigation) Cross-Validator & Multipath Rejector.
 *
 * Physical Principle:
 *  - In narrow alpine canyons/gorges, GNSS signals reflect off vertical rock walls,
 *    causing pseudo-range elongation and sudden vertical spikes (~40m multipath jump).
 *  - BMP581 barometric pressure sensor has sub-meter relative accuracy (<0.05m noise)
 *    and is physically immune to RF multipath.
 *  - A 50-sample sliding window cross-evaluates GNSS vertical delta against Barometric delta
 *    and DEM (Digital Elevation Model) terrain gradient.
 *  - When MAD (Mean Absolute Deviation) exceeds 4.5m with speed > 0.3 m/s:
 *      * Flag GNSS_MULTIPATH_DETECTED = 1.
 *      * Zero-weight GNSS vertical innovation.
 *      * Snap altitude to DEM valley floor baseline integrated with barometric delta.
 */

#ifndef TRN_VALIDATOR_H
#define TRN_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRN_WINDOW_SIZE         (50U)   /**< 50-sample sliding window */
#define TRN_MAD_THRESHOLD_M     (4.5f)  /**< 4.5 meters MAD threshold */
#define TRN_MIN_SPEED_MMS       (300)   /**< 0.3 m/s minimum speed for dynamic TRN */
#define TRN_RECOVERY_SAMPLES    (20U)   /**< Consecutive clean samples to clear multipath flag */

/**
 * @brief TRN Cross-Validator State Structure (Zero Heap).
 */
typedef struct {
    float    residuals[TRN_WINDOW_SIZE]; /**< Circular window of vertical innovation residuals */
    uint8_t  window_idx;                 /**< Insertion index in circular window */
    uint8_t  window_count;               /**< Number of active samples in window */

    float    last_baro_alt_m;            /**< Previous step barometric altitude */
    float    last_gnss_alt_m;            /**< Previous step raw GNSS altitude */
    float    last_dem_alt_m;             /**< Previous step DEM terrain elevation */
    bool     has_last_sample;            /**< True if valid previous sample exists */

    float    current_mad_m;              /**< Latest computed Mean Absolute Deviation */
    bool     multipath_detected;         /**< Active GNSS Multipath Flag */
    uint32_t multipath_events;           /**< Cumulative count of multipath trigger events */

    float    valley_baseline_alt_m;      /**< Locked DEM valley baseline elevation */
    float    fused_altitude_m;           /**< Output filtered/fused altitude */
    uint16_t recovery_samples;           /**< Counter for consecutive clean samples */
} trn_validator_t;

/* -------------------------------------------------------------------------- */
/* Function Prototypes                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the TRN validator engine.
 *
 * @param trn Pointer to TRN state structure.
 * @param initial_dem_alt_m Initial known terrain elevation from DEM (e.g. gorge entrance).
 */
void trn_validator_init(trn_validator_t *trn, float initial_dem_alt_m);

/**
 * @brief Compute barometric altitude from BMP581 raw pressure and temperature.
 *
 * Uses standard hypsometric formula with sea level baseline P0 = 101325 Pa.
 *
 * @param press_pa_x100 24-bit raw pressure in 0.01 Pa (e.g. 10132500 for 1013.25 hPa).
 * @param temp_deci_c Temperature in 0.1 deg C (e.g. -150 for -15.0 C).
 * @return Geopotential altitude in meters.
 */
float trn_compute_baro_alt(uint32_t press_pa_x100, int16_t temp_deci_c);

/**
 * @brief Update TRN validator with a new synchronized navigation step.
 *
 * @param trn Pointer to TRN state structure.
 * @param gnss_alt_m Raw GNSS geometric altitude (meters).
 * @param baro_alt_m Barometric altitude (meters).
 * @param dem_alt_m DEM surface elevation at current (Lat, Lon) coordinates (meters).
 * @param speed_mms Ground speed in mm/s (from $GNRMC).
 * @param out_fused_alt_m Pointer to write output clean fused altitude.
 * @return true if multipath was actively detected and suppressed, false if GNSS was clean.
 */
bool trn_update_step(trn_validator_t *trn,
                    float gnss_alt_m,
                    float baro_alt_m,
                    float dem_alt_m,
                    int32_t speed_mms,
                    float *out_fused_alt_m);

/**
 * @brief Query whether GNSS multipath is actively flagged.
 */
bool trn_is_multipath_active(const trn_validator_t *trn);

/**
 * @brief Get the latest calculated Mean Absolute Deviation (MAD) in meters.
 */
float trn_get_mad(const trn_validator_t *trn);

#ifdef __cplusplus
}
#endif

#endif /* TRN_VALIDATOR_H */

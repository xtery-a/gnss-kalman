/**
 * @file trn_validator.c
 * @brief Zero-Heap Baro-TRN (Terrain-Referenced Navigation) Cross-Validator & Multipath Rejector.
 */

#include "trn_validator.h"
#include <string.h>
#include <math.h>

void trn_validator_init(trn_validator_t *trn, float initial_dem_alt_m) {
    if (!trn) return;
    memset(trn, 0, sizeof(trn_validator_t));

    trn->valley_baseline_alt_m = initial_dem_alt_m;
    trn->fused_altitude_m = initial_dem_alt_m;
    trn->last_dem_alt_m = initial_dem_alt_m;
    trn->has_last_sample = false;
    trn->multipath_detected = false;
    trn->multipath_events = 0;
    trn->recovery_samples = 0;
}

float trn_compute_baro_alt(uint32_t press_pa_x100, int16_t temp_deci_c) {
    (void)temp_deci_c; /* Standard ISA pressure-altitude baseline */
    if (press_pa_x100 == 0) return 0.0f;

    /* Pressure in Pascals */
    float p_pa = (float)press_pa_x100 / 100.0f;
    const float p0 = 101325.0f; /* Sea level standard pressure */

    if (p_pa <= 0.0f) return 0.0f;

    /* Hypsometric formula: h = 44330 * (1 - (p / p0)^0.190294957) */
    float pressure_ratio = p_pa / p0;
    float alt = 44330.0f * (1.0f - powf(pressure_ratio, 0.190294957f));

    return alt;
}

bool trn_update_step(trn_validator_t *trn,
                    float gnss_alt_m,
                    float baro_alt_m,
                    float dem_alt_m,
                    int32_t speed_mms,
                    float *out_fused_alt_m) {
    if (!trn || !out_fused_alt_m) return false;

    if (!trn->has_last_sample) {
        trn->last_baro_alt_m = baro_alt_m;
        trn->last_gnss_alt_m = gnss_alt_m;
        trn->last_dem_alt_m = dem_alt_m;
        trn->valley_baseline_alt_m = dem_alt_m;
        trn->fused_altitude_m = dem_alt_m;
        trn->has_last_sample = true;
        *out_fused_alt_m = dem_alt_m;
        return false;
    }

    /* Compute single-step differentials */
    float delta_baro = baro_alt_m - trn->last_baro_alt_m;
    float delta_gnss = gnss_alt_m - trn->last_gnss_alt_m;
    float delta_dem  = dem_alt_m - trn->last_dem_alt_m;
    (void)delta_dem;

    /* Instantaneous vertical innovation error between GNSS and immune Baro */
    float vertical_residual = fabsf(delta_gnss - delta_baro);

    /* Push into circular sliding window */
    trn->residuals[trn->window_idx] = vertical_residual;
    trn->window_idx = (uint8_t)((trn->window_idx + 1U) % TRN_WINDOW_SIZE);
    if (trn->window_count < TRN_WINDOW_SIZE) {
        trn->window_count++;
    }

    /* Calculate Mean Absolute Deviation (MAD) across window */
    float sum_res = 0.0f;
    for (uint8_t i = 0; i < trn->window_count; i++) {
        sum_res += trn->residuals[i];
    }
    float mean_res = sum_res / (float)trn->window_count;

    float sum_dev = 0.0f;
    for (uint8_t i = 0; i < trn->window_count; i++) {
        sum_dev += fabsf(trn->residuals[i] - mean_res);
    }
    trn->current_mad_m = sum_dev / (float)trn->window_count;

    /* Absolute deviation against DEM valley floor */
    float dem_discrepancy = fabsf(gnss_alt_m - dem_alt_m);

    /* Multipath Detection Logic:
     * High innovation residual or sustained MAD above 4.5m with user moving */
    bool exceeds_threshold = (vertical_residual > TRN_MAD_THRESHOLD_M) ||
                             (trn->current_mad_m > TRN_MAD_THRESHOLD_M) ||
                             (dem_discrepancy > 15.0f && vertical_residual > 2.0f);

    if (exceeds_threshold && (speed_mms >= TRN_MIN_SPEED_MMS)) {
        if (!trn->multipath_detected) {
            trn->multipath_events++;
        }
        trn->multipath_detected = true;
        trn->recovery_samples = 0;
    } else {
        if (trn->multipath_detected) {
            if (vertical_residual < 1.5f && dem_discrepancy < 6.0f) {
                trn->recovery_samples++;
                if (trn->recovery_samples >= TRN_RECOVERY_SAMPLES) {
                    trn->multipath_detected = false;
                }
            } else {
                trn->recovery_samples = 0;
            }
        }
    }

    if (trn->multipath_detected) {
        /* Zero-weight GNSS vertical innovation:
         * Snap altitude to DEM valley surface integrated with ultra-quiet barometric delta */
        trn->valley_baseline_alt_m = dem_alt_m;
        trn->fused_altitude_m = dem_alt_m + delta_baro * 0.5f;
    } else {
        /* Normal operation: Clean GNSS fused with barometric gradient */
        trn->valley_baseline_alt_m = dem_alt_m;
        float baro_projected = trn->fused_altitude_m + delta_baro;
        trn->fused_altitude_m = 0.90f * baro_projected + 0.10f * gnss_alt_m;

        /* Soft clamp to DEM corridor +/- 5m */
        if (fabsf(trn->fused_altitude_m - dem_alt_m) > 5.0f) {
            if (trn->fused_altitude_m > dem_alt_m + 5.0f) trn->fused_altitude_m = dem_alt_m + 5.0f;
            if (trn->fused_altitude_m < dem_alt_m - 5.0f) trn->fused_altitude_m = dem_alt_m - 5.0f;
        }
    }

    /* Update history */
    trn->last_baro_alt_m = baro_alt_m;
    trn->last_gnss_alt_m = gnss_alt_m;
    trn->last_dem_alt_m = dem_alt_m;

    *out_fused_alt_m = trn->fused_altitude_m;
    return trn->multipath_detected;
}

bool trn_is_multipath_active(const trn_validator_t *trn) {
    return trn ? trn->multipath_detected : false;
}

float trn_get_mad(const trn_validator_t *trn) {
    return trn ? trn->current_mad_m : 0.0f;
}

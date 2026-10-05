/**
 * @file cryo_sentinel.c
 * @brief High-Altitude Cryospheric Climate & Micro-Meteorology Engine (Zero-Heap C99).
 */

#include "cryo_sentinel.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD (float)(M_PI / 180.0)
#define RAD_TO_DEG (float)(180.0 / M_PI)
#define EARTH_RADIUS_M (6371000.0f)

void cryo_thermo_init(cryo_thermo_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(cryo_thermo_state_t));

    state->current_lapse_c_per_km = CRYO_STD_LAPSE_RATE_C_KM;
    state->regression_r2 = 0.0f;
    state->current_freezing_level_m = 0.0f;
    state->thermal_anomaly_m = 0.0f;
    state->lapse_valid = false;
    state->last_sample_ele_m = -9999.0f;
    state->accumulated_pdh = 0.0f;
    state->last_pdh_update_s = 0;
    state->rockfall_hazard_score = 0;
}

bool cryo_thermo_feed_sample(cryo_thermo_state_t *state,
                             float elevation_m,
                             float temp_c,
                             uint32_t timestamp_s,
                             float climatological_fl_m) {
    if (!state) return false;

    /* Check if elevation has changed enough to warrant a new atmospheric profile point */
    if (state->count > 0 && fabsf(elevation_m - state->last_sample_ele_m) < CRYO_SAMPLE_MIN_DZ_M) {
        /* Update current freezing level dynamically even between discrete buffer points */
        if (state->lapse_valid && state->current_lapse_c_per_km > 0.5f) {
            float lapse_per_m = state->current_lapse_c_per_km / 1000.0f;
            state->current_freezing_level_m = elevation_m + (temp_c / lapse_per_m);
            if (climatological_fl_m > 0.0f) {
                state->thermal_anomaly_m = state->current_freezing_level_m - climatological_fl_m;
            }
        }
        return state->lapse_valid;
    }

    /* Insert new observation into circular buffer */
    state->samples[state->head_idx].elevation_m = elevation_m;
    state->samples[state->head_idx].temp_c = temp_c;
    state->samples[state->head_idx].timestamp_s = timestamp_s;

    state->head_idx = (state->head_idx + 1U) % CRYO_LAPSE_BUFFER_SIZE;
    if (state->count < CRYO_LAPSE_BUFFER_SIZE) {
        state->count++;
    }
    state->last_sample_ele_m = elevation_m;

    /* Need at least 4 distinct samples for regression */
    if (state->count < 4U) {
        state->lapse_valid = false;
        return false;
    }

    /* Compute means */
    float sum_z = 0.0f;
    float sum_t = 0.0f;
    float min_z = 1e9f;
    float max_z = -1e9f;

    for (uint8_t i = 0; i < state->count; i++) {
        float z = state->samples[i].elevation_m;
        float t = state->samples[i].temp_c;
        sum_z += z;
        sum_t += t;
        if (z < min_z) min_z = z;
        if (z > max_z) max_z = z;
    }

    /* Check vertical span */
    float vert_span = max_z - min_z;
    if (vert_span < CRYO_MIN_VERT_SPAN_M) {
        state->lapse_valid = false;
        return false;
    }

    float mean_z = sum_z / (float)state->count;
    float mean_t = sum_t / (float)state->count;

    /* Compute sums of squares for linear regression */
    float s_zz = 0.0f;
    float s_tt = 0.0f;
    float s_zt = 0.0f;

    for (uint8_t i = 0; i < state->count; i++) {
        float dz = state->samples[i].elevation_m - mean_z;
        float dt = state->samples[i].temp_c - mean_t;
        s_zz += dz * dz;
        s_tt += dt * dt;
        s_zt += dz * dt;
    }

    if (s_zz < 1e-4f) {
        state->lapse_valid = false;
        return false;
    }

    /* Lapse rate Gamma = -dT/dz (in °C/m, converted to °C/km) */
    float slope_dt_dz = s_zt / s_zz;
    float gamma_c_km = -slope_dt_dz * 1000.0f;

    /* Compute R^2 */
    float r2 = 0.0f;
    if (s_tt > 1e-4f) {
        r2 = (s_zt * s_zt) / (s_zz * s_tt);
    }

    state->regression_r2 = r2;

    /* Physical bounds check: 
     * In nature, lapse rates typically fall between -3.0°C/km (strong inversion) 
     * and +12.0°C/km (superadiabatic near heated ground).
     */
    if (gamma_c_km >= -3.0f && gamma_c_km <= 14.0f && r2 >= 0.60f) {
        state->current_lapse_c_per_km = gamma_c_km;
        state->lapse_valid = true;
    } else {
        /* Low confidence or unphysical spike: keep prior estimate or default */
        state->lapse_valid = false;
    }

    /* Compute 0°C Isotherm (Freezing Level) */
    float active_gamma = (state->lapse_valid && state->current_lapse_c_per_km > 1.0f) 
                         ? state->current_lapse_c_per_km 
                         : CRYO_STD_LAPSE_RATE_C_KM;

    float lapse_per_m = active_gamma / 1000.0f;
    state->current_freezing_level_m = elevation_m + (temp_c / lapse_per_m);

    if (climatological_fl_m > 0.0f) {
        state->thermal_anomaly_m = state->current_freezing_level_m - climatological_fl_m;
    }

    return state->lapse_valid;
}

uint8_t cryo_update_permafrost_hazard(cryo_thermo_state_t *state,
                                      float current_temp_c,
                                      float current_ele_m,
                                      float terrain_slope_deg,
                                      uint32_t timestamp_s) {
    if (!state) return 0;

    /* Compute time delta for PDH integration */
    if (state->last_pdh_update_s > 0 && timestamp_s > state->last_pdh_update_s) {
        float dt_hours = (float)(timestamp_s - state->last_pdh_update_s) / 3600.0f;
        if (dt_hours > 0.0f && dt_hours < 24.0f) {
            if (current_temp_c > 0.0f) {
                state->accumulated_pdh += current_temp_c * dt_hours;
            } else {
                /* Refreezing relaxation: slow decay of accumulated thermal energy */
                state->accumulated_pdh *= expf(-0.05f * dt_hours);
                if (state->accumulated_pdh < 0.0f) state->accumulated_pdh = 0.0f;
            }
        }
    }
    state->last_pdh_update_s = timestamp_s;

    /* Base score from Thermal Forcing (Positive Degree Hours) */
    /* 0 to 48 PDH maps linearly to 0 - 50 points */
    float thermal_points = (state->accumulated_pdh / 48.0f) * 50.0f;
    if (thermal_points > 50.0f) thermal_points = 50.0f;

    /* Altitude relative to freezing level */
    float fl_points = 0.0f;
    if (state->current_freezing_level_m > 0.0f) {
        float elevation_deficit = state->current_freezing_level_m - current_ele_m;
        if (elevation_deficit > 0.0f) {
            /* We are below the 0°C line (in the active melting zone) */
            fl_points = (elevation_deficit / 800.0f) * 25.0f;
            if (fl_points > 25.0f) fl_points = 25.0f;
        }
    }

    /* Slope factor: rockfall requires gravitational shear stress */
    /* Slopes < 25° rarely produce major rockfall; 35°-55° are maximum hazard */
    float slope_multiplier = 0.0f;
    if (terrain_slope_deg >= 25.0f) {
        if (terrain_slope_deg <= 50.0f) {
            slope_multiplier = (terrain_slope_deg - 25.0f) / 25.0f; /* 0.0 to 1.0 */
        } else {
            slope_multiplier = 1.0f;
        }
    }

    float total_raw_score = (thermal_points + fl_points) * (0.3f + 0.7f * slope_multiplier);

    /* Direct temperature bonus if actively above +4°C in high permafrost zone */
    if (current_temp_c > 4.0f && current_ele_m > 2800.0f) {
        total_raw_score += 15.0f;
    }

    if (total_raw_score > 100.0f) total_raw_score = 100.0f;
    if (total_raw_score < 0.0f) total_raw_score = 0.0f;

    state->rockfall_hazard_score = (uint8_t)(total_raw_score + 0.5f);
    return state->rockfall_hazard_score;
}

bool cryo_compute_snout_recession(const glacier_baseline_t *baseline,
                                  int32_t current_lat_scaled,
                                  int32_t current_lon_scaled,
                                  float current_ele_m,
                                  uint16_t current_year,
                                  snout_recession_metrics_t *out_metrics) {
    if (!baseline || !out_metrics) return false;
    memset(out_metrics, 0, sizeof(snout_recession_metrics_t));

    if (current_year <= baseline->ref_year) {
        return false;
    }

    float dt_years = (float)(current_year - baseline->ref_year);

    /* Degrees conversion from 1e7 scaled format */
    float lat1_rad = ((float)baseline->ref_lat_scaled / 1e7f) * DEG_TO_RAD;
    float lon1_rad = ((float)baseline->ref_lon_scaled / 1e7f) * DEG_TO_RAD;
    float lat2_rad = ((float)current_lat_scaled / 1e7f) * DEG_TO_RAD;
    float lon2_rad = ((float)current_lon_scaled / 1e7f) * DEG_TO_RAD;

    /* Great circle distance (Haversine) */
    float dlat = lat2_rad - lat1_rad;
    float dlon = lon2_rad - lon1_rad;

    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(lat1_rad) * cosf(lat2_rad) *
              sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
    
    if (a > 1.0f) a = 1.0f;
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    float horizontal_dist_m = EARTH_RADIUS_M * c;

    /* Forward azimuth (bearing from baseline to current snout) */
    float y = sinf(dlon) * cosf(lat2_rad);
    float x = cosf(lat1_rad) * sinf(lat2_rad) -
              sinf(lat1_rad) * cosf(lat2_rad) * cosf(dlon);
    float bearing_rad = atan2f(y, x);
    float bearing_deg = bearing_rad * RAD_TO_DEG;
    if (bearing_deg < 0.0f) bearing_deg += 360.0f;

    /* Vertical lift: current snout is typically higher up the mountain than historical */
    float vertical_lift_m = current_ele_m - baseline->ref_snout_ele_m;

    out_metrics->horizontal_retreat_m = horizontal_dist_m;
    out_metrics->vertical_lift_m = vertical_lift_m;
    out_metrics->retreat_bearing_deg = bearing_deg;
    out_metrics->annual_h_retreat_m_yr = horizontal_dist_m / dt_years;
    out_metrics->annual_v_retreat_m_yr = vertical_lift_m / dt_years;

    /* In glaciology, ELA upward migration can be modeled from terminus vertical rise 
     * through the mass balance ablation gradient. For alpine valley glaciers,
     * delta_ELA ~ 0.55 to 0.70 * delta_Z_terminus (Braithwaite & Raper, 2002).
     */
    out_metrics->estimated_ela_shift_m = vertical_lift_m * 0.65f;

    /* Surface downwasting placeholder (will be filled if DEM baseline is available) */
    out_metrics->ice_downwasting_m = 0.0f;
    out_metrics->valid = true;

    return true;
}

float cryo_compute_surface_thinning(float current_ele_m,
                                    float historical_surface_dem_m,
                                    uint16_t baseline_year,
                                    uint16_t current_year,
                                    float *out_thinning_rate_m_yr) {
    float net_elevation_loss_m = historical_surface_dem_m - current_ele_m;

    if (out_thinning_rate_m_yr) {
        if (current_year > baseline_year) {
            float dt = (float)(current_year - baseline_year);
            *out_thinning_rate_m_yr = net_elevation_loss_m / dt;
        } else {
            *out_thinning_rate_m_yr = 0.0f;
        }
    }

    return net_elevation_loss_m;
}

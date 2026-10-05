#include "nlos_filter.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void nlos_filter_init(nlos_filter_context_t *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < NLOS_MAX_TRACKED_SATS; i++) {
        ctx->state_history[i] = SAT_STATE_UNINITIALIZED;
        ctx->svid_map[i] = 0;
        ctx->channel_active[i] = false;
    }
}

float nlos_interpolate_horizon_deg(const uint8_t *lut, float azimuth_deg) {
    if (!lut) return 0.0f;

    /* Normalize azimuth to [0.0, 360.0) */
    while (azimuth_deg < 0.0f) azimuth_deg += 360.0f;
    while (azimuth_deg >= 360.0f) azimuth_deg -= 360.0f;

    float raw_bin = azimuth_deg / NLOS_AZIMUTH_STEP_DEG;
    uint32_t bin_idx = (uint32_t)raw_bin;
    if (bin_idx >= NLOS_NUM_SKYMASK_BINS) bin_idx = 0;

    uint32_t next_bin_idx = (bin_idx + 1U) % NLOS_NUM_SKYMASK_BINS;
    float frac = raw_bin - (float)bin_idx;

    float h0 = (float)lut[bin_idx] * (90.0f / 255.0f);
    float h1 = (float)lut[next_bin_idx] * (90.0f / 255.0f);

    return (1.0f - frac) * h0 + frac * h1;
}

float nlos_fresnel_knife_edge_loss_db(float v) {
    /* ITU-R P.526 approximation for knife-edge diffraction loss */
    if (v <= -1.0f) {
        return 0.0f;
    } else if (v <= 0.0f) {
        float loss = 6.02f + 9.0f * v + 1.66f * v * v;
        return (loss > 0.0f) ? loss : 0.0f;
    } else if (v <= 1.0f) {
        return 6.02f + 9.11f * v - 1.27f * v * v;
    } else {
        return 12.95f + 20.0f * log10f(v);
    }
}

static int32_t get_or_allocate_channel(nlos_filter_context_t *ctx, uint8_t svid) {
    /* Find existing channel */
    for (uint32_t i = 0; i < NLOS_MAX_TRACKED_SATS; i++) {
        if (ctx->channel_active[i] && ctx->svid_map[i] == svid) {
            return (int32_t)i;
        }
    }
    /* Allocate new empty slot */
    for (uint32_t i = 0; i < NLOS_MAX_TRACKED_SATS; i++) {
        if (!ctx->channel_active[i]) {
            ctx->channel_active[i] = true;
            ctx->svid_map[i] = svid;
            ctx->state_history[i] = SAT_STATE_UNINITIALIZED;
            return (int32_t)i;
        }
    }
    /* Static pool full, fallback to slot 0 */
    return 0;
}

void nlos_filter_evaluate_satellite(
    nlos_filter_context_t *ctx,
    const uint8_t *lut,
    const nlos_sat_measurement_t *meas,
    nlos_sat_filter_output_t *out
) {
    if (!ctx || !lut || !meas || !out) return;

    int32_t ch = get_or_allocate_channel(ctx, meas->svid);
    nlos_satellite_state_t prev_state = ctx->state_history[ch];

    /* 1. Interpolate horizon angle at satellite azimuth */
    float horizon_deg = nlos_interpolate_horizon_deg(lut, meas->azimuth_deg);
    float margin_deg = meas->elevation_deg - horizon_deg;

    /* 2. Determine carrier wavelength */
    float lambda = (meas->band == GNSS_BAND_L5) ? NLOS_WAVELENGTH_L5_M : NLOS_WAVELENGTH_L1_M;
    float dist_ridge = (meas->estimated_ridge_dist_m > 100.0f) ? meas->estimated_ridge_dist_m : 2000.0f;

    /* 3. Compute Fresnel diffraction parameter v */
    /* v = -margin_rad * sqrt(2 * d1 / lambda) */
    float margin_rad = margin_deg * ((float)M_PI / 180.0f);
    float fresnel_geom_factor = sqrtf(2.0f * dist_ridge / lambda);
    float v = -margin_rad * fresnel_geom_factor;

    float diff_loss_db = nlos_fresnel_knife_edge_loss_db(v);

    /* 4. Apply +/- 1.5 deg angular hysteresis state machine */
    nlos_satellite_state_t new_state;

    if (prev_state == SAT_STATE_UNINITIALIZED) {
        if (margin_deg > NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_LOS;
        } else if (margin_deg < -NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_NLOS_BLOCKED;
        } else {
            new_state = SAT_STATE_DIFFRACTED;
        }
    } else if (prev_state == SAT_STATE_LOS) {
        if (margin_deg < -NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_NLOS_BLOCKED;
        } else if (margin_deg <= NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_DIFFRACTED;
        } else {
            new_state = SAT_STATE_LOS;
        }
    } else if (prev_state == SAT_STATE_NLOS_BLOCKED) {
        if (margin_deg > NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_LOS;
        } else if (margin_deg >= -NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_DIFFRACTED;
        } else {
            new_state = SAT_STATE_NLOS_BLOCKED;
        }
    } else { /* SAT_STATE_DIFFRACTED */
        if (margin_deg > NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_LOS;
        } else if (margin_deg < -NLOS_HYSTERESIS_HALF_DEG) {
            new_state = SAT_STATE_NLOS_BLOCKED;
        } else {
            new_state = SAT_STATE_DIFFRACTED;
        }
    }

    ctx->state_history[ch] = new_state;

    /* 5. Compute Kalman filter weight w in [0.0, 1.0] */
    float weight = 0.0f;
    if (new_state == SAT_STATE_LOS) {
        /* Sine elevation weighting for clear sky */
        float sin_el = sinf(meas->elevation_deg * ((float)M_PI / 180.0f));
        weight = (sin_el > 0.15f) ? 1.0f : (sin_el / 0.15f);
    } else if (new_state == SAT_STATE_NLOS_BLOCKED) {
        weight = 0.0f; /* Reject outright from position fix */
    } else { /* SAT_STATE_DIFFRACTED */
        /* Linear amplitude attenuation factor from diffraction */
        float amp_factor = powf(10.0f, -diff_loss_db / 20.0f);

        /* CN0 quality penalty */
        float cn0_factor = (meas->cn0_dbhz - 20.0f) / 25.0f;
        if (cn0_factor < 0.1f) cn0_factor = 0.1f;
        if (cn0_factor > 1.0f) cn0_factor = 1.0f;

        weight = amp_factor * cn0_factor;
        /* Clamp diffracted weight to avoid singular or overconfident matrices */
        if (weight < 0.05f) weight = 0.05f;
        if (weight > 0.85f) weight = 0.85f;
    }

    /* Populate output structure */
    out->svid = meas->svid;
    out->current_state = new_state;
    out->horizon_elev_deg = horizon_deg;
    out->angular_margin_deg = margin_deg;
    out->fresnel_v = v;
    out->diffraction_loss_db = diff_loss_db;
    out->kalman_weight = weight;
    out->is_valid = true;
}

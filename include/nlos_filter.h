#ifndef NLOS_FILTER_H
#define NLOS_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extrem-Condition GNSS NLOS Decision & Fresnel Diffraction Filtering Engine
 * 
 * Implements:
 * 1. 64-bin Topographic Skymask horizon interpolation
 * 2. Knife-edge Fresnel diffraction modeling (ITU-R P.526 approximation)
 * 3. +/- 1.5 degree angular hysteresis state machine to eliminate signal chattering
 * 4. Continuous Kalman filter weight calculation (0.0 to 1.0)
 * 5. Strictly zero-heap, deterministic compile-time memory structures
 */

#define NLOS_NUM_SKYMASK_BINS       64U
#define NLOS_AZIMUTH_STEP_DEG       5.625f
#define NLOS_HYSTERESIS_HALF_DEG    1.5f
#define NLOS_MAX_TRACKED_SATS       32U

/* Carrier wavelengths (meters) */
#define NLOS_WAVELENGTH_L1_M        0.19029f   /* 1575.42 MHz */
#define NLOS_WAVELENGTH_L5_M        0.25483f   /* 1176.45 MHz */

typedef enum {
    SAT_STATE_UNINITIALIZED = 0,
    SAT_STATE_LOS,           /* Clear line of sight: Elevation > (Horizon + 1.5 deg) */
    SAT_STATE_DIFFRACTED,    /* Fresnel diffraction zone: |Elevation - Horizon| <= 1.5 deg */
    SAT_STATE_NLOS_BLOCKED   /* Deep terrain shadow: Elevation < (Horizon - 1.5 deg) */
} nlos_satellite_state_t;

typedef enum {
    GNSS_BAND_L1 = 0,
    GNSS_BAND_L5 = 1
} gnss_carrier_band_t;

typedef struct {
    uint8_t svid;                   /* Space Vehicle ID (e.g., 1..32 for GPS) */
    gnss_carrier_band_t band;       /* L1 or L5 carrier */
    float azimuth_deg;              /* 0.0 to 360.0 clockwise from North */
    float elevation_deg;            /* 0.0 to 90.0 */
    float cn0_dbhz;                 /* Carrier-to-noise ratio in dB-Hz */
    float estimated_ridge_dist_m;   /* Distance to terrain obstacle (default ~2000m) */
} nlos_sat_measurement_t;

typedef struct {
    uint8_t svid;
    nlos_satellite_state_t current_state;
    float horizon_elev_deg;         /* Interpolated local terrain horizon angle */
    float angular_margin_deg;       /* elevation - horizon */
    float fresnel_v;                /* Dimensionless knife-edge diffraction parameter */
    float diffraction_loss_db;      /* Attenuation in dB */
    float kalman_weight;            /* Recommended measurement weight [0.0, 1.0] */
    bool is_valid;
} nlos_sat_filter_output_t;

typedef struct {
    nlos_satellite_state_t state_history[NLOS_MAX_TRACKED_SATS];
    uint8_t svid_map[NLOS_MAX_TRACKED_SATS];
    bool channel_active[NLOS_MAX_TRACKED_SATS];
} nlos_filter_context_t;

/**
 * @brief Initialize the zero-heap NLOS filter context
 */
void nlos_filter_init(nlos_filter_context_t *ctx);

/**
 * @brief Bilinearly interpolates horizon elevation angle from a 64-bin LUT for a given azimuth
 * @param lut 64-byte skymask lookup table (quantized 0..255 -> 0..90 deg)
 * @param azimuth_deg Azimuth in degrees [0.0, 360.0)
 * @return Horizon elevation angle in degrees [0.0, 90.0]
 */
float nlos_interpolate_horizon_deg(const uint8_t *lut, float azimuth_deg);

/**
 * @brief Computes knife-edge Fresnel diffraction loss J(v) in dB using ITU-R P.526 approximation
 * @param v Dimensionless Fresnel diffraction parameter
 * @return Attenuation in dB (>= 0.0)
 */
float nlos_fresnel_knife_edge_loss_db(float v);

/**
 * @brief Evaluates a single satellite measurement against local skymask with hysteresis and diffraction
 * @param ctx Pointer to static filter context
 * @param lut Pointer to active 64-byte skymask LUT
 * @param meas Input satellite measurement
 * @param out Output filter decision, diffraction loss, and Kalman weight
 */
void nlos_filter_evaluate_satellite(
    nlos_filter_context_t *ctx,
    const uint8_t *lut,
    const nlos_sat_measurement_t *meas,
    nlos_sat_filter_output_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* NLOS_FILTER_H */

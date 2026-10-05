/**
 * @file kalman_filter.h
 * @brief Zero-Heap ANSI C99 Kinematic Extended Kalman Filter (EKF) for GNSS & Baro-TRN Fusion.
 *
 * State Vector (6x1):
 *   x = [ pos_e, pos_n, pos_u, vel_e, vel_n, vel_u ]^T
 *   in Local Tangent Plane (ENU - East, North, Up) coordinates in meters and m/s.
 *
 * Features:
 *   - Zero dynamic heap allocation (static BSS buffers only).
 *   - Continuous constant-velocity (CV) kinematic prediction with discrete process noise Q(dt).
 *   - Adaptive GNSS measurement covariance R_gnss scaled by NLOS skymask weight w_skymask.
 *   - Barometric vertical innovation update with temperature lapse-rate compensation.
 *   - Joseph-form covariance update with symmetry enforcement: P = 0.5 * (P + P^T).
 *   - Chi-squared innovation gating (Mahalanobis distance) to reject sudden multipath spikes.
 */

#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EKF_STATE_DIM       (6U)  /* [pe, pn, pu, ve, vn, vu] */
#define EKF_MEAS_GNSS_DIM   (3U)  /* [ze, zn, zu] */

/**
 * @brief 6-State Kinematic Kalman Filter Context.
 */
typedef struct {
    /* State Estimate Vector (6x1) */
    float x[EKF_STATE_DIM];

    /* Error Covariance Matrix (6x6) */
    float P[EKF_STATE_DIM * EKF_STATE_DIM];

    /* Process Noise Spectral Densities */
    float q_pos;      /**< Position process noise spectral density (m^2/s) */
    float q_vel;      /**< Acceleration process noise spectral density (m^2/s^3) */

    /* Reference Geodetic Origin (Datum for ENU frame) */
    int32_t ref_lat_1e7;
    int32_t ref_lon_1e7;
    float   ref_alt_m;
    bool    has_origin;

    /* Filter Diagnostic Counters */
    uint32_t predict_count;
    uint32_t gnss_update_count;
    uint32_t baro_update_count;
    uint32_t rejected_outliers;
    float    last_innovation_norm;
} kalman_filter_t;

/**
 * @brief Initialize Kalman filter with initial origin and a priori uncertainty.
 */
void kalman_filter_init(kalman_filter_t *kf,
                        int32_t init_lat_1e7,
                        int32_t init_lon_1e7,
                        float init_alt_m,
                        float init_pos_std_m,
                        float init_vel_std_ms);

/**
 * @brief Time update / State & Covariance Prediction step.
 * @param dt_sec Elapsed time delta in seconds (e.g. 0.1 for 10 Hz GNSS).
 */
void kalman_filter_predict(kalman_filter_t *kf, float dt_sec);

/**
 * @brief Measurement update using 3D GNSS Position.
 * @param lat_1e7 Measured latitude in 1e7 degrees.
 * @param lon_1e7 Measured longitude in 1e7 degrees.
 * @param alt_m Measured ellipsoidal/geoid altitude in meters.
 * @param hdop Horizontal Dilution of Precision (e.g. 0.8 - 5.0).
 * @param nlos_weight Weight from topographic skymask [0.0 (blocked) to 1.0 (clear LOS)].
 * @return True if update was accepted, false if rejected as outlier.
 */
bool kalman_filter_update_gnss(kalman_filter_t *kf,
                               int32_t lat_1e7,
                               int32_t lon_1e7,
                               float alt_m,
                               float hdop,
                               float nlos_weight);

/**
 * @brief 1D Vertical Measurement update using Barometric Altimeter.
 * @param baro_alt_m Barometric altitude in meters.
 * @param baro_sigma_m Barometer measurement standard deviation (e.g. 0.3 m).
 * @return True if update was accepted.
 */
bool kalman_filter_update_baro(kalman_filter_t *kf, float baro_alt_m, float baro_sigma_m);

/**
 * @brief Convert estimated ENU state back to geodetic WGS-84 coordinates.
 */
void kalman_filter_get_geodetic(const kalman_filter_t *kf,
                                int32_t *out_lat_1e7,
                                int32_t *out_lon_1e7,
                                float *out_alt_m,
                                float *out_speed_ms,
                                float *out_course_deg);

/**
 * @brief Get position and velocity uncertainties (1-sigma standard deviations).
 */
void kalman_filter_get_uncertainties(const kalman_filter_t *kf,
                                     float *out_horiz_std_m,
                                     float *out_vert_std_m,
                                     float *out_vel_std_ms);

#ifdef __cplusplus
}
#endif

#endif /* KALMAN_FILTER_H */

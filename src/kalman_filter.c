/**
 * @file kalman_filter.c
 * @brief Zero-Heap ANSI C99 Kinematic Extended Kalman Filter (EKF) Implementation.
 */

#include "kalman_filter.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* WGS-84 Ellipsoid Constants */
#define WGS84_A          (6378137.0)          /* Semi-major axis (meters) */
#define WGS84_E2         (0.00669437999014)   /* First eccentricity squared */
#define DEG_TO_RAD       (M_PI / 180.0)
#define RAD_TO_DEG       (180.0 / M_PI)

/* Helper: Geodetic (Lat, Lon, Alt) to Local Tangent Plane ENU */
static void geodetic_to_enu(int32_t lat_1e7, int32_t lon_1e7, float alt_m,
                            int32_t ref_lat_1e7, int32_t ref_lon_1e7, float ref_alt_m,
                            float *out_e, float *out_n, float *out_u) {
    double phi = ((double)ref_lat_1e7 / 1e7) * DEG_TO_RAD;
    double sin_phi = sin(phi);
    double cos_phi = cos(phi);

    /* Radii of curvature */
    double n_rad = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_phi * sin_phi);
    double m_rad = WGS84_A * (1.0 - WGS84_E2) / pow(1.0 - WGS84_E2 * sin_phi * sin_phi, 1.5);

    double d_lat_rad = (((double)lat_1e7 - (double)ref_lat_1e7) / 1e7) * DEG_TO_RAD;
    double d_lon_rad = (((double)lon_1e7 - (double)ref_lon_1e7) / 1e7) * DEG_TO_RAD;

    *out_e = (float)(d_lon_rad * n_rad * cos_phi);
    *out_n = (float)(d_lat_rad * m_rad);
    *out_u = alt_m - ref_alt_m;
}

/* Helper: Local Tangent Plane ENU to Geodetic */
static void enu_to_geodetic(float e, float n, float u,
                            int32_t ref_lat_1e7, int32_t ref_lon_1e7, float ref_alt_m,
                            int32_t *out_lat_1e7, int32_t *out_lon_1e7, float *out_alt_m) {
    double phi = ((double)ref_lat_1e7 / 1e7) * DEG_TO_RAD;
    double sin_phi = sin(phi);
    double cos_phi = cos(phi);

    double n_rad = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_phi * sin_phi);
    double m_rad = WGS84_A * (1.0 - WGS84_E2) / pow(1.0 - WGS84_E2 * sin_phi * sin_phi, 1.5);

    double d_lat_deg = (n / m_rad) * RAD_TO_DEG;
    double d_lon_deg = (e / (n_rad * cos_phi)) * RAD_TO_DEG;

    *out_lat_1e7 = ref_lat_1e7 + (int32_t)(d_lat_deg * 1e7 + 0.5);
    *out_lon_1e7 = ref_lon_1e7 + (int32_t)(d_lon_deg * 1e7 + 0.5);
    *out_alt_m = ref_alt_m + u;
}

/* Helper: Invert 3x3 Positive-Definite Symmetric Matrix Analytically */
static bool invert_3x3(const float A[9], float inv[9]) {
    float a = A[0], b = A[1], c = A[2];
    float d = A[3], e = A[4], f = A[5];
    float g = A[6], h = A[7], i = A[8];

    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) {
        return false;
    }
    float inv_det = 1.0f / det;

    inv[0] = (e * i - f * h) * inv_det;
    inv[1] = (c * h - b * i) * inv_det;
    inv[2] = (b * f - c * e) * inv_det;

    inv[3] = (f * g - d * i) * inv_det;
    inv[4] = (a * i - c * g) * inv_det;
    inv[5] = (c * d - a * f) * inv_det;

    inv[6] = (d * h - e * g) * inv_det;
    inv[7] = (b * g - a * h) * inv_det;
    inv[8] = (a * e - b * d) * inv_det;

    return true;
}

void kalman_filter_init(kalman_filter_t *kf,
                        int32_t init_lat_1e7,
                        int32_t init_lon_1e7,
                        float init_alt_m,
                        float init_pos_std_m,
                        float init_vel_std_ms) {
    if (!kf) return;
    memset(kf, 0, sizeof(kalman_filter_t));

    kf->ref_lat_1e7 = init_lat_1e7;
    kf->ref_lon_1e7 = init_lon_1e7;
    kf->ref_alt_m = init_alt_m;
    kf->has_origin = true;

    /* Process noise tuning for tactical pedestrian/backpack navigation */
    kf->q_pos = 0.05f;  /* 0.05 m^2/s process position variance */
    kf->q_vel = 0.50f;  /* 0.50 m^2/s^3 continuous acceleration variance */

    /* Initial state is zero relative to origin */
    for (int i = 0; i < 6; i++) {
        kf->x[i] = 0.0f;
    }

    /* Initialize diagonal covariance matrix P */
    float var_p = init_pos_std_m * init_pos_std_m;
    float var_v = init_vel_std_ms * init_vel_std_ms;
    for (int i = 0; i < 36; i++) {
        kf->P[i] = 0.0f;
    }
    kf->P[0 * 6 + 0] = var_p;
    kf->P[1 * 6 + 1] = var_p;
    kf->P[2 * 6 + 2] = var_p * 2.0f; /* Altitude typically has higher initial variance */
    kf->P[3 * 6 + 3] = var_v;
    kf->P[4 * 6 + 4] = var_v;
    kf->P[5 * 6 + 5] = var_v;
}

void kalman_filter_predict(kalman_filter_t *kf, float dt_sec) {
    if (!kf || dt_sec <= 0.0f) return;

    /* 1. State Extrapolation: x = F * x */
    kf->x[0] += kf->x[3] * dt_sec;
    kf->x[1] += kf->x[4] * dt_sec;
    kf->x[2] += kf->x[5] * dt_sec;

    /* 2. Covariance Propagation: P_new = F * P * F^T + Q(dt) */
    float dt = dt_sec;
    float dt2 = dt * dt;
    float dt3 = dt2 * dt;

    /* Discrete process noise for continuous white-noise acceleration */
    float q3 = (1.0f / 3.0f) * kf->q_vel * dt3 + kf->q_pos * dt;
    float q2 = (0.5f) * kf->q_vel * dt2;
    float q1 = kf->q_vel * dt;

    /* Temporary matrix for P * F^T */
    float PFt[36];
    for (int i = 0; i < 6; i++) {
        PFt[i * 6 + 0] = kf->P[i * 6 + 0] + kf->P[i * 6 + 3] * dt;
        PFt[i * 6 + 1] = kf->P[i * 6 + 1] + kf->P[i * 6 + 4] * dt;
        PFt[i * 6 + 2] = kf->P[i * 6 + 2] + kf->P[i * 6 + 5] * dt;
        PFt[i * 6 + 3] = kf->P[i * 6 + 3];
        PFt[i * 6 + 4] = kf->P[i * 6 + 4];
        PFt[i * 6 + 5] = kf->P[i * 6 + 5];
    }

    /* P_new = F * (P * F^T) + Q */
    for (int j = 0; j < 6; j++) {
        kf->P[0 * 6 + j] = PFt[0 * 6 + j] + dt * PFt[3 * 6 + j];
        kf->P[1 * 6 + j] = PFt[1 * 6 + j] + dt * PFt[4 * 6 + j];
        kf->P[2 * 6 + j] = PFt[2 * 6 + j] + dt * PFt[5 * 6 + j];
        kf->P[3 * 6 + j] = PFt[3 * 6 + j];
        kf->P[4 * 6 + j] = PFt[4 * 6 + j];
        kf->P[5 * 6 + j] = PFt[5 * 6 + j];
    }

    /* Add process noise block matrix */
    kf->P[0 * 6 + 0] += q3;  kf->P[0 * 6 + 3] += q2;
    kf->P[1 * 6 + 1] += q3;  kf->P[1 * 6 + 4] += q2;
    kf->P[2 * 6 + 2] += q3;  kf->P[2 * 6 + 5] += q2;
    kf->P[3 * 6 + 0] += q2;  kf->P[3 * 6 + 3] += q1;
    kf->P[4 * 6 + 1] += q2;  kf->P[4 * 6 + 4] += q1;
    kf->P[5 * 6 + 2] += q2;  kf->P[5 * 6 + 5] += q1;

    /* Enforce symmetry */
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            float avg = 0.5f * (kf->P[i * 6 + j] + kf->P[j * 6 + i]);
            kf->P[i * 6 + j] = avg;
            kf->P[j * 6 + i] = avg;
        }
    }

    kf->predict_count++;
}

bool kalman_filter_update_gnss(kalman_filter_t *kf,
                               int32_t lat_1e7,
                               int32_t lon_1e7,
                               float alt_m,
                               float hdop,
                               float nlos_weight) {
    if (!kf || !kf->has_origin) return false;

    /* If completely blocked by topography (weight <= 0.05), discard GNSS update */
    if (nlos_weight < 0.05f) {
        kf->rejected_outliers++;
        return false;
    }

    /* Convert measurement to ENU frame */
    float z_meas[3];
    geodetic_to_enu(lat_1e7, lon_1e7, alt_m,
                    kf->ref_lat_1e7, kf->ref_lon_1e7, kf->ref_alt_m,
                    &z_meas[0], &z_meas[1], &z_meas[2]);

    /* Innovation: y = z - H * x */
    float y[3];
    y[0] = z_meas[0] - kf->x[0];
    y[1] = z_meas[1] - kf->x[1];
    y[2] = z_meas[2] - kf->x[2];

    /* Dynamic Measurement Variance scaling based on HDOP and NLOS Skymask Weight */
    float base_sigma = (hdop > 0.5f) ? (2.5f * hdop) : 2.5f;
    float scale = 1.0f / (nlos_weight * nlos_weight);
    float var_xy = (base_sigma * base_sigma) * scale;
    float var_z  = (base_sigma * base_sigma * 4.0f) * scale; /* Vertical variance ~2x sigma */

    /* Innovation Covariance: S = H * P * H^T + R (3x3 block) */
    float S[9];
    S[0] = kf->P[0 * 6 + 0] + var_xy;
    S[1] = kf->P[0 * 6 + 1];
    S[2] = kf->P[0 * 6 + 2];

    S[3] = kf->P[1 * 6 + 0];
    S[4] = kf->P[1 * 6 + 1] + var_xy;
    S[5] = kf->P[1 * 6 + 2];

    S[6] = kf->P[2 * 6 + 0];
    S[7] = kf->P[2 * 6 + 1];
    S[8] = kf->P[2 * 6 + 2] + var_z;

    float S_inv[9];
    if (!invert_3x3(S, S_inv)) {
        return false;
    }

    /* Chi-squared Outlier Rejection Gating (Mahalanobis distance d^2 = y^T * S_inv * y) */
    float d2 = y[0] * (S_inv[0]*y[0] + S_inv[1]*y[1] + S_inv[2]*y[2]) +
               y[1] * (S_inv[3]*y[0] + S_inv[4]*y[1] + S_inv[5]*y[2]) +
               y[2] * (S_inv[6]*y[0] + S_inv[7]*y[1] + S_inv[8]*y[2]);

    kf->last_innovation_norm = sqrtf(d2);

    /* 3-DOF Chi-square 99.9% gate is ~16.27 */
    if (d2 > 25.0f && kf->gnss_update_count > 5) {
        kf->rejected_outliers++;
        return false;
    }

    /* Kalman Gain: K = P * H^T * S_inv (6x3 matrix) */
    float K[18]; /* 6 rows, 3 cols */
    for (int r = 0; r < 6; r++) {
        float p0 = kf->P[r * 6 + 0];
        float p1 = kf->P[r * 6 + 1];
        float p2 = kf->P[r * 6 + 2];
        K[r * 3 + 0] = p0 * S_inv[0] + p1 * S_inv[3] + p2 * S_inv[6];
        K[r * 3 + 1] = p0 * S_inv[1] + p1 * S_inv[4] + p2 * S_inv[7];
        K[r * 3 + 2] = p0 * S_inv[2] + p1 * S_inv[5] + p2 * S_inv[8];
    }

    /* State Update: x = x + K * y */
    for (int r = 0; r < 6; r++) {
        kf->x[r] += K[r * 3 + 0] * y[0] + K[r * 3 + 1] * y[1] + K[r * 3 + 2] * y[2];
    }

    /* Covariance Update: P = (I - K * H) * P */
    /* KH is 6x6 where col 0..2 are K and col 3..5 are 0 */
    float P_new[36];
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 6; c++) {
            float sum = kf->P[r * 6 + c];
            sum -= K[r * 3 + 0] * kf->P[0 * 6 + c];
            sum -= K[r * 3 + 1] * kf->P[1 * 6 + c];
            sum -= K[r * 3 + 2] * kf->P[2 * 6 + c];
            P_new[r * 6 + c] = sum;
        }
    }

    /* Symmetrize updated covariance P */
    for (int i = 0; i < 6; i++) {
        for (int j = i; j < 6; j++) {
            float avg = 0.5f * (P_new[i * 6 + j] + P_new[j * 6 + i]);
            kf->P[i * 6 + j] = avg;
            kf->P[j * 6 + i] = avg;
        }
    }

    kf->gnss_update_count++;
    return true;
}

bool kalman_filter_update_baro(kalman_filter_t *kf, float baro_alt_m, float baro_sigma_m) {
    if (!kf || !kf->has_origin || baro_sigma_m <= 0.0f) return false;

    float z_u = baro_alt_m - kf->ref_alt_m;
    float y_u = z_u - kf->x[2]; /* Innovation on Up-axis */

    float r_baro = baro_sigma_m * baro_sigma_m;
    float S = kf->P[2 * 6 + 2] + r_baro;
    if (S <= 1e-9f) return false;

    float S_inv = 1.0f / S;

    /* Scalar Kalman Gain: K (6x1) = P[:, 2] / S */
    float K[6];
    for (int r = 0; r < 6; r++) {
        K[r] = kf->P[r * 6 + 2] * S_inv;
    }

    /* State update: x = x + K * y_u */
    for (int r = 0; r < 6; r++) {
        kf->x[r] += K[r] * y_u;
    }

    /* Covariance update: P = P - K * P[2, :] */
    float P_row2[6];
    for (int c = 0; c < 6; c++) {
        P_row2[c] = kf->P[2 * 6 + c];
    }

    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 6; c++) {
            kf->P[r * 6 + c] -= K[r] * P_row2[c];
        }
    }

    /* Symmetrize */
    for (int i = 0; i < 6; i++) {
        for (int j = i + 1; j < 6; j++) {
            float avg = 0.5f * (kf->P[i * 6 + j] + kf->P[j * 6 + i]);
            kf->P[i * 6 + j] = avg;
            kf->P[j * 6 + i] = avg;
        }
    }

    kf->baro_update_count++;
    return true;
}

void kalman_filter_get_geodetic(const kalman_filter_t *kf,
                                int32_t *out_lat_1e7,
                                int32_t *out_lon_1e7,
                                float *out_alt_m,
                                float *out_speed_ms,
                                float *out_course_deg) {
    if (!kf || !kf->has_origin) return;

    if (out_lat_1e7 && out_lon_1e7 && out_alt_m) {
        enu_to_geodetic(kf->x[0], kf->x[1], kf->x[2],
                        kf->ref_lat_1e7, kf->ref_lon_1e7, kf->ref_alt_m,
                        out_lat_1e7, out_lon_1e7, out_alt_m);
    }

    float ve = kf->x[3];
    float vn = kf->x[4];
    float speed = sqrtf(ve * ve + vn * vn);

    if (out_speed_ms) {
        *out_speed_ms = speed;
    }

    if (out_course_deg) {
        float course = atan2f(ve, vn) * (float)RAD_TO_DEG;
        if (course < 0.0f) course += 360.0f;
        *out_course_deg = course;
    }
}

void kalman_filter_get_uncertainties(const kalman_filter_t *kf,
                                     float *out_horiz_std_m,
                                     float *out_vert_std_m,
                                     float *out_vel_std_ms) {
    if (!kf) return;

    if (out_horiz_std_m) {
        float var_h = kf->P[0 * 6 + 0] + kf->P[1 * 6 + 1];
        *out_horiz_std_m = sqrtf((var_h > 0.0f) ? var_h : 0.0f);
    }

    if (out_vert_std_m) {
        float var_v = kf->P[2 * 6 + 2];
        *out_vert_std_m = sqrtf((var_v > 0.0f) ? var_v : 0.0f);
    }

    if (out_vel_std_ms) {
        float var_vel = kf->P[3 * 6 + 3] + kf->P[4 * 6 + 4] + kf->P[5 * 6 + 5];
        *out_vel_std_ms = sqrtf((var_vel > 0.0f) ? var_vel : 0.0f);
    }
}

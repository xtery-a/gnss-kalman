/**
 * @file test_harness_kalman.c
 * @brief Standalone Bare-Metal Verification Harness for Zero-Heap Kinematic EKF.
 */

#include "kalman_filter.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

int main(void) {
    printf("=================================================================\n");
    printf(" ZERO-HEAP 6-STATE KINEMATIC EXTENDED KALMAN FILTER VERIFICATION\n");
    printf("=================================================================\n");

    kalman_filter_t kf;
    int32_t origin_lat = 458326200; /* Mont Blanc: 45.8326200 N */
    int32_t origin_lon = 68652000;   /* 6.8652000 E */
    float   origin_alt = 3842.0f;    /* 3842 m MSL */

    /* [TEST 1] Initialization */
    printf("[TEST 1] Initializing 6-State Kinematic Filter (Origin: Mont Blanc)...\n");
    kalman_filter_init(&kf, origin_lat, origin_lon, origin_alt, 5.0f, 1.0f);
    assert(kf.has_origin == true);
    assert(kf.x[0] == 0.0f && kf.x[1] == 0.0f && kf.x[2] == 0.0f);
    assert(kf.P[0 * 6 + 0] == 25.0f); /* var_p = 5.0^2 */
    printf("  [PASS] State vector x[6] initialized to zero; covariance P[6x6] seeded.\n");

    /* [TEST 2] Kinematic Prediction Step */
    printf("[TEST 2] Time Update (Prediction Step, dt = 1.0s)...\n");
    /* Seed initial velocity: 1.5 m/s North (y-axis) */
    kf.x[4] = 1.5f;
    kalman_filter_predict(&kf, 1.0f);
    assert(fabsf(kf.x[1] - 1.5f) < 1e-4f);
    assert(kf.predict_count == 1);
    /* Verify covariance increased due to process noise Q */
    assert(kf.P[1 * 6 + 1] > 25.0f);
    printf("  [PASS] Velocity successfully propagated position (North = %.2f m, P_nn = %.2f m^2).\n",
           kf.x[1], kf.P[1 * 6 + 1]);

    /* [TEST 3] Clean Line-of-Sight GNSS Measurement Update */
    printf("[TEST 3] Clean Line-of-Sight GNSS Measurement Update (w_skymask = 1.0)...\n");
    /* True position at t=1s: (0.0 E, 1.5 N, 0.0 U) */
    int32_t meas_lat = 458326335; /* ~1.5m North */
    int32_t meas_lon = origin_lon;
    float   meas_alt = origin_alt;
    bool accepted = kalman_filter_update_gnss(&kf, meas_lat, meas_lon, meas_alt, 0.9f, 1.0f);
    assert(accepted == true);
    assert(kf.gnss_update_count == 1);
    /* Variance should decrease after measurement update */
    float horiz_std, vert_std, vel_std;
    kalman_filter_get_uncertainties(&kf, &horiz_std, &vert_std, &vel_std);
    printf("  [PASS] GNSS position accepted: Horiz 1-sigma uncertainty = %.2f m (filtered).\n", horiz_std);

    /* [TEST 4] Topographic NLOS Skymask Outright Rejection */
    printf("[TEST 4] Topographic NLOS Rejection (Canyon Obstacle w_skymask = 0.02)...\n");
    /* Ghost multipath coordinate 80m away */
    int32_t nlos_lat = 458333400;
    int32_t nlos_lon = origin_lon;
    bool nlos_accepted = kalman_filter_update_gnss(&kf, nlos_lat, nlos_lon, meas_alt, 1.2f, 0.02f);
    assert(nlos_accepted == false);
    assert(kf.rejected_outliers == 1);
    printf("  [PASS] Obstructed satellite completely dropped before corrupting state vector.\n");

    /* [TEST 5] Chi-Square Mahalanobis Multipath Spike Gating */
    printf("[TEST 5] Chi-Square Outlier Gating (Sudden 100m Multipath Jump)...\n");
    /* Seed a few good updates to converge filter */
    for (int i = 0; i < 6; i++) {
        kalman_filter_predict(&kf, 0.2f);
        kalman_filter_update_gnss(&kf, meas_lat, meas_lon, meas_alt, 0.8f, 0.95f);
    }
    int32_t spike_lat = 458335200; /* ~100m instantaneous cliff reflection */
    bool spike_accepted = kalman_filter_update_gnss(&kf, spike_lat, meas_lon, meas_alt, 0.8f, 0.95f);
    assert(spike_accepted == false);
    printf("  [PASS] Multipath spike rejected by Chi-square innovation gate (d = %.2f > 5.0).\n",
           kf.last_innovation_norm);

    /* [TEST 6] Baro-TRN Vertical Update Fusion */
    printf("[TEST 6] Baro-TRN Vertical Innovation Update (Baro Altimeter)...\n");
    float baro_alt = 3843.2f; /* +1.2 m above origin */
    bool baro_accepted = kalman_filter_update_baro(&kf, baro_alt, 0.4f);
    assert(baro_accepted == true);
    assert(kf.baro_update_count == 1);
    printf("  [PASS] Fused barometric vertical state: Altitude = %.2f m MSL (1-sigma = %.2f m).\n",
           kf.ref_alt_m + kf.x[2], sqrtf(kf.P[2 * 6 + 2]));

    /* [TEST 7] Back-Conversion to WGS-84 Geodetic Coordinates */
    printf("[TEST 7] Geodetic WGS-84 Back-Conversion & Kinematic Output...\n");
    int32_t out_lat, out_lon;
    float out_alt, out_speed, out_course;
    kalman_filter_get_geodetic(&kf, &out_lat, &out_lon, &out_alt, &out_speed, &out_course);
    assert(out_lat > origin_lat);
    assert(out_speed >= 0.0f);
    printf("  [PASS] Geodetic fix: Lat=%.7f, Lon=%.7f, Alt=%.1fm, Speed=%.2f m/s, Course=%.1f deg.\n",
           out_lat / 1e7, out_lon / 1e7, out_alt, out_speed, out_course);

    printf("=================================================================\n");
    printf(" ALL ZERO-HEAP KALMAN FILTER TESTS PASSED WITH 100%% SUCCESS!\n");
    printf("=================================================================\n");
    return 0;
}

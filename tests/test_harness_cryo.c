/**
 * @file test_harness_cryo.c
 * @brief High-Altitude Cryospheric Climate & Micro-Meteorology Unit Test Suite.
 */

#include "cryo_sentinel.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#define ANSI_GREEN "\033[32m"
#define ANSI_RED   "\033[31m"
#define ANSI_RESET "\033[0m"

static int g_pass_count = 0;
static int g_fail_count = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        g_pass_count++; \
    } else { \
        printf("  " ANSI_RED "[FAIL]" ANSI_RESET " %s (Line %d)\n", msg, __LINE__); \
        g_fail_count++; \
    } \
} while(0)

/* -------------------------------------------------------------------------- */
/* TEST 1: Glacier Snout Retreat Vector & ELA Migration (Mount Ararat / Mont Blanc) */
/* -------------------------------------------------------------------------- */
static void test_glacier_snout_retreat(void) {
    printf("\n=== TEST 1: GLACIER SNOUT RECESSION & ELA MIGRATION ===\n");

    /* Historical baseline for Mont Blanc - Mer de Glace snout (Year 1985 survey) */
    glacier_baseline_t mer_de_glace_1985 = {
        .glacier_name = "Mer de Glace (Mont Blanc)",
        .ref_year = 1985,
        .ref_lat_scaled = 459345000, /* 45.93450° N */
        .ref_lon_scaled = 69212000,  /* 6.92120° E */
        .ref_snout_ele_m = 1420.0f,  /* Snout terminus elevation in 1985 */
        .historical_ela_m = 2950.0f, /* ELA in 1985 */
        .climatological_fl_m = 3800.0f
    };

    /* Observed in-situ terminus waypoint during 2026 expedition */
    /* Snout has retreated up-valley by ~950m horizontally and climbed 230m in elevation */
    int32_t obs_lat_scaled = 459282000; /* 45.92820° N */
    int32_t obs_lon_scaled = 69288000;  /* 6.92880° E */
    float obs_ele_m = 1650.0f;
    uint16_t obs_year = 2026;

    snout_recession_metrics_t metrics;
    bool success = cryo_compute_snout_recession(&mer_de_glace_1985,
                                               obs_lat_scaled,
                                               obs_lon_scaled,
                                               obs_ele_m,
                                               obs_year,
                                               &metrics);

    TEST_ASSERT(success == true, "Snout recession calculation completed successfully");
    TEST_ASSERT(metrics.valid == true, "Metrics output marked valid");

    /* Distance check: Haversine distance between coordinates should be ~900-1100m */
    printf("    -> Horizontal Retreat dL: %.1f m\n", metrics.horizontal_retreat_m);
    TEST_ASSERT(metrics.horizontal_retreat_m >= 800.0f && metrics.horizontal_retreat_m <= 1200.0f,
                "Horizontal recession distance matches glaciological record (~950m)");

    /* Vertical lift check: 1650m - 1420m = 230m */
    printf("    -> Vertical Lift dZ: %.1f m\n", metrics.vertical_lift_m);
    TEST_ASSERT(fabsf(metrics.vertical_lift_m - 230.0f) < 1.0f, "Vertical terminus retreat is exactly 230m");

    /* Annual recession rate: 230m / 41 years = ~5.6 m/yr vertical, ~23 m/yr horizontal */
    printf("    -> Annual Horizontal Retreat Rate: %.2f m/year\n", metrics.annual_h_retreat_m_yr);
    printf("    -> Annual Vertical Lift Rate: %.2f m/year\n", metrics.annual_v_retreat_m_yr);
    TEST_ASSERT(metrics.annual_h_retreat_m_yr > 15.0f && metrics.annual_h_retreat_m_yr < 35.0f,
                "Annual horizontal retreat rate within realistic alpine bounds (15-35 m/yr)");

    /* ELA upward shift: 230m * 0.65 = 149.5m */
    printf("    -> Estimated ELA Upward Shift: +%.1f m\n", metrics.estimated_ela_shift_m);
    TEST_ASSERT(metrics.estimated_ela_shift_m > 120.0f && metrics.estimated_ela_shift_m < 180.0f,
                "Inferred ELA climb aligns with IPCC mountain cryosphere trends (+150m)");
}

/* -------------------------------------------------------------------------- */
/* TEST 2: In-Situ Adaptive Lapse Rate & Dynamic 0°C Freezing Level           */
/* -------------------------------------------------------------------------- */
static void test_lapse_rate_and_freezing_level(void) {
    printf("\n=== TEST 2: ADAPTIVE LAPSE RATE (Gamma) & 0°C FREEZING LEVEL ===\n");

    cryo_thermo_state_t thermo;
    cryo_thermo_init(&thermo);

    TEST_ASSERT(thermo.current_lapse_c_per_km == CRYO_STD_LAPSE_RATE_C_KM, "Initial lapse defaults to ISA 6.5°C/km");
    TEST_ASSERT(thermo.lapse_valid == false, "Initial lapse validity is false (empty buffer)");

    /* Simulate an ascent profile from Chamonix valley floor (1035m) to Aiguille du Midi (3842m)
     * Let actual lapse rate be 7.2°C / 1000m (moist-to-dry alpine transition)
     * Sea-level baseline temperature = +20°C
     * T(1035m) = 20 - (1.035 * 7.2) = +12.55°C
     */
    const float true_lapse = 7.2f; /* °C/km */
    float base_alt = 1000.0f;
    float base_temp = 12.8f;
    uint32_t t_stamp = 1700000000;

    /* Feed 20 altitude steps, every 100m gain */
    for (int i = 0; i < 20; i++) {
        float alt = base_alt + (float)i * 120.0f; /* 1000m up to 3280m */
        float dz_km = (alt - base_alt) / 1000.0f;
        float temp = base_temp - (dz_km * true_lapse);

        cryo_thermo_feed_sample(&thermo, alt, temp, t_stamp + (uint32_t)i * 600, 3600.0f);
    }

    printf("    -> Computed Lapse Rate Gamma: %.2f °C/km (True: %.2f °C/km)\n",
           thermo.current_lapse_c_per_km, true_lapse);
    printf("    -> Regression R^2 Confidence: %.4f\n", thermo.regression_r2);
    printf("    -> Computed 0°C Isotherm: %.1f m\n", thermo.current_freezing_level_m);

    TEST_ASSERT(thermo.lapse_valid == true, "Lapse rate marked valid after vertical ascent span");
    TEST_ASSERT(fabsf(thermo.current_lapse_c_per_km - true_lapse) < 0.2f, "Lapse rate error < 0.2°C/km");
    TEST_ASSERT(thermo.regression_r2 > 0.98f, "Regression R^2 > 0.98 indicates pristine linear fit");

    /* Theoretical 0°C line: base_alt + (base_temp / (true_lapse / 1000)) = 1000 + (12.8 / 0.0072) = 2777m */
    float expected_fl = base_alt + (base_temp / (true_lapse / 1000.0f));
    TEST_ASSERT(fabsf(thermo.current_freezing_level_m - expected_fl) < 15.0f,
                "Dynamic 0°C Isotherm level matches theoretical thermodynamic freezing line");
}

/* -------------------------------------------------------------------------- */
/* TEST 3: Elevation-Dependent Warming (EDW) Heatwave Anomaly & Permafrost Hazard */
/* -------------------------------------------------------------------------- */
static void test_edw_heatwave_and_permafrost_hazard(void) {
    printf("\n=== TEST 3: EDW HEATWAVE ANOMALY & PERMAFROST ROCKFALL HAZARD ===\n");

    cryo_thermo_state_t thermo;
    cryo_thermo_init(&thermo);

    /* Extreme heatwave scenario (similar to July 2022 Marmolada glacier collapse):
     * At 3,400m altitude (normal temp: -2°C to 0°C), temperature reaches anomalous +8.5°C!
     * Freezing level spikes above Mont Blanc summit to 4,850m.
     */
    float climb_alt = 3400.0f;
    float anomalous_temp = 8.5f;
    float climatological_summer_fl = 3800.0f; /* Normal freezing line is at 3800m */
    uint32_t t0 = 1700000000;

    /* Feed ascent under severe heat anomaly */
    for (int i = 0; i < 10; i++) {
        float alt = 2800.0f + (float)i * 60.0f;
        float temp = 12.5f - ((alt - 2800.0f) / 1000.0f) * 6.5f;
        cryo_thermo_feed_sample(&thermo, alt, temp, t0 + i * 300, climatological_summer_fl);
    }

    printf("    -> Heatwave 0°C Isotherm: %.1f m\n", thermo.current_freezing_level_m);
    printf("    -> Thermal Anomaly vs Climatology: +%.1f m\n", thermo.thermal_anomaly_m);

    TEST_ASSERT(thermo.thermal_anomaly_m > 700.0f, "Extreme Thermal Anomaly detected (> +700m above normal)");

    /* Simulate 36 hours of sustained positive temperatures at 3400m on a steep 45° couloir */
    uint8_t hazard = 0;
    for (uint32_t hour = 1; hour <= 36; hour++) {
        hazard = cryo_update_permafrost_hazard(&thermo, anomalous_temp, climb_alt, 45.0f, t0 + hour * 3600);
    }

    printf("    -> Accumulated Positive Degree Hours (PDH): %.1f °C*hr\n", thermo.accumulated_pdh);
    printf("    -> Permafrost Rockfall Hazard Score: %u / 100\n", hazard);

    TEST_ASSERT(thermo.accumulated_pdh > 250.0f, "PDH accumulated correctly over 36h thermal exposure");
    TEST_ASSERT(hazard >= 80, "Rockfall hazard escalates to CRITICAL (>= 80/100) on 45° slope with thawed ice glue");
}

int main(void) {
    printf("=================================================================\n");
    printf(" HIGH-ALTITUDE CRYOSPHERIC & CLIMATE SENTINEL VERIFICATION HARNESS\n");
    printf("=================================================================\n");

    test_glacier_snout_retreat();
    test_lapse_rate_and_freezing_level();
    test_edw_heatwave_and_permafrost_hazard();

    printf("\n=================================================================\n");
    printf(" TEST SUMMARY: %d PASSED | %d FAILED\n", g_pass_count, g_fail_count);
    printf("=================================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}

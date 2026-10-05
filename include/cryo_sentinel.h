/**
 * @file cryo_sentinel.h
 * @brief High-Altitude Cryospheric Climate & Micro-Meteorology Engine (Zero-Heap C99).
 *
 * Designed for extreme high-altitude alpine exploration and climate change in-situ observation.
 *
 * Physical Principles & Scientific Foundations:
 * 1. GLACIER SNOUT & EQUILIBRIUM LINE ALTITUDE (ELA) ENGINE:
 *    - Quantifies 3D glacier terminus (snout) recession against historical geospatial baselines
 *      (e.g., 1985/2000 Landsat/DEM moraine limits).
 *    - Computes horizontal retreat delta (dL), vertical elevation lift (dZ), retreat azimuth,
 *      annual recession rates (m/yr), and estimated ELA upward migration.
 *    - Validates ice surface downwasting (thinning) against reference bed topography.
 *
 * 2. ADAPTIVE ELEVATION LAPSE RATE (Gamma) & 0°C ISOTHERM (Z_FL) ENGINE:
 *    - Standard atmospheric models (ISA) assume constant lapse rate (6.5°C/km). In real alpine
 *      topography, Elevation-Dependent Warming (EDW) and local boundary layer effects cause
 *      severe variations between dry adiabatic (9.8°C/km) and moist/inversion regimes.
 *    - Computes running in-situ lapse rate Gamma = -dT/dz via linear regression over discrete
 *      altitude-temperature observation pairs during climber ascent.
 *    - Dynamically computes real-time Freezing Level / 0°C Isotherm: Z_0C = z + (T / Gamma).
 *    - Detects EDW thermal anomalies against climatological baselines (e.g. Z_0C > 4800m).
 *    - Integrates Positive Degree Hours (PDH) to evaluate Permafrost Thaw & Rockfall Hazard.
 */

#ifndef CRYO_SENTINEL_H
#define CRYO_SENTINEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYO_LAPSE_BUFFER_SIZE      (32U)   /**< Sliding window for altitude-temperature ascent samples */
#define CRYO_MIN_VERT_SPAN_M        (50.0f) /**< Minimum vertical delta required for valid regression */
#define CRYO_SAMPLE_MIN_DZ_M        (15.0f) /**< Minimum elevation gain to register a new lapse profile point */
#define CRYO_STD_LAPSE_RATE_C_KM    (6.5f)  /**< Standard International Standard Atmosphere (ISA) lapse rate */

/**
 * @brief Pre-calibrated Historical Glacier Baseline Record.
 */
typedef struct {
    const char *glacier_name;       /**< Official glacier identifier (e.g. "Mer de Glace", "Agri Takke") */
    uint16_t    ref_year;           /**< Baseline survey year (e.g. 1985, 2000) */
    int32_t     ref_lat_scaled;     /**< Latitude in 1e7 degrees (WGS84) */
    int32_t     ref_lon_scaled;     /**< Longitude in 1e7 degrees (WGS84) */
    float       ref_snout_ele_m;    /**< Terminus/snout elevation in meters at baseline year */
    float       historical_ela_m;   /**< Historical Equilibrium Line Altitude (ELA) in meters */
    float       climatological_fl_m;/**< Normal summer climatological freezing level (0°C isotherm) */
} glacier_baseline_t;

/**
 * @brief Output Metrics for Glacier Snout Recession and Mass Loss.
 */
typedef struct {
    float    horizontal_retreat_m;  /**< Horizontal recession distance (dL) in meters */
    float    vertical_lift_m;       /**< Vertical terminus retreat (dZ = Z_current - Z_ref) in meters */
    float    retreat_bearing_deg;   /**< Azimuth of the recession vector */
    float    annual_h_retreat_m_yr; /**< Horizontal retreat rate (dL / dt) in m/year */
    float    annual_v_retreat_m_yr; /**< Vertical lift rate (dZ / dt) in m/year */
    float    estimated_ela_shift_m; /**< Inferred upward migration of the Equilibrium Line Altitude */
    float    ice_downwasting_m;     /**< Local vertical ice thinning relative to historical surface */
    bool     valid;                 /**< True if valid observation was computed */
} snout_recession_metrics_t;

/**
 * @brief Discrete Point in Atmospheric Profile.
 */
typedef struct {
    float    elevation_m;           /**< Baro-GNSS fused elevation in meters */
    float    temp_c;                /**< Ambient temperature in degrees Celsius */
    uint32_t timestamp_s;           /**< Epoch or monotonic timestamp in seconds */
} lapse_sample_t;

/**
 * @brief Real-Time Lapse Rate & 0°C Isotherm Estimation State.
 */
typedef struct {
    lapse_sample_t samples[CRYO_LAPSE_BUFFER_SIZE];
    uint8_t        head_idx;
    uint8_t        count;

    float          last_sample_ele_m;
    float          current_lapse_c_per_km;  /**< Estimated in-situ lapse rate (-dT/dz * 1000) */
    float          regression_r2;           /**< Coefficient of determination (confidence metric: 0.0 to 1.0) */
    float          current_freezing_level_m;/**< Extrapolated 0°C Isotherm altitude */
    float          thermal_anomaly_m;       /**< Freezing level elevation difference vs climatological baseline */
    bool           lapse_valid;             /**< True if regression has sufficient span and correlation */

    /* Permafrost & Thermal Degradation Tracking */
    float          accumulated_pdh;         /**< Accumulated Positive Degree Hours (°C * hr) above 0°C */
    uint32_t       last_pdh_update_s;       /**< Timestamp of previous PDH calculation */
    uint8_t        rockfall_hazard_score;   /**< 0 (Low) to 100 (Extreme) based on PDH and slope angle */
} cryo_thermo_state_t;

/* -------------------------------------------------------------------------- */
/* Public Function Prototypes                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the Cryospheric Thermodynamic state machine.
 *
 * @param state Pointer to state structure.
 */
void cryo_thermo_init(cryo_thermo_state_t *state);

/**
 * @brief Ingest a new baro-altitude & temperature observation.
 *
 * Automatically updates sliding-window linear regression if minimum vertical
 * displacement threshold (CRYO_SAMPLE_MIN_DZ_M) is met.
 *
 * @param state Pointer to thermodynamic state.
 * @param elevation_m Current fused altitude in meters.
 * @param temp_c Current ambient temperature in degrees Celsius.
 * @param timestamp_s Current monotonic/epoch timestamp in seconds.
 * @param climatological_fl_m Normal seasonal freezing level for anomaly detection.
 * @return true if regression updated with high confidence (R² >= 0.70).
 */
bool cryo_thermo_feed_sample(cryo_thermo_state_t *state,
                             float elevation_m,
                             float temp_c,
                             uint32_t timestamp_s,
                             float climatological_fl_m);

/**
 * @brief Update Permafrost Thermal Degradation and Rockfall Hazard Index.
 *
 * @param state Pointer to thermodynamic state.
 * @param current_temp_c Current ambient temperature.
 * @param current_ele_m Current altitude in meters.
 * @param terrain_slope_deg Local slope angle from DEM/IMU (degrees).
 * @param timestamp_s Current timestamp in seconds.
 * @return Hazard score between 0 (safe/frozen) and 100 (critical thaw rockfall risk).
 */
uint8_t cryo_update_permafrost_hazard(cryo_thermo_state_t *state,
                                      float current_temp_c,
                                      float current_ele_m,
                                      float terrain_slope_deg,
                                      uint32_t timestamp_s);

/**
 * @brief Compute 3D Glacier Snout Recession and ELA metrics against a baseline.
 *
 * Uses Haversine distance and 3D vector projection to determine the exact
 * horizontal and vertical displacement of the glacier terminus.
 *
 * @param baseline Historical glacier baseline record.
 * @param current_lat_scaled Observed snout latitude in 1e7 degrees.
 * @param current_lon_scaled Observed snout longitude in 1e7 degrees.
 * @param current_ele_m Observed snout elevation in meters.
 * @param current_year Observation year (e.g. 2026).
 * @param out_metrics Pointer to output metrics structure.
 * @return true if computation succeeded.
 */
bool cryo_compute_snout_recession(const glacier_baseline_t *baseline,
                                  int32_t current_lat_scaled,
                                  int32_t current_lon_scaled,
                                  float current_ele_m,
                                  uint16_t current_year,
                                  snout_recession_metrics_t *out_metrics);

/**
 * @brief Estimate instantaneous ice surface downwasting (thinning) against DEM.
 *
 * @param current_ele_m Current observed elevation on glacier surface.
 * @param historical_surface_dem_m Historical DEM surface elevation at this coordinate.
 * @param baseline_year Historical DEM epoch year.
 * @param current_year Current epoch year.
 * @param out_thinning_rate_m_yr Pointer to output thinning rate (m/yr).
 * @return Net vertical elevation loss in meters (positive = ice loss).
 */
float cryo_compute_surface_thinning(float current_ele_m,
                                    float historical_surface_dem_m,
                                    uint16_t baseline_year,
                                    uint16_t current_year,
                                    float *out_thinning_rate_m_yr);

#ifdef __cplusplus
}
#endif

#endif /* CRYO_SENTINEL_H */

/**
 * @file squad_bft_mesh.h
 * @brief Zero-Heap Squad Blue Force Tracking (BFT), Peer-to-Peer LoRa Mesh &
 *        Automated Alpine Crevasse / Avalanche Fall Detection Engine.
 *
 * Designed for Extreme Condition Split-Node GNSS Terminal (EXT-GNSS-SPEC-001):
 *  - 16-Byte Ultra-Short LoRa SF7/SF8 Air Frames (< 45 ms on-air dwell).
 *  - Real-time squad relative polar navigation vectors (Distance & Azimuth).
 *  - Kinematic sensor fusion for automated crevasse fall & avalanche burial detection.
 *  - 400x240 Sharp MIP Tactical HUD integration with emergency banner overlay.
 *  - Strictly zero dynamic memory allocation (CONFIG_HEAP_MEM_POOL_SIZE = 0).
 */

#ifndef SQUAD_BFT_MESH_H
#define SQUAD_BFT_MESH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mip_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Squad Configuration Constants                                              */
/* -------------------------------------------------------------------------- */
#define SQUAD_MAX_MEMBERS           (8U)     /**< Maximum squad members tracked */
#define SQUAD_PACKET_MAGIC          (0x53U)  /**< 'S' for Squad Mesh Frame */
#define SQUAD_FRAME_SIZE            (16U)    /**< 16-byte packed radio beacon */

#define SQUAD_BEACON_INTERVAL_MS    (15000UL) /**< Normal broadcast rate (15 sec) */
#define SQUAD_EMERGENCY_INTERVAL_MS (2000UL)  /**< Emergency SOS broadcast rate (2 sec) */
#define SQUAD_STALE_TIMEOUT_MS      (60000UL) /**< 60s without beacon = Stale warning */
#define SQUAD_LOST_TIMEOUT_MS       (180000UL)/**< 180s without beacon = Missing alert */

/* -------------------------------------------------------------------------- */
/* Squad Member Status Flags (Bitmask)                                        */
/* -------------------------------------------------------------------------- */
#define SQUAD_STATUS_NORMAL         (0x00U)  /**< Nominal status */
#define SQUAD_STATUS_LOW_BATT       (0x01U)  /**< Battery < 15% */
#define SQUAD_STATUS_STATIONARY     (0x02U)  /**< No movement detected > 5 min */
#define SQUAD_STATUS_MANUAL_SOS     (0x04U)  /**< User manual 4-button chord SOS */
#define SQUAD_STATUS_CREVASSE_FALL  (0x08U)  /**< Auto-detected vertical fall into crevasse */
#define SQUAD_STATUS_AVALANCHE_BURIAL (0x10U)/**< Auto-detected avalanche burial */

#pragma pack(push, 1)

/**
 * @brief Packed 16-Byte On-Wire Squad LoRa Mesh Beacon
 */
typedef struct {
    uint8_t  magic;        /**< 0x53 ('S') */
    uint8_t  member_id;    /**< 1..8 (Callsign index e.g. T1..T8) */
    uint8_t  seq_num;      /**< Rolling sequence counter (0..255) */
    uint8_t  status_flags; /**< SQUAD_STATUS_* bitmask */
    int32_t  lat_1e7;      /**< Latitude in degrees * 1e7 */
    int32_t  lon_1e7;      /**< Longitude in degrees * 1e7 */
    int16_t  alt_m;        /**< Baro-TRN fused altitude in meters MSL */
    uint8_t  battery_pct;  /**< Remaining battery (0..100%) */
    uint8_t  crc8;         /**< CRC-8-CCITT integrity checksum */
} squad_beacon_packet_t;

#pragma pack(pop)

/**
 * @brief In-Memory Squad Member Tactical Record
 */
typedef struct {
    uint8_t  id;                    /**< Member ID (1..8) */
    char     callsign[4];           /**< "T1" through "T8" */
    int32_t  lat_1e7;               /**< Last reported latitude */
    int32_t  lon_1e7;               /**< Last reported longitude */
    int16_t  alt_m;                 /**< Last reported altitude */
    uint8_t  battery_pct;           /**< Last reported battery percentage */
    uint8_t  status_flags;          /**< Health / emergency flags */
    uint8_t  last_seq;              /**< Last sequence number */
    uint32_t last_seen_ms;          /**< Timestamp of last packet */
    int16_t  rel_dist_m;            /**< Computed relative distance from self in meters */
    int16_t  rel_bearing_deg;       /**< Computed azimuth from self (0..359 deg) */
    bool     active;                /**< True if valid member slot */
    bool     is_stale;              /**< True if time since last beacon > 60s */
    bool     is_lost;               /**< True if time since last beacon > 180s */
} squad_member_t;

/**
 * @brief Kinematic Fall & Crevasse Detection State Machine Context
 */
typedef enum {
    FALL_STATE_NORMAL = 0,          /**< Monitoring normal gait/hiking */
    FALL_STATE_FREEFALL,            /**< Free-fall acceleration detected (|a| < 0.35g) */
    FALL_STATE_IMPACT_WAIT,         /**< High-G deceleration impact confirmed */
    FALL_STATE_POST_IMMOBILITY,     /**< Evaluating post-fall motionlessness */
    FALL_STATE_CREVASSE_ALARM,      /**< Confirmed crevasse fall */
    FALL_STATE_AVALANCHE_ALARM      /**< Confirmed avalanche burial */
} fall_detector_state_t;

typedef struct {
    fall_detector_state_t state;
    uint32_t state_timer_ms;        /**< Duration spent in current kinematic state */
    float    pre_fall_alt_m;        /**< Barometric baseline altitude prior to event */
    float    fall_depth_m;          /**< Calculated total vertical descent */
    uint32_t immobility_timer_ms;   /**< Elapsed motionless duration */
    float    max_impact_g;          /**< Peak deceleration registered during shock */
} squad_fall_detector_t;

/* -------------------------------------------------------------------------- */
/* Squad Mesh Core API                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initializes the squad mesh subsystem and registers local terminal callsign.
 * @param my_id Local unit identifier (1..8)
 */
void squad_mesh_init(uint8_t my_id);

/**
 * @brief Serializes the local unit's state into a 16-byte packed radio beacon.
 * @param self Pointer to local unit data
 * @param out_buf Output buffer (minimum 16 bytes)
 * @param out_len Receives exact serialized length (always 16)
 * @return true on success, false on buffer error
 */
bool squad_mesh_encode_beacon(const squad_member_t *self, uint8_t *out_buf, size_t *out_len);

/**
 * @brief Ingests an incoming 16-byte LoRa mesh radio packet from a teammate.
 * Verifies CRC-8 checksum, updates squad tracking table, and flags status changes.
 * @param in_buf Received radio bytes
 * @param in_len Packet length (must be 16)
 * @param now_ms Current system uptime in milliseconds
 * @return true if valid teammate packet ingested, false if corrupted/invalid
 */
bool squad_mesh_decode_beacon(const uint8_t *in_buf, size_t in_len, uint32_t now_ms);

/**
 * @brief Recomputes relative distance and azimuth vectors for all squad members
 * relative to the current local terminal position.
 * @param my_lat_1e7 Local latitude
 * @param my_lon_1e7 Local longitude
 * @param my_alt_m Local altitude
 * @param now_ms Current system uptime in milliseconds
 */
void squad_mesh_update_relatives(int32_t my_lat_1e7, int32_t my_lon_1e7, int16_t my_alt_m, uint32_t now_ms);

/**
 * @brief Evaluates IMU accelerometer and barometric altimeter streams for
 * free-fall, high-G impact, rapid vertical drop and subsequent immobility.
 * Automatically triggers CREVASSE_FALL or AVALANCHE_BURIAL status flags.
 *
 * @param det Pointer to detector state machine
 * @param accel_norm_g Total 3-axis acceleration norm in g-force (|a| = sqrt(ax^2+ay^2+az^2))
 * @param baro_alt_m Current barometric altitude in meters
 * @param dt_ms Elapsed time step since last call in milliseconds
 * @param out_status Receives updated status bitmask flag
 */
void squad_mesh_process_kinematics(squad_fall_detector_t *det,
                                   float accel_norm_g,
                                   float baro_alt_m,
                                   uint32_t dt_ms,
                                   uint8_t *out_status);

/**
 * @brief Returns the member record for a given squad ID (1..8).
 */
const squad_member_t *squad_mesh_get_member(uint8_t member_id);

/**
 * @brief Returns total count of active squad members currently in table.
 */
size_t squad_mesh_get_active_count(void);

/**
 * @brief Returns the first squad member currently in an emergency status
 * (Crevasse, Avalanche, or SOS), or NULL if all members are nominal.
 */
const squad_member_t *squad_mesh_get_emergency_member(void);

/**
 * @brief Renders the tactical squad overlay onto the 400x240 MIP display framebuffer:
 *  - On-screen teammates rendered as tactical icons with callsign tags `[T2]`.
 *  - Off-screen teammates rendered as directional border arrows with range (e.g. `▶T3 280m`).
 *  - Emergency overlay banner if any member has triggered an alarm.
 *
 * @param center_lat_1e7 Viewport center latitude
 * @param center_lon_1e7 Viewport center longitude
 * @param scale_m_per_px Viewport scale in meters per pixel (e.g. 2.5 m/px for 500m LOD)
 */
void squad_mesh_render_overlay(int32_t center_lat_1e7, int32_t center_lon_1e7, float scale_m_per_px);

#ifdef __cplusplus
}
#endif

#endif /* SQUAD_BFT_MESH_H */

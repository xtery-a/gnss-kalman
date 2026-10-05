/**
 * @file gnss_nmea.h
 * @brief High-Speed Zero-Heap Circular DMA NMEA Tokenizer & Dual-Band (L1/L5) LC29H Parser.
 *
 * Target: Quectel LC29H (Dual-band L1/L5 GNSS receiver)
 * Physical Link: USART 115200 baud stream via Circular DMA.
 *
 * Features:
 *  - Zero-heap circular ring buffer DMA tokenizer with hardware wrap handling.
 *  - Incremental NMEA sentence checksum verification.
 *  - Dual-band $GNGSV parser populating 32-satellite tracking table (L1 & L5 C/N0).
 *  - Fixed-point $GNRMC parser (Latitude, Longitude in 1e7 deg, Speed in mm/s, Course in cdeg).
 *  - 64-bin Topographic Skymask NLOS rejection filter.
 */

#ifndef GNSS_NMEA_H
#define GNSS_NMEA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNSS_RING_BUF_SIZE      (1024U) /* Power of two for mask wrapping */
#define GNSS_MAX_SENTENCE_LEN   (128U)  /* Standard NMEA sentence <= 82, extra headroom */
#define GNSS_MAX_SATELLITES     (32U)   /* Concurrent satellite tracking table capacity */
#define GNSS_SKYMASK_BINS       (64U)   /* 64-bin azimuthal skymask LUT */

/**
 * @brief Satellite tracking status in local sky.
 */
typedef struct {
    uint8_t  prn;              /**< Satellite PRN / ID (1..32 for GPS, etc.) */
    uint8_t  elevation_deg;    /**< Elevation angle above horizon (0..90 deg) */
    uint16_t azimuth_deg;      /**< Azimuth angle from true North (0..359 deg) */
    uint8_t  cno_l1;           /**< L1 C/N0 carrier-to-noise ratio in dB-Hz (0..99) */
    uint8_t  cno_l5;           /**< L5 C/N0 carrier-to-noise ratio in dB-Hz (0..99) */
    bool     tracked_l1;       /**< True if L1 signal is actively tracked */
    bool     tracked_l5;       /**< True if L5 signal is actively tracked */
    bool     is_nlos;          /**< True if blocked by local topographic horizon */
    uint32_t last_update_ms;   /**< System tick timestamp of last observation */
} satellite_state_t;

/**
 * @brief Consolidated Position, Velocity & Time (PVT) Solution.
 */
typedef struct {
    bool     valid;            /**< Fix validity ('A' = true, 'V' = false) */
    int32_t  lat_1e7;          /**< Latitude in 1e7 degrees (positive = North, negative = South) */
    int32_t  lon_1e7;          /**< Longitude in 1e7 degrees (positive = East, negative = West) */
    int32_t  speed_mms;        /**< Ground speed in millimeters per second (mm/s) */
    uint16_t course_cd;        /**< Course over ground in centi-degrees (0..35999) */
    uint32_t utc_time_ms;      /**< UTC time of day in milliseconds */
    uint32_t utc_date;         /**< UTC date (DDMMYY) */
    int32_t  alt_geo_mm;       /**< Geometric GNSS altitude in millimeters */
    uint8_t  sats_in_view;     /**< Total satellites currently in tracking table */
    uint8_t  sats_l1_count;    /**< Satellites tracking L1 carrier */
    uint8_t  sats_l5_count;    /**< Satellites tracking L5 carrier */
    uint8_t  sats_clean_count; /**< Clean LOS satellites passing Skymask */
    uint8_t  sats_nlos_count;  /**< Blocked NLOS satellites behind mountain ridges */
} gnss_pvt_t;

/**
 * @brief Zero-Heap Circular DMA Tokenizer State.
 */
typedef struct {
    uint8_t  ring_buf[GNSS_RING_BUF_SIZE]; /**< DMA destination buffer */
    volatile uint32_t head;                /**< DMA write index */
    uint32_t tail;                         /**< Parser read index */
    char     line_buf[GNSS_MAX_SENTENCE_LEN]; /**< Line assembly buffer */
    uint8_t  line_pos;                     /**< Current write position in line_buf */
    bool     in_sentence;                  /**< True when '$' has been encountered */
    uint32_t sentences_parsed;             /**< Total valid NMEA sentences processed */
    uint32_t checksum_errors;              /**< Checksum mismatch counter */
    uint32_t dropped_chars;                /**< Buffer overflow dropped char counter */
} gnss_tokenizer_t;

/* -------------------------------------------------------------------------- */
/* Function Prototypes                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the circular DMA tokenizer.
 */
void gnss_tokenizer_init(gnss_tokenizer_t *tok);

/**
 * @brief Feed incoming raw bytes into the tokenizer (emulates DMA transfer).
 *
 * In bare metal STM32U5, DMA writes directly into `tok->ring_buf` and updates `tok->head`.
 */
void gnss_dma_feed(gnss_tokenizer_t *tok, const uint8_t *data, size_t len);

/**
 * @brief Poll the tokenizer for the next complete, checksum-verified NMEA sentence.
 *
 * @param tok Pointer to tokenizer.
 * @param out_sentence Destination buffer for null-terminated NMEA sentence.
 * @param max_len Size of out_sentence buffer.
 * @return true if a complete valid sentence was extracted, false otherwise.
 */
bool gnss_tokenizer_poll(gnss_tokenizer_t *tok, char *out_sentence, size_t max_len);

/**
 * @brief Parse a validated NMEA sentence ($GNRMC or $GNGSV) and update PVT / Satellite table.
 *
 * @param sentence Null-terminated NMEA string.
 * @param pvt Pointer to PVT state to update.
 * @param sats Array of 32 satellite slots.
 * @param max_sats Size of sats array (typically GNSS_MAX_SATELLITES).
 * @return true if sentence was recognized and parsed, false otherwise.
 */
bool gnss_parse_sentence(const char *sentence,
                        gnss_pvt_t *pvt,
                        satellite_state_t *sats,
                        uint8_t max_sats);

/**
 * @brief Apply a 64-bin Topographic Skymask LUT to filter NLOS satellites.
 *
 * Any satellite whose elevation is below the canyon/mountain ridge mask for its azimuth
 * has its `is_nlos` set to true.
 *
 * @param sats Array of satellites.
 * @param sat_count Number of satellites in array.
 * @param skymask_lut_64 64-byte array of horizon elevation thresholds (0..90 deg).
 * @param pvt Pointer to PVT state to update clean/nlos counts.
 * @return Number of clean (LOS) satellites available.
 */
uint8_t gnss_apply_skymask(satellite_state_t *sats,
                          uint8_t sat_count,
                          const uint8_t skymask_lut_64[GNSS_SKYMASK_BINS],
                          gnss_pvt_t *pvt);

#ifdef __cplusplus
}
#endif

#endif /* GNSS_NMEA_H */

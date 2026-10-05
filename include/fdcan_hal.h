/**
 * @file fdcan_hal.h
 * @brief STM32 Silicon FDCAN (Bosch M_CAN IP Core) Hardware Abstraction Layer.
 *
 * Implements ISO 11898-1:2015 Classical CAN & CAN-FD protocol stack:
 *  - Hardware Message RAM allocation (Tx Buffers, Rx FIFO0/FIFO1, Filter Lists).
 *  - 11-bit Standard & 29-bit Extended ID filtering.
 *  - Dual Bit-Timing Engine: Nominal Arbitration (500 kbps) & Data Bit Rate (2 / 5 Mbps).
 *  - Transceiver Delay Compensation (TDC).
 *  - Zero dynamic heap allocation (static BSS buffers only).
 */

#ifndef FDCAN_HAL_H
#define FDCAN_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard CAN-FD Payload Constants */
#define FDCAN_MAX_PAYLOAD_BYTES     (64U)
#define FDCAN_STD_FILTER_MAX        (16U)
#define FDCAN_EXT_FILTER_MAX        (8U)
#define FDCAN_TX_BUFFER_COUNT       (8U)
#define FDCAN_RX_FIFO_COUNT         (16U)

/* Identifier Types */
typedef enum {
    FDCAN_ID_STANDARD = 0x00U,  /**< 11-bit standard frame identifier */
    FDCAN_ID_EXTENDED = 0x01U   /**< 29-bit extended frame identifier */
} fdcan_id_type_t;

/* Frame Formats */
typedef enum {
    FDCAN_FRAME_CLASSIC = 0x00U, /**< Classic CAN 2.0B (<= 8 bytes, no BRS) */
    FDCAN_FRAME_FD_NO_BRS = 0x01U,/**< CAN-FD frame without Bit Rate Switching */
    FDCAN_FRAME_FD_BRS    = 0x02U /**< CAN-FD frame with Bit Rate Switching (up to 5 Mbps) */
} fdcan_frame_format_t;

/* Filter Actions */
typedef enum {
    FDCAN_FILTER_ACCEPT_FIFO0 = 0x00U,
    FDCAN_FILTER_ACCEPT_FIFO1 = 0x01U,
    FDCAN_FILTER_REJECT       = 0x02U
} fdcan_filter_action_t;

/**
 * @brief Bit Timing Configuration.
 */
typedef struct {
    /* Nominal (Arbitration Phase) Bit Timing: e.g. 500 kbps */
    uint16_t nominal_prescaler;   /**< 1..512 */
    uint16_t nominal_tseg1;       /**< 1..256 */
    uint8_t  nominal_tseg2;       /**< 1..128 */
    uint8_t  nominal_sjw;         /**< 1..128 */

    /* Data Phase Bit Timing: e.g. 2 Mbps / 5 Mbps */
    uint8_t  data_prescaler;      /**< 1..32 */
    uint8_t  data_tseg1;          /**< 1..32 */
    uint8_t  data_tseg2;          /**< 1..16 */
    uint8_t  data_sjw;            /**< 1..16 */
    bool     tdc_enable;          /**< Transceiver Delay Compensation */
} fdcan_bit_timing_t;

/**
 * @brief Transmit Message Header.
 */
typedef struct {
    uint32_t             identifier;      /**< 11-bit or 29-bit CAN ID */
    fdcan_id_type_t      id_type;         /**< Standard vs. Extended */
    fdcan_frame_format_t format;          /**< Classic vs. CAN-FD */
    uint8_t              data_length;     /**< Payload length in bytes (0..64) */
    uint8_t              message_marker;  /**< Message identifier for Tx Event FIFO */
} fdcan_tx_header_t;

/**
 * @brief Receive Message Header.
 */
typedef struct {
    uint32_t             identifier;      /**< 11-bit or 29-bit CAN ID */
    fdcan_id_type_t      id_type;         /**< Standard vs. Extended */
    fdcan_frame_format_t format;          /**< Classic vs. CAN-FD */
    uint8_t              data_length;     /**< Payload length in bytes (0..64) */
    uint16_t             timestamp_ticks; /**< Hardware 16-bit timer timestamp */
    uint8_t              filter_index;    /**< Matching filter index */
} fdcan_rx_header_t;

/**
 * @brief Standard 11-bit ID Filter Configuration.
 */
typedef struct {
    uint32_t              filter_id;       /**< Matching ID */
    uint32_t              filter_mask;     /**< Bitmask (0x7FF for exact match) */
    fdcan_filter_action_t action;          /**< Route to FIFO0, FIFO1, or reject */
} fdcan_std_filter_t;

/**
 * @brief FDCAN Protocol Controller Handle & Message RAM State.
 */
typedef struct {
    /* Peripheral Registers / Configuration */
    fdcan_bit_timing_t bit_timing;
    bool is_initialized;
    bool bus_off_active;

    /* Message RAM: Filter Lists */
    fdcan_std_filter_t std_filters[FDCAN_STD_FILTER_MAX];
    uint8_t            std_filter_count;

    /* Message RAM: Rx FIFO 0 */
    struct {
        fdcan_rx_header_t header;
        uint8_t           data[FDCAN_MAX_PAYLOAD_BYTES];
    } rx_fifo0[FDCAN_RX_FIFO_COUNT];
    uint8_t rx_fifo0_head;
    uint8_t rx_fifo0_tail;
    uint8_t rx_fifo0_count;

    /* Error Statistics */
    uint8_t  tec;            /**< Transmit Error Counter */
    uint8_t  rec;            /**< Receive Error Counter */
    uint32_t tx_frame_count;
    uint32_t rx_frame_count;
    uint32_t crc_error_count;
    uint32_t fifo_overflow_count;
} fdcan_handle_t;

/**
 * @brief Initialize FDCAN controller and configure dual nominal/data bit timing.
 */
bool fdcan_hal_init(fdcan_handle_t *h, const fdcan_bit_timing_t *timing);

/**
 * @brief Add standard 11-bit hardware acceptance filter.
 */
bool fdcan_hal_add_std_filter(fdcan_handle_t *h, const fdcan_std_filter_t *flt);

/**
 * @brief Send frame via CAN-FD Message RAM Transmit Buffer.
 */
bool fdcan_hal_transmit(fdcan_handle_t *h,
                        const fdcan_tx_header_t *hdr,
                        const uint8_t *payload);

/**
 * @brief Read frame from Hardware Rx FIFO 0.
 */
bool fdcan_hal_read_fifo0(fdcan_handle_t *h,
                          fdcan_rx_header_t *out_hdr,
                          uint8_t *out_payload);

/**
 * @brief Inject incoming physical CAN-FD bus frame into controller (HIL / transceiver hook).
 */
bool fdcan_hal_inject_rx_frame(fdcan_handle_t *h,
                               const fdcan_rx_header_t *hdr,
                               const uint8_t *payload);

/**
 * @brief Convert byte length (0..64) to CAN-FD Data Length Code (DLC 0..15).
 */
uint8_t fdcan_bytes_to_dlc(uint8_t len);

/**
 * @brief Convert DLC code (0..15) to byte count (0..64).
 */
uint8_t fdcan_dlc_to_bytes(uint8_t dlc);

#ifdef __cplusplus
}
#endif

#endif /* FDCAN_HAL_H */

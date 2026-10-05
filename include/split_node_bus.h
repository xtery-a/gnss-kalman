/**
 * @file split_node_bus.h
 * @brief Zero-Heap Deterministic CAN-FD Protocol Stack for Split-Node Architecture.
 *
 * Physical Link:
 *  - Chest HMI Pod (STM32G0B1) <-> 1m PUR Cable <-> Backpack Core Node (STM32U5)
 *  - ISO 11898-2 CAN-FD: 500 kbps Arbitration, 2 Mbps Data, 64-byte payload.
 *
 * 64-Byte CAN-FD Frame Format:
 *  [SOF: 0xAA55 (2B)] | [MsgID (1B)] | [SeqNum (1B)] | [Len (1B)] | [Payload (0..57B)] | [CRC-16-CCITT (2B)]
 */

#ifndef SPLIT_NODE_BUS_H
#define SPLIT_NODE_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Protocol Constants                                                         */
/* -------------------------------------------------------------------------- */
#define SPLIT_BUS_SOF0              (0xAAU)
#define SPLIT_BUS_SOF1              (0x55U)
#define SPLIT_BUS_SOF_WORD          (0xAA55U)

#define SPLIT_BUS_HEADER_SIZE       (5U)  /* SOF0(1) + SOF1(1) + MsgID(1) + Seq(1) + Len(1) */
#define SPLIT_BUS_CRC_SIZE          (2U)
#define SPLIT_BUS_MAX_PAYLOAD       (57U)
#define SPLIT_BUS_CANFD_FRAME_SIZE  (SPLIT_BUS_HEADER_SIZE + SPLIT_BUS_MAX_PAYLOAD + SPLIT_BUS_CRC_SIZE) /* 64B */

#define SPLIT_BUS_TIMEOUT_MS        (150U) /* 150 ms ACK timeout before controller soft reset */
#define SPLIT_BUS_RESYNC_TIME_MS    (50U)  /* Target resynchronization window */

/* Message Identifiers */
#define SPLIT_MSG_CHORD_EVENT       (0x10U) /* HMI -> CORE: 4-button chord event */
#define SPLIT_MSG_BARO_SAMPLE       (0x20U) /* HMI -> CORE: BMP581 raw pressure & temperature */
#define SPLIT_MSG_GNSS_TELEMETRY    (0x30U) /* CORE -> HMI: PVT & skymask satellite stats */
#define SPLIT_MSG_DIRTY_LINE        (0x40U) /* CORE -> HMI: Framebuffer dirty scanline */
#define SPLIT_MSG_HEARTBEAT         (0x50U) /* BIDIRECTIONAL: Uptime, supply voltage & alerts */

/* -------------------------------------------------------------------------- */
/* Typed Message Payloads                                                     */
/* -------------------------------------------------------------------------- */

#pragma pack(push, 1)

/**
 * @brief MsgID 0x10: Chord Input Event (5 Bytes)
 */
typedef struct {
    uint8_t  chord_mask;   /**< Active 4-bit switch mask (0x01..0x0F) */
    uint32_t duration_ms;  /**< Press duration in milliseconds */
} split_msg_chord_t;

/**
 * @brief MsgID 0x20: BMP581 Barometric & Temperature Sample (7 Bytes)
 */
typedef struct {
    uint32_t raw_press_24; /**< 24-bit raw pressure (0.01 Pa resolution, lower 24 bits) */
    int16_t  raw_temp_16;  /**< 16-bit temperature (0.01 deg C resolution, e.g. -1500 = -15.00 C) */
    uint8_t  sensor_status;/**< ODR / FIFO / Data ready flags */
} split_msg_baro_t;

/**
 * @brief MsgID 0x30: GNSS & Navigation Telemetry (26 Bytes)
 */
typedef struct {
    uint8_t  fix_status;   /**< 0=NoFix, 1=2D, 2=3D, 3=DGNSS, 4=RTK-Float, 5=RTK-Fixed */
    int32_t  lat_1e7;      /**< Latitude in 1e7 degrees */
    int32_t  lon_1e7;      /**< Longitude in 1e7 degrees */
    int32_t  alt_mm;       /**< Altitude in millimeters */
    uint16_t heading_cd;   /**< Heading in centi-degrees (0..35999) */
    uint16_t hdop_1e2;     /**< HDOP * 100 */
    uint8_t  sats_clean;   /**< Clean direct line-of-sight satellites count */
    uint8_t  sats_blocked; /**< Blocked / severely diffracted satellites count */
    uint32_t tow_ms;       /**< GPS Time-of-Week in milliseconds */
} split_msg_gnss_t;

/**
 * @brief MsgID 0x40: Framebuffer Dirty Line Block (51 Bytes)
 */
typedef struct {
    uint8_t line_index;       /**< Scanline index (0..239) */
    uint8_t pixel_data[50];   /**< 50 bytes of 1-bit monochrome pixel data */
} split_msg_dirty_line_t;

/**
 * @brief MsgID 0x50: Heartbeat & Bus Health (9 Bytes)
 */
typedef struct {
    uint32_t uptime_ms;    /**< Node uptime in milliseconds */
    uint16_t vbus_mv;      /**< Measured supply voltage in millivolts */
    int16_t  temp_deci_c;  /**< Internal temperature in 0.1 deg C */
    uint8_t  alert_flags;  /**< Bit 0: Sub-Zero Cold, Bit 1: Voltage Sag, Bit 2: Retries High */
} split_msg_heartbeat_t;

/**
 * @brief Generic Decoded CAN-FD Frame
 */
typedef struct {
    uint8_t  msg_id;
    uint8_t  seq_num;
    uint8_t  payload_len;
    uint8_t  payload[SPLIT_BUS_MAX_PAYLOAD];
    uint16_t crc16;
} split_bus_frame_t;

#pragma pack(pop)

/* -------------------------------------------------------------------------- */
/* Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer                */
/* -------------------------------------------------------------------------- */
#define SPLIT_BUS_QUEUE_CAPACITY    (16U) /* Power of 2 */

typedef struct {
    split_bus_frame_t buffer[SPLIT_BUS_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t dropped_count;
} split_bus_spsc_t;

void split_bus_spsc_init(split_bus_spsc_t *q);
bool split_bus_spsc_push(split_bus_spsc_t *q, const split_bus_frame_t *frame);
bool split_bus_spsc_pop(split_bus_spsc_t *q, split_bus_frame_t *out_frame);
uint32_t split_bus_spsc_count(const split_bus_spsc_t *q);

/* -------------------------------------------------------------------------- */
/* Bus Fault Handling & Receiver State Machine                                */
/* -------------------------------------------------------------------------- */
typedef enum {
    BUS_STATE_SEARCH_SOF0 = 0,
    BUS_STATE_SEARCH_SOF1,
    BUS_STATE_READ_MSGID,
    BUS_STATE_READ_SEQ,
    BUS_STATE_READ_LEN,
    BUS_STATE_READ_PAYLOAD,
    BUS_STATE_READ_CRC_HI,
    BUS_STATE_READ_CRC_LO
} split_bus_rx_state_t;

typedef struct {
    uint32_t rx_valid_frames;
    uint32_t rx_crc_errors;
    uint32_t rx_len_errors;
    uint32_t rx_sof_resyncs;
    uint32_t timeout_resets;
    uint32_t seq_drops;
} split_bus_stats_t;

typedef struct {
    split_bus_rx_state_t state;
    split_bus_frame_t    rx_frame;
    uint8_t              payload_idx;
    uint8_t              last_seq;
    bool                 has_last_seq;

    /* Timeout & Bus-Off Watchdog */
    uint32_t             last_rx_timestamp_ms;
    bool                 bus_off_triggered;

    split_bus_stats_t    stats;
} split_bus_receiver_t;

/* -------------------------------------------------------------------------- */
/* Public API Functions                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Computes standard CRC-16-CCITT (Poly: 0x1021, Init: 0xFFFF).
 */
uint16_t split_bus_crc16(const uint8_t *data, size_t len);

/**
 * @brief Updates a running CRC-16-CCITT calculation.
 */
uint16_t split_bus_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/**
 * @brief Serializes a packet into a 64-byte CAN-FD frame buffer.
 *
 * @param msg_id Target message identifier (0x10, 0x20, 0x30, 0x40, 0x50)
 * @param seq_num Sequence number
 * @param payload Binary payload
 * @param payload_len Payload length (0..57 bytes)
 * @param out_frame Buffer of at least 64 bytes
 * @return Total bytes encoded (always <= 64), or 0 on error
 */
uint8_t split_bus_pack_frame(uint8_t msg_id,
                            uint8_t seq_num,
                            const void *payload,
                            uint8_t payload_len,
                            uint8_t *out_frame);

/**
 * @brief Initializes the streaming receiver state machine.
 */
void split_bus_receiver_init(split_bus_receiver_t *rx);

/**
 * @brief Consumes incoming byte stream, recovers frame synchronization,
 *        and emits valid frames with bit-exact CRC validation.
 *
 * @param rx Receiver context
 * @param byte Incoming byte from CAN-FD controller or UART
 * @param now_ms Current system millisecond timestamp (for timeout tracking)
 * @param out_frame Output struct populated when a complete frame is validated
 * @return true if a complete, valid frame was assembled; false otherwise
 */
bool split_bus_receiver_feed(split_bus_receiver_t *rx,
                            uint8_t byte,
                            uint32_t now_ms,
                            split_bus_frame_t *out_frame);

/**
 * @brief Evaluates whether bus has timed out (> 150 ms without valid traffic).
 * Triggers soft controller reset and re-synchronization state.
 */
bool split_bus_check_timeout(split_bus_receiver_t *rx, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SPLIT_NODE_BUS_H */

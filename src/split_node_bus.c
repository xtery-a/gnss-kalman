/**
 * @file split_node_bus.c
 * @brief Zero-Heap Deterministic CAN-FD Protocol Stack for Split-Node Architecture.
 */

#include "split_node_bus.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* CRC-16-CCITT (Poly: 0x1021, Init: 0xFFFF) Table-Driven Implementation     */
/* -------------------------------------------------------------------------- */
static const uint16_t CRC16_TABLE[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
    0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
    0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
    0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
    0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
    0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
    0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
    0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
    0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
    0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
    0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
    0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
    0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
    0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
    0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
    0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
    0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
    0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
    0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
    0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
    0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
    0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
    0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
    0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBAU, 0xBB9BU,
    0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
    0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
    0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
    0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
    0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U
};

uint16_t split_bus_crc16_update(uint16_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t table_idx = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (uint16_t)((crc << 8) ^ CRC16_TABLE[table_idx]);
    }
    return crc;
}

uint16_t split_bus_crc16(const uint8_t *data, size_t len) {
    return split_bus_crc16_update(0xFFFFU, data, len);
}

/* -------------------------------------------------------------------------- */
/* Lock-Free SPSC Ring Buffer Implementation                                  */
/* -------------------------------------------------------------------------- */
void split_bus_spsc_init(split_bus_spsc_t *q) {
    if (!q) return;
    q->head = 0;
    q->tail = 0;
    q->dropped_count = 0;
}

bool split_bus_spsc_push(split_bus_spsc_t *q, const split_bus_frame_t *frame) {
    if (!q || !frame) return false;

    uint32_t current_head = q->head;
    uint32_t next_head = (current_head + 1U) & (SPLIT_BUS_QUEUE_CAPACITY - 1U);

    if (next_head == q->tail) {
        /* Queue full: drop packet deterministically */
        q->dropped_count++;
        return false;
    }

    q->buffer[current_head] = *frame;
    q->head = next_head;
    return true;
}

bool split_bus_spsc_pop(split_bus_spsc_t *q, split_bus_frame_t *out_frame) {
    if (!q || !out_frame) return false;

    uint32_t current_tail = q->tail;
    if (q->head == current_tail) {
        return false; /* Queue empty */
    }

    *out_frame = q->buffer[current_tail];
    q->tail = (current_tail + 1U) & (SPLIT_BUS_QUEUE_CAPACITY - 1U);
    return true;
}

uint32_t split_bus_spsc_count(const split_bus_spsc_t *q) {
    if (!q) return 0;
    return (q->head - q->tail) & (SPLIT_BUS_QUEUE_CAPACITY - 1U);
}

/* -------------------------------------------------------------------------- */
/* Frame Packaging API                                                        */
/* -------------------------------------------------------------------------- */
uint8_t split_bus_pack_frame(uint8_t msg_id,
                            uint8_t seq_num,
                            const void *payload,
                            uint8_t payload_len,
                            uint8_t *out_frame) {
    if (!out_frame) return 0;
    if (payload_len > SPLIT_BUS_MAX_PAYLOAD) return 0;
    if (payload_len > 0 && !payload) return 0;

    out_frame[0] = SPLIT_BUS_SOF0;
    out_frame[1] = SPLIT_BUS_SOF1;
    out_frame[2] = msg_id;
    out_frame[3] = seq_num;
    out_frame[4] = payload_len;

    if (payload_len > 0) {
        memcpy(&out_frame[5], payload, payload_len);
    }

    /* CRC computed over [MsgID, SeqNum, Len, Payload] */
    uint16_t crc = split_bus_crc16(&out_frame[2], (size_t)(3U + payload_len));

    out_frame[5U + payload_len] = (uint8_t)(crc >> 8);
    out_frame[5U + payload_len + 1U] = (uint8_t)(crc & 0xFFU);

    return (uint8_t)(SPLIT_BUS_HEADER_SIZE + payload_len + SPLIT_BUS_CRC_SIZE);
}

/* -------------------------------------------------------------------------- */
/* Receiver State Machine Implementation                                      */
/* -------------------------------------------------------------------------- */
void split_bus_receiver_init(split_bus_receiver_t *rx) {
    if (!rx) return;
    memset(rx, 0, sizeof(split_bus_receiver_t));
    rx->state = BUS_STATE_SEARCH_SOF0;
    rx->last_rx_timestamp_ms = 0;
    rx->bus_off_triggered = false;
}

bool split_bus_receiver_feed(split_bus_receiver_t *rx,
                            uint8_t byte,
                            uint32_t now_ms,
                            split_bus_frame_t *out_frame) {
    if (!rx || !out_frame) return false;

    switch (rx->state) {
        case BUS_STATE_SEARCH_SOF0:
            if (byte == SPLIT_BUS_SOF0) {
                rx->state = BUS_STATE_SEARCH_SOF1;
            }
            break;

        case BUS_STATE_SEARCH_SOF1:
            if (byte == SPLIT_BUS_SOF1) {
                rx->state = BUS_STATE_READ_MSGID;
                rx->stats.rx_sof_resyncs++;
            } else if (byte == SPLIT_BUS_SOF0) {
                /* Consecutive 0xAA remains in SOF1 search */
                rx->state = BUS_STATE_SEARCH_SOF1;
            } else {
                rx->state = BUS_STATE_SEARCH_SOF0;
            }
            break;

        case BUS_STATE_READ_MSGID:
            rx->rx_frame.msg_id = byte;
            rx->state = BUS_STATE_READ_SEQ;
            break;

        case BUS_STATE_READ_SEQ:
            rx->rx_frame.seq_num = byte;
            rx->state = BUS_STATE_READ_LEN;
            break;

        case BUS_STATE_READ_LEN:
            if (byte > SPLIT_BUS_MAX_PAYLOAD) {
                rx->stats.rx_len_errors++;
                rx->state = BUS_STATE_SEARCH_SOF0;
            } else {
                rx->rx_frame.payload_len = byte;
                rx->payload_idx = 0;
                if (byte == 0) {
                    rx->state = BUS_STATE_READ_CRC_HI;
                } else {
                    rx->state = BUS_STATE_READ_PAYLOAD;
                }
            }
            break;

        case BUS_STATE_READ_PAYLOAD:
            rx->rx_frame.payload[rx->payload_idx++] = byte;
            if (rx->payload_idx >= rx->rx_frame.payload_len) {
                rx->state = BUS_STATE_READ_CRC_HI;
            }
            break;

        case BUS_STATE_READ_CRC_HI:
            rx->rx_frame.crc16 = ((uint16_t)byte) << 8;
            rx->state = BUS_STATE_READ_CRC_LO;
            break;

        case BUS_STATE_READ_CRC_LO: {
            rx->rx_frame.crc16 |= ((uint16_t)byte);

            /* Incremental zero-copy CRC calculation over [MsgID, Seq, Len, Payload] */
            uint8_t hdr_bytes[3];
            hdr_bytes[0] = rx->rx_frame.msg_id;
            hdr_bytes[1] = rx->rx_frame.seq_num;
            hdr_bytes[2] = rx->rx_frame.payload_len;

            uint16_t computed_crc = split_bus_crc16_update(0xFFFFU, hdr_bytes, 3);
            if (rx->rx_frame.payload_len > 0) {
                computed_crc = split_bus_crc16_update(computed_crc, rx->rx_frame.payload, rx->rx_frame.payload_len);
            }

            if (computed_crc == rx->rx_frame.crc16) {
                /* Valid Frame Received */
                rx->stats.rx_valid_frames++;
                rx->last_rx_timestamp_ms = now_ms;
                rx->bus_off_triggered = false;

                /* Check Sequence Continuity */
                if (rx->has_last_seq) {
                    uint8_t expected_seq = (uint8_t)(rx->last_seq + 1U);
                    if (rx->rx_frame.seq_num != expected_seq) {
                        rx->stats.seq_drops++;
                    }
                }
                rx->last_seq = rx->rx_frame.seq_num;
                rx->has_last_seq = true;

                *out_frame = rx->rx_frame;
                rx->state = BUS_STATE_SEARCH_SOF0;
                return true;
            } else {
                /* Checksum Failure */
                rx->stats.rx_crc_errors++;
                rx->state = BUS_STATE_SEARCH_SOF0;
            }
            break;
        }

        default:
            rx->state = BUS_STATE_SEARCH_SOF0;
            break;
    }

    return false;
}

bool split_bus_check_timeout(split_bus_receiver_t *rx, uint32_t now_ms) {
    if (!rx) return false;

    if (rx->last_rx_timestamp_ms > 0 && (now_ms - rx->last_rx_timestamp_ms > SPLIT_BUS_TIMEOUT_MS)) {
        rx->stats.timeout_resets++;
        rx->bus_off_triggered = true;
        rx->state = BUS_STATE_SEARCH_SOF0;
        rx->last_rx_timestamp_ms = now_ms; /* Reset timeout tracker */
        return true;
    }

    return false;
}

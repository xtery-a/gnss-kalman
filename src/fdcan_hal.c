/**
 * @file fdcan_hal.c
 * @brief STM32 Silicon FDCAN (Bosch M_CAN IP Core) Hardware Abstraction Layer Implementation.
 */

#include "fdcan_hal.h"
#include <string.h>

/* DLC Conversion Lookup Table (ISO 11898-1 Table 4) */
static const uint8_t DLC_TO_BYTES[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

uint8_t fdcan_dlc_to_bytes(uint8_t dlc) {
    if (dlc > 15U) return 64U;
    return DLC_TO_BYTES[dlc];
}

uint8_t fdcan_bytes_to_dlc(uint8_t len) {
    if (len <= 8U)  return len;
    if (len <= 12U) return 9U;
    if (len <= 16U) return 10U;
    if (len <= 20U) return 11U;
    if (len <= 24U) return 12U;
    if (len <= 32U) return 13U;
    if (len <= 48U) return 14U;
    return 15U; /* 64 bytes max */
}

bool fdcan_hal_init(fdcan_handle_t *h, const fdcan_bit_timing_t *timing) {
    if (!h || !timing) return false;
    memset(h, 0, sizeof(fdcan_handle_t));

    /* Validate Nominal Bit Timing Parameters */
    if (timing->nominal_prescaler == 0 || timing->nominal_tseg1 == 0 || timing->nominal_tseg2 == 0) {
        return false;
    }

    /* Validate Data Bit Timing Parameters */
    if (timing->data_prescaler == 0 || timing->data_tseg1 == 0 || timing->data_tseg2 == 0) {
        return false;
    }

    h->bit_timing = *timing;
    h->is_initialized = true;
    h->bus_off_active = false;
    h->tec = 0;
    h->rec = 0;

    return true;
}

bool fdcan_hal_add_std_filter(fdcan_handle_t *h, const fdcan_std_filter_t *flt) {
    if (!h || !flt || !h->is_initialized) return false;
    if (h->std_filter_count >= FDCAN_STD_FILTER_MAX) return false;

    h->std_filters[h->std_filter_count++] = *flt;
    return true;
}

bool fdcan_hal_transmit(fdcan_handle_t *h,
                        const fdcan_tx_header_t *hdr,
                        const uint8_t *payload) {
    if (!h || !hdr || !h->is_initialized || h->bus_off_active) return false;

    /* Enforce Classic CAN 8-byte ceiling */
    if (hdr->format == FDCAN_FRAME_CLASSIC && hdr->data_length > 8U) {
        return false;
    }
    if (hdr->data_length > FDCAN_MAX_PAYLOAD_BYTES) {
        return false;
    }
    if (hdr->data_length > 0 && !payload) {
        return false;
    }

    /* Check standard 11-bit boundary */
    if (hdr->id_type == FDCAN_ID_STANDARD && hdr->identifier > 0x7FFU) {
        return false;
    }

    /* Simulate transmission and update counter */
    h->tx_frame_count++;

    /* In low-level hardware: write to Tx Buffer in Message RAM & set TXBAR */
    return true;
}

bool fdcan_hal_inject_rx_frame(fdcan_handle_t *h,
                               const fdcan_rx_header_t *hdr,
                               const uint8_t *payload) {
    if (!h || !hdr || !h->is_initialized || h->bus_off_active) return false;

    /* 1. Hardware Acceptance Filtering */
    bool accepted = false;
    fdcan_filter_action_t action = FDCAN_FILTER_ACCEPT_FIFO0;

    if (h->std_filter_count > 0 && hdr->id_type == FDCAN_ID_STANDARD) {
        for (uint8_t i = 0; i < h->std_filter_count; i++) {
            uint32_t masked_id = hdr->identifier & h->std_filters[i].filter_mask;
            uint32_t target_id = h->std_filters[i].filter_id & h->std_filters[i].filter_mask;
            if (masked_id == target_id) {
                accepted = true;
                action = h->std_filters[i].action;
                break;
            }
        }
    } else {
        /* No filter configured: promiscuous accept into FIFO0 */
        accepted = true;
    }

    if (!accepted || action == FDCAN_FILTER_REJECT) {
        return false; /* Rejected by silicon filter hardware */
    }

    /* 2. Push to Rx FIFO 0 */
    if (h->rx_fifo0_count >= FDCAN_RX_FIFO_COUNT) {
        h->fifo_overflow_count++;
        if (h->rec < 250U) h->rec += 8U;
        if (h->rec >= 255U) h->bus_off_active = true;
        return false; /* FIFO Overflow */
    }

    uint8_t head = h->rx_fifo0_head;
    h->rx_fifo0[head].header = *hdr;
    if (hdr->data_length > 0 && payload) {
        uint8_t copy_len = (hdr->data_length > FDCAN_MAX_PAYLOAD_BYTES) ?
                            FDCAN_MAX_PAYLOAD_BYTES : hdr->data_length;
        memcpy(h->rx_fifo0[head].data, payload, copy_len);
    }

    h->rx_fifo0_head = (uint8_t)((head + 1U) % FDCAN_RX_FIFO_COUNT);
    h->rx_fifo0_count++;
    h->rx_frame_count++;

    return true;
}

bool fdcan_hal_read_fifo0(fdcan_handle_t *h,
                          fdcan_rx_header_t *out_hdr,
                          uint8_t *out_payload) {
    if (!h || !out_hdr || !h->is_initialized) return false;
    if (h->rx_fifo0_count == 0) return false;

    uint8_t tail = h->rx_fifo0_tail;
    *out_hdr = h->rx_fifo0[tail].header;
    if (out_payload && out_hdr->data_length > 0) {
        memcpy(out_payload, h->rx_fifo0[tail].data, out_hdr->data_length);
    }

    h->rx_fifo0_tail = (uint8_t)((tail + 1U) % FDCAN_RX_FIFO_COUNT);
    h->rx_fifo0_count--;

    return true;
}

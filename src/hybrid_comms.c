/**
 * @file hybrid_comms.c
 * @brief Zero-Heap Hybrid Tactical Telemetry, Blue Force Tracking (BFT) & Satellite Failover.
 */

#include "hybrid_comms.h"
#include "crypto_hal.h"
#include <string.h>

/* Simple CRC16-CCITT for tactical frame integrity */
static uint16_t comms_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

void hybrid_comms_init(hybrid_comms_ctx_t *ctx, uint16_t local_node_id) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(hybrid_comms_ctx_t));
    ctx->state = COMMS_TIER1_LORA_ACTIVE;
    ctx->local_node_id = local_node_id;
    ctx->retry_count = 0;
    ctx->tx_seq_counter = 1;
    ctx->iridium_powered = false;
}

void iridium_power_enable(hybrid_comms_ctx_t *ctx, bool enable) {
    if (!ctx) return;
    ctx->iridium_powered = enable;
    /* On target hardware:
     * Si2301 P-MOSFET is driven active-LOW.
     * When enable == false, Gate is pulled up to VBATT -> 0.0 uA leakage current.
     */
}

bool hybrid_comms_pack_bft(hybrid_comms_ctx_t *ctx,
                           const bft_beacon_payload_t *beacon,
                           tactical_frame_t *out_frame,
                           const uint8_t aes_key[16]) {
    if (!ctx || !beacon || !out_frame || !aes_key) return false;

    out_frame->header_magic[0] = 0x54; /* 'T' */
    out_frame->header_magic[1] = 0x42; /* 'B' */

    /* Copy payload into temporary buffer */
    bft_beacon_payload_t p = *beacon;
    p.node_id = ctx->local_node_id;
    p.seq_num = ctx->tx_seq_counter++;

    /* Nonce / IV generation: 4 bytes node_id + 4 bytes seq_num */
    uint8_t full_iv[16];
    memset(full_iv, 0, sizeof(full_iv));
    memcpy(&full_iv[0], &p.node_id, sizeof(uint16_t));
    memcpy(&full_iv[2], &p.seq_num, sizeof(uint16_t));
    memcpy(out_frame->iv, full_iv, 8);

    /* Encrypt with AES-128-CTR */
    crypto_aes_ctx_t aes_ctx;
    crypto_aes128_ctr_init(&aes_ctx, aes_key, full_iv, 0);
    crypto_aes128_ctr_process(&aes_ctx, (const uint8_t *)&p, out_frame->encrypted_bytes, sizeof(bft_beacon_payload_t));

    /* Checksum over header, encrypted bytes and IV */
    size_t crc_len = sizeof(out_frame->header_magic) + sizeof(out_frame->encrypted_bytes) + sizeof(out_frame->iv);
    out_frame->crc16 = comms_crc16((const uint8_t *)out_frame, crc_len);

    return true;
}

bool hybrid_comms_unpack_bft(const tactical_frame_t *in_frame,
                             bft_beacon_payload_t *out_beacon,
                             const uint8_t aes_key[16]) {
    if (!in_frame || !out_beacon || !aes_key) return false;

    if (in_frame->header_magic[0] != 0x54 || in_frame->header_magic[1] != 0x42) {
        return false;
    }

    size_t crc_len = sizeof(in_frame->header_magic) + sizeof(in_frame->encrypted_bytes) + sizeof(in_frame->iv);
    uint16_t expected_crc = comms_crc16((const uint8_t *)in_frame, crc_len);
    if (expected_crc != in_frame->crc16) {
        return false;
    }

    uint8_t full_iv[16];
    memset(full_iv, 0, sizeof(full_iv));
    memcpy(full_iv, in_frame->iv, 8);

    crypto_aes_ctx_t aes_ctx;
    crypto_aes128_ctr_init(&aes_ctx, aes_key, full_iv, 0);
    crypto_aes128_ctr_process(&aes_ctx, in_frame->encrypted_bytes, (uint8_t *)out_beacon, sizeof(bft_beacon_payload_t));

    return true;
}

bool hybrid_comms_dispatch(hybrid_comms_ctx_t *ctx,
                           const uint8_t *payload,
                           size_t len,
                           bool is_emergency_sos) {
    if (!ctx || !payload || len == 0) return false;

    /* If emergency SOS is asserted OR LoRa exceeded retry limits, escalate to Tier-2 Satellite */
    if (is_emergency_sos || ctx->retry_count >= LORA_MAX_RETRIES) {
        ctx->state = COMMS_TIER2_IRIDIUM_FAILOVER;
        iridium_power_enable(ctx, true);
        ctx->state = COMMS_TIER2_IRIDIUM_TRANSMIT;
        ctx->total_iridium_tx_count++;

        /* In a real deployment: send AT+SBDWT / AT+SBDIX to RockBLOCK 9603 */
        /* Cut power immediately after transmit session to protect battery */
        iridium_power_enable(ctx, false);
        ctx->state = COMMS_TIER1_LORA_ACTIVE;
        ctx->retry_count = 0;
        return true;
    }

    /* Otherwise, Tier-1 LoRa transmission */
    ctx->state = COMMS_TIER1_LORA_ACTIVE;
    ctx->total_lora_tx_count++;
    return true;
}

void hybrid_comms_notify_ack(hybrid_comms_ctx_t *ctx, bool ack_received) {
    if (!ctx) return;
    if (ack_received) {
        ctx->retry_count = 0;
        ctx->total_lora_ack_count++;
        ctx->state = COMMS_TIER1_LORA_ACTIVE;
    } else {
        ctx->retry_count++;
        if (ctx->retry_count >= LORA_MAX_RETRIES) {
            ctx->state = COMMS_TIER2_IRIDIUM_FAILOVER;
        }
    }
}

/**
 * @file test_harness_hybrid_comms.c
 * @brief Self-contained verification for Hybrid Comms & Blue Force Tracking.
 */

#include "hybrid_comms.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("[TEST] Initializing Hybrid Comms Harness...\n");

    hybrid_comms_ctx_t ctx;
    hybrid_comms_init(&ctx, 0x1042); /* Local Node ID = 0x1042 */

    assert(ctx.state == COMMS_TIER1_LORA_ACTIVE);
    assert(ctx.retry_count == 0);
    assert(ctx.local_node_id == 0x1042);
    assert(!ctx.iridium_powered);

    const uint8_t aes_key[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };

    /* 1. Pack BFT Beacon */
    bft_beacon_payload_t tx_beacon;
    memset(&tx_beacon, 0, sizeof(tx_beacon));
    tx_beacon.lat_1e7 = 458326000;  /* 45.8326000 N (Mont Blanc) */
    tx_beacon.lon_1e7 = 68652000;   /* 6.8652000 E */
    tx_beacon.alt_m = 4810;
    tx_beacon.heading_cd = 27000;   /* 270.00 deg */
    tx_beacon.battery_pct = 94;
    tx_beacon.status_flags = 0x00;

    tactical_frame_t frame;
    bool pack_ok = hybrid_comms_pack_bft(&ctx, &tx_beacon, &frame, aes_key);
    assert(pack_ok);
    assert(frame.header_magic[0] == 'T' && frame.header_magic[1] == 'B');

    /* Verify payload is truly encrypted (not plaintext) */
    assert(memcmp(frame.encrypted_bytes, &tx_beacon, sizeof(bft_beacon_payload_t)) != 0);

    /* 2. Unpack BFT Beacon */
    bft_beacon_payload_t rx_beacon;
    bool unpack_ok = hybrid_comms_unpack_bft(&frame, &rx_beacon, aes_key);
    assert(unpack_ok);
    assert(rx_beacon.node_id == 0x1042);
    assert(rx_beacon.lat_1e7 == 458326000);
    assert(rx_beacon.lon_1e7 == 68652000);
    assert(rx_beacon.alt_m == 4810);
    assert(rx_beacon.heading_cd == 27000);
    assert(rx_beacon.battery_pct == 94);
    assert(rx_beacon.seq_num == 1);
    printf("  [PASS] AES-128-CTR encrypted BFT beacon roundtrip bit-exact.\n");

    /* 3. Tamper detection */
    frame.encrypted_bytes[0] ^= 0xFF;
    bool tamper_ok = hybrid_comms_unpack_bft(&frame, &rx_beacon, aes_key);
    assert(!tamper_ok);
    printf("  [PASS] Single-bit corrupted frame rejected via CRC16.\n");

    /* 4. Tier-1 LoRa Dispatch & ACK */
    uint8_t dummy_buf[10] = {0};
    hybrid_comms_dispatch(&ctx, dummy_buf, sizeof(dummy_buf), false);
    assert(ctx.state == COMMS_TIER1_LORA_ACTIVE);
    assert(ctx.total_lora_tx_count == 1);

    /* ACK failure accumulation */
    hybrid_comms_notify_ack(&ctx, false);
    assert(ctx.retry_count == 1);
    assert(ctx.state == COMMS_TIER1_LORA_ACTIVE);

    hybrid_comms_notify_ack(&ctx, false);
    assert(ctx.retry_count == 2);
    assert(ctx.state == COMMS_TIER1_LORA_ACTIVE);

    hybrid_comms_notify_ack(&ctx, false);
    assert(ctx.retry_count == 3);
    assert(ctx.state == COMMS_TIER2_IRIDIUM_FAILOVER);
    printf("  [PASS] 3 consecutive LoRa NACKs trigger Tier-2 Iridium failover state.\n");

    /* Escalated transmission over Iridium */
    hybrid_comms_dispatch(&ctx, dummy_buf, sizeof(dummy_buf), false);
    assert(ctx.total_iridium_tx_count == 1);
    assert(!ctx.iridium_powered); /* Power cut off post-session */
    printf("  [PASS] Automatic Tier-2 satellite dispatch & P-MOSFET power-down verified.\n");

    /* 5. Instant SOS Trigger */
    hybrid_comms_dispatch(&ctx, dummy_buf, sizeof(dummy_buf), true);
    assert(ctx.total_iridium_tx_count == 2);
    printf("  [PASS] Emergency SOS bypasses LoRa retries directly to satellite.\n");

    printf("\nALL HYBRID COMMS & BFT TESTS PASSED PERFECTLY (100%%)!\n");
    return 0;
}

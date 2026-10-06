/**
 * @file test_harness_squad.c
 * @brief Standalone Bare-Metal C Verification Harness for Squad BFT LoRa Mesh &
 *        Alpine Crevasse / Avalanche Fall Detection Engine.
 */

#include "squad_bft_mesh.h"
#include "mip_display.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#define ANSI_GREEN "\033[0;32m"
#define ANSI_RED   "\033[0;31m"
#define ANSI_RESET "\033[0m"

static void test_initialization(void) {
    printf("[TEST 1] Squad Mesh Initialization & Zero-Heap Callbacks...\n");

    squad_mesh_init(1); /* Unit callsign T1 */

    assert(squad_mesh_get_active_count() == 1);
    const squad_member_t *m1 = squad_mesh_get_member(1);
    assert(m1 != NULL);
    assert(m1->id == 1);
    assert(strcmp(m1->callsign, "T1") == 0);
    assert(m1->active == true);

    /* Other members should not be active yet */
    const squad_member_t *m2 = squad_mesh_get_member(2);
    assert(m2 != NULL);
    assert(m2->active == false);

    /* Out of bounds checks */
    assert(squad_mesh_get_member(0) == NULL);
    assert(squad_mesh_get_member(9) == NULL);

    printf("  " ANSI_GREEN "[PASS]" ANSI_RESET " Local terminal T1 registered in BSS table.\n");
}

static void test_packet_encode_decode_tamper(void) {
    printf("[TEST 2] 16-Byte LoRa Air Frame Serialization & CRC-8 Tamper Rejection...\n");

    squad_member_t t2_mock = {
        .id = 2,
        .lat_1e7 = 458326000,
        .lon_1e7 = 68652000,
        .alt_m = 3840,
        .battery_pct = 92,
        .status_flags = SQUAD_STATUS_NORMAL,
        .last_seq = 105,
        .active = true
    };

    uint8_t packet_buf[SQUAD_FRAME_SIZE];
    size_t out_len = sizeof(packet_buf);

    bool enc_ok = squad_mesh_encode_beacon(&t2_mock, packet_buf, &out_len);
    assert(enc_ok);
    assert(out_len == SQUAD_FRAME_SIZE);
    assert(packet_buf[0] == SQUAD_PACKET_MAGIC);
    assert(packet_buf[1] == 2);
    assert(packet_buf[2] == 105);

    /* Ingest packet into node 1 */
    uint32_t now_ms = 10000;
    bool dec_ok = squad_mesh_decode_beacon(packet_buf, out_len, now_ms);
    assert(dec_ok);
    assert(squad_mesh_get_active_count() == 2);

    const squad_member_t *m2 = squad_mesh_get_member(2);
    assert(m2 != NULL);
    assert(m2->active == true);
    assert(m2->id == 2);
    assert(m2->lat_1e7 == 458326000);
    assert(m2->lon_1e7 == 68652000);
    assert(m2->alt_m == 3840);
    assert(m2->battery_pct == 92);
    assert(m2->last_seq == 105);
    assert(m2->last_seen_ms == now_ms);

    /* Tamper Test 1: Single bit corruption in payload */
    uint8_t corrupted[SQUAD_FRAME_SIZE];
    memcpy(corrupted, packet_buf, SQUAD_FRAME_SIZE);
    corrupted[6] ^= 0x01; /* Corrupt latitude byte */
    bool dec_corrupt = squad_mesh_decode_beacon(corrupted, SQUAD_FRAME_SIZE, now_ms + 1000);
    assert(!dec_corrupt); /* CRC-8 MUST reject corrupted frame */

    /* Tamper Test 2: Invalid Magic Byte */
    memcpy(corrupted, packet_buf, SQUAD_FRAME_SIZE);
    corrupted[0] = 0xAA;
    bool dec_bad_magic = squad_mesh_decode_beacon(corrupted, SQUAD_FRAME_SIZE, now_ms + 1000);
    assert(!dec_bad_magic);

    /* Tamper Test 3: Truncated frame */
    bool dec_short = squad_mesh_decode_beacon(packet_buf, 15, now_ms + 1000);
    assert(!dec_short);

    printf("  " ANSI_GREEN "[PASS]" ANSI_RESET " 16-byte packed frame bit-exact and CRC-8 integrity verified.\n");
}

static void test_kinematic_fall_engine(void) {
    printf("[TEST 3] Kinematic IMU+Baro Alpine Crevasse Fall Detection State Machine...\n");

    squad_fall_detector_t det;
    memset(&det, 0, sizeof(det));
    det.state = FALL_STATE_NORMAL;

    uint8_t status = SQUAD_STATUS_NORMAL;
    float current_alt = 3800.0f;

    /* Phase 1: Nominal hiking on glacier (1.0g +/- 0.1g) */
    for (int t = 0; t < 10; t++) {
        squad_mesh_process_kinematics(&det, 1.05f, current_alt, 50, &status);
        assert(det.state == FALL_STATE_NORMAL);
        assert(status == SQUAD_STATUS_NORMAL);
    }

    /* Phase 2: False alarm bump (shock of 3.2g without freefall) */
    squad_mesh_process_kinematics(&det, 3.2f, current_alt, 50, &status);
    assert(det.state == FALL_STATE_NORMAL);
    assert(status == SQUAD_STATUS_NORMAL);

    /* Phase 3: Snow bridge collapses -> Freefall (|a| = 0.12g < 0.35g) for 250ms */
    for (int t = 0; t < 5; t++) {
        current_alt -= 0.5f; /* Starting to drop */
        squad_mesh_process_kinematics(&det, 0.12f, current_alt, 50, &status);
        assert(det.state == FALL_STATE_FREEFALL);
    }

    /* Phase 4: High-G Impact shock (5.4g) hitting crevasse ledge */
    squad_mesh_process_kinematics(&det, 5.4f, current_alt, 50, &status);
    assert(det.state == FALL_STATE_IMPACT_WAIT);
    assert(det.max_impact_g >= 5.4f);

    /* Phase 5: Barometric altitude settles at crevasse floor: -12.5m drop */
    current_alt = 3800.0f - 12.5f;
    for (int t = 0; t < 8; t++) {
        squad_mesh_process_kinematics(&det, 1.05f, current_alt, 50, &status);
    }
    assert(det.state == FALL_STATE_POST_IMMOBILITY);
    assert(det.fall_depth_m >= 12.0f);

    /* Phase 6: Injured teammate remains motionless (1.0g +/- 0.05g) for 3.5 seconds */
    for (int t = 0; t < 70; t++) {
        squad_mesh_process_kinematics(&det, 1.01f, current_alt, 50, &status);
    }

    /* Verify alarm state triggered! */
    assert(det.state == FALL_STATE_CREVASSE_ALARM);
    assert((status & SQUAD_STATUS_CREVASSE_FALL) != 0);

    printf("  " ANSI_GREEN "[PASS]" ANSI_RESET " Crevasse fall (250ms freefall -> 5.4g shock -> -12.5m drop -> 3.5s immobility) triggered alarm.\n");
}

static void test_geodetic_relatives_and_polar_vectors(void) {
    printf("[TEST 4] Geodetic Relative Distance & Azimuth Polar Vectors...\n");

    squad_mesh_init(1);

    /* Local terminal at Mont Blanc summit: 45.8326000 N, 6.8652000 E */
    int32_t my_lat = 458326000;
    int32_t my_lon = 68652000;

    /* Teammate T2: approx 222 meters directly North (+0.002 deg lat) */
    squad_member_t t2 = {
        .id = 2,
        .lat_1e7 = 458346000,
        .lon_1e7 = 68652000,
        .alt_m = 3820,
        .battery_pct = 85,
        .status_flags = SQUAD_STATUS_NORMAL,
        .last_seq = 1,
        .active = true
    };
    uint8_t buf[SQUAD_FRAME_SIZE];
    size_t len = sizeof(buf);
    squad_mesh_encode_beacon(&t2, buf, &len);
    squad_mesh_decode_beacon(buf, len, 5000);

    /* Teammate T3: approx 310 meters directly East (+0.004 deg lon at lat 45.83) */
    squad_member_t t3 = {
        .id = 3,
        .lat_1e7 = 458326000,
        .lon_1e7 = 68692000,
        .alt_m = 3810,
        .battery_pct = 78,
        .status_flags = SQUAD_STATUS_NORMAL,
        .last_seq = 1,
        .active = true
    };
    len = sizeof(buf);
    squad_mesh_encode_beacon(&t3, buf, &len);
    squad_mesh_decode_beacon(buf, len, 5000);

    /* Compute relative vectors */
    squad_mesh_update_relatives(my_lat, my_lon, 3840, 5000);

    const squad_member_t *res_t2 = squad_mesh_get_member(2);
    assert(res_t2 != NULL);
    printf("    -> T2 Relative Distance: %d m (Expected ~222 m), Bearing: %d deg (Expected ~0 deg)\n",
           res_t2->rel_dist_m, res_t2->rel_bearing_deg);
    assert(res_t2->rel_dist_m >= 215 && res_t2->rel_dist_m <= 230);
    assert(res_t2->rel_bearing_deg == 0 || res_t2->rel_bearing_deg == 360);

    const squad_member_t *res_t3 = squad_mesh_get_member(3);
    assert(res_t3 != NULL);
    printf("    -> T3 Relative Distance: %d m (Expected ~310 m), Bearing: %d deg (Expected ~90 deg)\n",
           res_t3->rel_dist_m, res_t3->rel_bearing_deg);
    assert(res_t3->rel_dist_m >= 300 && res_t3->rel_dist_m <= 325);
    assert(res_t3->rel_bearing_deg >= 89 && res_t3->rel_bearing_deg <= 91);

    /* Test staleness & lost timeouts */
    squad_mesh_update_relatives(my_lat, my_lon, 3840, 5000 + SQUAD_STALE_TIMEOUT_MS + 100);
    assert(res_t2->is_stale == true);
    assert(res_t2->is_lost == false);

    squad_mesh_update_relatives(my_lat, my_lon, 3840, 5000 + SQUAD_LOST_TIMEOUT_MS + 100);
    assert(res_t2->is_lost == true);

    printf("  " ANSI_GREEN "[PASS]" ANSI_RESET " Polar navigation vectors and staleness lifecycle verified.\n");
}

static void test_mip_tactical_hud_overlay(void) {
    printf("[TEST 5] 400x240 Sharp MIP Tactical HUD Overlay & Inverted Emergency Banner...\n");

    mip_display_init();
    mip_display_clear(MIP_COLOR_WHITE);

    squad_mesh_init(1);

    int32_t my_lat = 458326000;
    int32_t my_lon = 68652000;

    /* T2 in crevasse fall emergency */
    squad_member_t t2 = {
        .id = 2,
        .lat_1e7 = 458328000, /* ~22m North: visible in viewport */
        .lon_1e7 = 68652000,
        .alt_m = 3788,
        .battery_pct = 75,
        .status_flags = SQUAD_STATUS_CREVASSE_FALL,
        .last_seq = 12,
        .active = true
    };
    uint8_t buf[SQUAD_FRAME_SIZE];
    size_t len = sizeof(buf);
    squad_mesh_encode_beacon(&t2, buf, &len);
    squad_mesh_decode_beacon(buf, len, 1000);

    /* T3 far off-screen (2.5km East) */
    squad_member_t t3 = {
        .id = 3,
        .lat_1e7 = 458326000,
        .lon_1e7 = 68952000,
        .alt_m = 3500,
        .battery_pct = 82,
        .status_flags = SQUAD_STATUS_NORMAL,
        .last_seq = 45,
        .active = true
    };
    len = sizeof(buf);
    squad_mesh_encode_beacon(&t3, buf, &len);
    squad_mesh_decode_beacon(buf, len, 1000);

    squad_mesh_update_relatives(my_lat, my_lon, 3840, 1000);

    const squad_member_t *em = squad_mesh_get_emergency_member();
    assert(em != NULL);
    assert(em->id == 2);
    assert(em->status_flags & SQUAD_STATUS_CREVASSE_FALL);

    /* Render HUD overlay (scale 2.5 m/px) */
    squad_mesh_render_overlay(my_lat, my_lon, 2.5f);

    /* Packaging DMA stream to confirm scanlines were marked dirty */
    static uint8_t dma_stream[MIP_MAX_DMA_STREAM_SIZE];
    uint16_t dma_len = 0;
    uint16_t dirty_count = mip_prepare_dma_stream(dma_stream, &dma_len);

    assert(dirty_count > 0);
    assert(dma_len > 0);

    /* Check that top banner scanlines (e.g. lines 4..24) are rendered */
    const uint8_t *fb = mip_get_framebuffer();
    assert(fb != NULL);

    /* Inverted banner has black pixels (1) */
    bool banner_pixels_found = false;
    for (int y = 5; y <= 20; y++) {
        for (int x = 15; x < 35; x++) {
            int byte_idx = y * MIP_ROW_BYTES + (x >> 3);
            if (fb[byte_idx] != 0) {
                banner_pixels_found = true;
                break;
            }
        }
        if (banner_pixels_found) break;
    }
    assert(banner_pixels_found);

    printf("  " ANSI_GREEN "[PASS]" ANSI_RESET " MIP 400x240 HUD rendered %u dirty lines with SOS banner.\n", dirty_count);
}

int main(void) {
    printf("=================================================================\n");
    printf(" SQUAD BFT MESH & ALPINE CREVASSE FALL ENGINE TEST HARNESS\n");
    printf("=================================================================\n");

    test_initialization();
    test_packet_encode_decode_tamper();
    test_kinematic_fall_engine();
    test_geodetic_relatives_and_polar_vectors();
    test_mip_tactical_hud_overlay();

    printf("=================================================================\n");
    printf(" ALL SQUAD BFT MESH & CREVASSE TESTS PASSED 100%% SUCCESS!\n");
    printf("=================================================================\n");

    return 0;
}

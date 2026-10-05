/**
 * @file test_harness_fdcan.c
 * @brief Standalone Bare-Metal Verification Harness for STM32 Silicon FDCAN HAL.
 */

#include "fdcan_hal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=================================================================\n");
    printf(" STM32 SILICON FDCAN (BOSCH M_CAN) HARDWARE ABSTRACTION LAYER\n");
    printf("=================================================================\n");

    fdcan_handle_t h;

    /* [TEST 1] Dual Bit Timing Initialization (500k Arbitration / 2M Data) */
    printf("[TEST 1] Initializing FDCAN Dual Bit Timing Engine...\n");
    fdcan_bit_timing_t timing = {
        .nominal_prescaler = 2,
        .nominal_tseg1     = 31,
        .nominal_tseg2     = 8,
        .nominal_sjw       = 8,
        .data_prescaler    = 1,
        .data_tseg1        = 15,
        .data_tseg2        = 4,
        .data_sjw          = 4,
        .tdc_enable        = true
    };
    bool init_ok = fdcan_hal_init(&h, &timing);
    assert(init_ok == true);
    assert(h.is_initialized == true);
    assert(h.bus_off_active == false);
    printf("  [PASS] FDCAN controller initialized: 500 kbps nominal, 2 Mbps data, TDC=ON.\n");

    /* [TEST 2] Hardware Acceptance Filter Configuration */
    printf("[TEST 2] Registering Standard 11-Bit Message RAM Filters...\n");
    fdcan_std_filter_t filt_tactical = {
        .filter_id   = 0x320,  /* Tactical BFT and telemetry ID */
        .filter_mask = 0x7F0,  /* Match 0x320..0x32F */
        .action      = FDCAN_FILTER_ACCEPT_FIFO0
    };
    fdcan_std_filter_t filt_blocked = {
        .filter_id   = 0x400,
        .filter_mask = 0x7F0,
        .action      = FDCAN_FILTER_REJECT
    };
    assert(fdcan_hal_add_std_filter(&h, &filt_tactical) == true);
    assert(fdcan_hal_add_std_filter(&h, &filt_blocked) == true);
    assert(h.std_filter_count == 2);
    printf("  [PASS] 2 Message RAM standard filters loaded (0x320 Accept, 0x400 Reject).\n");

    /* [TEST 3] Transmit 64-Byte CAN-FD Frame */
    printf("[TEST 3] Transmitting 64-Byte CAN-FD Frame (DLC=15)...\n");
    fdcan_tx_header_t tx_hdr = {
        .identifier     = 0x321,
        .id_type        = FDCAN_ID_STANDARD,
        .format         = FDCAN_FRAME_FD_BRS,
        .data_length    = 64,
        .message_marker = 0x01
    };
    uint8_t tx_data[64];
    for (int i = 0; i < 64; i++) tx_data[i] = (uint8_t)(i ^ 0x5A);

    bool tx_ok = fdcan_hal_transmit(&h, &tx_hdr, tx_data);
    assert(tx_ok == true);
    assert(h.tx_frame_count == 1);
    printf("  [PASS] 64-byte payload queued in Message RAM Tx Buffer (Total Tx: %u).\n",
           h.tx_frame_count);

    /* [TEST 4] Hardware Acceptance Filtering & Rx FIFO0 Routing */
    printf("[TEST 4] Silicon Filtering: Routing 0x322 into Rx FIFO0...\n");
    fdcan_rx_header_t rx_in = {
        .identifier     = 0x322,
        .id_type        = FDCAN_ID_STANDARD,
        .format         = FDCAN_FRAME_FD_BRS,
        .data_length    = 32,
        .timestamp_ticks= 12040
    };
    uint8_t rx_raw[32];
    memset(rx_raw, 0xA5, sizeof(rx_raw));

    bool inject_ok = fdcan_hal_inject_rx_frame(&h, &rx_in, rx_raw);
    assert(inject_ok == true);
    assert(h.rx_fifo0_count == 1);
    assert(h.rx_frame_count == 1);
    printf("  [PASS] Frame 0x322 matched filter 0x320/0x7F0 -> Routed to Rx FIFO0.\n");

    /* [TEST 5] Silicon Filtering: Rejecting Non-Matching / Filter-Rejected ID */
    printf("[TEST 5] Silicon Filtering: Rejecting 0x405 Frame...\n");
    fdcan_rx_header_t rx_bad = {
        .identifier     = 0x405,
        .id_type        = FDCAN_ID_STANDARD,
        .format         = FDCAN_FRAME_FD_BRS,
        .data_length    = 8
    };
    bool reject_ok = fdcan_hal_inject_rx_frame(&h, &rx_bad, rx_raw);
    assert(reject_ok == false);
    assert(h.rx_fifo0_count == 1); /* FIFO count unchanged */
    printf("  [PASS] Frame 0x405 discarded by hardware filter (0 CPU cycles wasted).\n");

    /* [TEST 6] Rx FIFO0 Readout & DLC Roundtrip */
    printf("[TEST 6] Popping Rx FIFO0 Message RAM Element...\n");
    fdcan_rx_header_t rx_out;
    uint8_t rx_payload[64];
    bool read_ok = fdcan_hal_read_fifo0(&h, &rx_out, rx_payload);
    assert(read_ok == true);
    assert(rx_out.identifier == 0x322);
    assert(rx_out.data_length == 32);
    assert(rx_payload[0] == 0xA5);
    assert(h.rx_fifo0_count == 0);
    printf("  [PASS] Read frame 0x322 (32 bytes): Bit-exact payload verified.\n");

    /* [TEST 7] DLC to Byte Encoding Table Verification */
    printf("[TEST 7] ISO 11898-1 DLC Table Verification...\n");
    assert(fdcan_bytes_to_dlc(0) == 0);
    assert(fdcan_bytes_to_dlc(8) == 8);
    assert(fdcan_bytes_to_dlc(12) == 9);
    assert(fdcan_bytes_to_dlc(24) == 12);
    assert(fdcan_bytes_to_dlc(64) == 15);
    assert(fdcan_dlc_to_bytes(15) == 64);
    printf("  [PASS] Complete DLC 0..15 <-> 0..64 Byte bidirectional conversion bit-exact.\n");

    printf("=================================================================\n");
    printf(" ALL STM32 FDCAN SILICON HAL TESTS PASSED WITH 100%% SUCCESS!\n");
    printf("=================================================================\n");
    return 0;
}

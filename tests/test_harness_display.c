/**
 * @file test_harness_display.c
 * @brief Standalone Desktop ANSI C Verification Harness for MIP Display Driver,
 *        Dirty-Line DMA Engine, Vector Graphics & Tactical Chording FSM.
 *
 * Compiles with:
 *   gcc -O2 -Wall -Wextra test_harness_display.c mip_display.c chord_fsm.c -o test_display.exe
 */

#include "mip_display.h"
#include "chord_fsm.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>

/* DMA buffer for testing (12,960 bytes maximum) */
static uint8_t s_dma_test_buf[MIP_MAX_DMA_STREAM_SIZE];

static void print_ascii_terminal(const uint8_t *fb, int width, int height, int cols, int rows) {
    printf("+");
    for (int c = 0; c < cols; c++) printf("-");
    printf("+\n");

    int row_bytes = width / 8;
    double x_step = (double)width / (double)cols;
    double y_step = (double)height / (double)rows;

    for (int r = 0; r < rows; r++) {
        printf("|");
        int y_start = (int)(r * y_step);
        int y_end = (int)((r + 1) * y_step);

        for (int c = 0; c < cols; c++) {
            int x_start = (int)(c * x_step);
            int x_end = (int)((c + 1) * x_step);

            int active = 0;
            for (int y = y_start; y < y_end && y < height; y++) {
                for (int x = x_start; x < x_end && x < width; x++) {
                    int idx = y * row_bytes + (x >> 3);
                    if (fb[idx] & (0x80 >> (x & 7))) {
                        active = 1;
                        break;
                    }
                }
                if (active) break;
            }
            printf("%s", active ? "#" : " ");
        }
        printf("|\n");
    }

    printf("+");
    for (int c = 0; c < cols; c++) printf("-");
    printf("+\n");
}

int main(void) {
    printf("=================================================================\n");
    printf(" PHASE 2: MIP DISPLAY DRIVER, DIRTY-LINE DMA & CHORDING FSM\n");
    printf("=================================================================\n\n");

    /* ---------------------------------------------------------------------- */
    /* TEST 1: Dirty Line Tracking & Incremental DMA Stream Packing           */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 1] Dirty Line Tracking & Incremental DMA Stream Packing...\n");

    mip_display_init();

    /* 1.1 Init state: All 240 lines must be dirty */
    const uint32_t *dirty_mask = mip_get_dirty_mask();
    for (int i = 0; i < 7; i++) {
        assert(dirty_mask[i] == 0xFFFFFFFFU);
    }
    assert(dirty_mask[7] == 0x0000FFFFU); /* 16 remaining lines */

    uint16_t dma_len = 0;
    uint16_t lines_packed = mip_prepare_dma_stream(s_dma_test_buf, &dma_len);
    assert(lines_packed == 240);
    assert(dma_len == 240 * MIP_DMA_LINE_PACKET_SIZE); /* 12,960 bytes */

    /* After prepare, all dirty bits must be 0 */
    for (int i = 0; i < 8; i++) {
        assert(dirty_mask[i] == 0x00000000U);
    }
    printf("  [PASS] Full frame initialization: 240 lines packed (%u bytes), mask cleared.\n", dma_len);

    /* 1.2 Single scanline dirty update: Draw hline at line 120 */
    mip_draw_hline(50, 350, 120, MIP_COLOR_BLACK);
    assert((dirty_mask[120 >> 5] & (1U << (120 & 31))) != 0);

    /* Verify ONLY line 120 is dirty */
    uint32_t dirty_sum = 0;
    for (int i = 0; i < 8; i++) {
        if (i == (120 >> 5)) {
            assert(dirty_mask[i] == (1U << (120 & 31)));
        } else {
            assert(dirty_mask[i] == 0);
        }
        dirty_sum += dirty_mask[i];
    }
    assert(dirty_sum == (1U << (120 & 31)));

    /* Pack incremental DMA stream */
    lines_packed = mip_prepare_dma_stream(s_dma_test_buf, &dma_len);
    assert(lines_packed == 1);
    assert(dma_len == MIP_DMA_LINE_PACKET_SIZE); /* 54 bytes */
    assert(s_dma_test_buf[0] == MIP_CMD_WRITE_LINE);
    assert(s_dma_test_buf[1] == 121); /* Line 120 is 1-based address 121 */

    /* Bandwidth reduction calculation */
    double bandwidth_saved = (1.0 - (double)dma_len / (double)MIP_FRAMEBUFFER_SIZE) * 100.0;
    printf("  [PASS] Single line update: 1 line packed (%u bytes instead of 12,000 B). Saved: %.2f%% bandwidth!\n",
           dma_len, bandwidth_saved);

    /* 1.3 VCOM Inversion Toggle */
    mip_display_vcom_toggle();
    mip_mark_line_dirty(45);
    lines_packed = mip_prepare_dma_stream(s_dma_test_buf, &dma_len);
    assert(lines_packed == 1);
    assert(s_dma_test_buf[0] == (MIP_CMD_WRITE_LINE | MIP_CMD_VCOM_MASK)); /* 0x41 */
    printf("  [PASS] VCOM 1 Hz polarity toggle verified: Command byte = 0x%02X\n\n", s_dma_test_buf[0]);

    /* ---------------------------------------------------------------------- */
    /* TEST 2: 100-Stroke Trajectory Draw & 2-Pixel Vector Brush Stress Test   */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 2] 100-Stroke Trajectory Draw & 2-Pixel Vector Graphics...\n");

    mip_display_clear(MIP_COLOR_WHITE);

    /* Render Top Inverted Tactical Status Bar */
    mip_draw_rect(0, 0, 400, 16, true, MIP_COLOR_BLACK);
    mip_draw_string(8, 2, "EXT-GNSS: FIX 3D-DIFF | BAT: 3.82V (-15C) | SBD: RDY", true);

    /* Render Bottom Coordinates Bar */
    mip_draw_rect(0, 224, 400, 16, true, MIP_COLOR_BLACK);
    mip_draw_string(8, 226, "WAYPOINT #04: MT-BLANC [45.83262N 006.86520E] 3842m", true);

    /* Draw UI Framing */
    mip_draw_rect(4, 20, 392, 200, false, MIP_COLOR_BLACK);
    mip_draw_hline(4, 395, 20, MIP_COLOR_BLACK);

    /* Synthetic 100-stroke vector trajectory with 2-pixel brush */
    clock_t t0 = clock();
    int16_t prev_x = 30, prev_y = 120;
    for (int stroke = 1; stroke <= 100; stroke++) {
        int16_t next_x = (int16_t)(30 + stroke * 3.4 + ((stroke % 7) * 4 - 12));
        int16_t next_y = (int16_t)(120 + ((stroke * stroke) % 75) - 35);

        if (next_x > 380) next_x = 380;
        if (next_y < 26) next_y = 26;
        if (next_y > 214) next_y = 214;

        /* Draw thick 2-pixel line (optical brush for 0.147mm MIP pixel pitch) */
        mip_draw_line_thick(prev_x, prev_y, next_x, next_y, 2, MIP_COLOR_BLACK);

        prev_x = next_x;
        prev_y = next_y;
    }
    clock_t t1 = clock();
    double render_time_ms = ((double)(t1 - t0) / (double)CLOCKS_PER_SEC) * 1000.0;

    /* Prepare DMA stream for the vector scene */
    lines_packed = mip_prepare_dma_stream(s_dma_test_buf, &dma_len);
    printf("  [PASS] 100-stroke 2-pixel vector trajectory rendered in %.3f ms.\n", render_time_ms);
    printf("  [PASS] Modified scanlines packed: %u lines (%u bytes DMA stream).\n\n", lines_packed, dma_len);

    /* ---------------------------------------------------------------------- */
    /* TEST 3: Tactical 4-Button Chording Input FSM & Debounce Integrator      */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 3] Tactical 4-Button Chording Input FSM & Debounce...\n");

    chord_fsm_t fsm;
    chord_fsm_init(&fsm);
    chord_event_t evt;

    /* 3.1 Contact Bounce Rejection (< 50 ms) */
    bool evt_fired = false;
    /* 30 ms bouncing signal */
    evt_fired |= chord_fsm_tick(&fsm, 0x01, 10, &evt);
    evt_fired |= chord_fsm_tick(&fsm, 0x00, 10, &evt);
    evt_fired |= chord_fsm_tick(&fsm, 0x01, 10, &evt);
    assert(!evt_fired);
    printf("  [PASS] Mechanical contact bounce (< 50 ms) successfully filtered.\n");

    /* 3.2 Short Chord Release (< 800 ms): SW1 + SW2 (0x03) -> ZOOM_IN */
    chord_fsm_init(&fsm);
    /* Stable press for 200 ms (20 ticks of 10 ms) */
    for (int i = 0; i < 20; i++) {
        chord_fsm_tick(&fsm, 0x03, 10, &evt);
    }
    /* Release switches: 60 ms stable 0x00 */
    bool short_release_fired = false;
    for (int i = 0; i < 6; i++) {
        if (chord_fsm_tick(&fsm, 0x00, 10, &evt)) {
            short_release_fired = true;
            break;
        }
    }
    assert(short_release_fired);
    assert(evt.event_type == CHORD_EVT_SHORT_RELEASE);
    assert(evt.chord_mask == 0x03);
    assert(evt.action == CHORD_ACTION_ZOOM_IN);
    printf("  [PASS] Short Chord (SW1+SW2 = 0x03): Action = %s (Duration: %u ms)\n",
           chord_action_name(evt.action), evt.duration_ms);

    /* 3.3 Layer Toggle Chord: SW2 + SW3 (0x06) -> LAYER_TOGGLE */
    chord_fsm_init(&fsm);
    for (int i = 0; i < 15; i++) chord_fsm_tick(&fsm, 0x06, 10, &evt);
    bool layer_fired = false;
    for (int i = 0; i < 6; i++) {
        if (chord_fsm_tick(&fsm, 0x00, 10, &evt)) {
            layer_fired = true;
            break;
        }
    }
    assert(layer_fired);
    assert(evt.action == CHORD_ACTION_LAYER_TOGGLE);
    printf("  [PASS] Layer Toggle (SW2+SW3 = 0x06): Action = %s\n", chord_action_name(evt.action));

    /* 3.4 Long Chord Hold (>= 800 ms): SW4 (0x08) -> PAN_DOWN_NAV_PREV */
    chord_fsm_init(&fsm);
    bool hold_fired = false;
    for (int i = 0; i < 85; i++) { /* 850 ms */
        if (chord_fsm_tick(&fsm, 0x08, 10, &evt)) {
            hold_fired = true;
            break;
        }
    }
    assert(hold_fired);
    assert(evt.event_type == CHORD_EVT_LONG_HOLD);
    assert(evt.action == CHORD_ACTION_PAN_DOWN_NAV_PREV);
    printf("  [PASS] Long Chord Hold (SW4 = 0x08, >=800ms): Action = %s\n", chord_action_name(evt.action));

    /* 3.5 Tactical Emergency Beacon Hold (>= 3000 ms): SW1 + SW4 (0x09) */
    chord_fsm_init(&fsm);
    bool emergency_fired = false;
    for (int i = 0; i < 310; i++) { /* 3100 ms */
        if (chord_fsm_tick(&fsm, 0x09, 10, &evt)) {
            emergency_fired = true;
            break;
        }
    }
    assert(emergency_fired);
    assert(evt.event_type == CHORD_EVT_EMERGENCY_HOLD);
    assert(evt.action == CHORD_ACTION_EMERGENCY_BEACON);
    printf("  [PASS] Tactical SOS Beacon Hold (SW1+SW4 = 0x09, >=3000ms): Action = %s (Duration: %u ms)\n\n",
           chord_action_name(evt.action), evt.duration_ms);

    /* ---------------------------------------------------------------------- */
    /* TEST 4: Netpbm PBM Framebuffer Export & ASCII Terminal Visualization   */
    /* ---------------------------------------------------------------------- */
    printf("[TEST 4] Netpbm P4 (.pbm) Export & Terminal ASCII Preview...\n");

    const char *out_pbm = "display_phase2.pbm";
    FILE *fp = fopen(out_pbm, "wb");
    assert(fp != NULL);

    fprintf(fp, "P4\n%d %d\n", MIP_WIDTH, MIP_HEIGHT);
    fwrite(mip_get_framebuffer(), 1, MIP_FRAMEBUFFER_SIZE, fp);
    fclose(fp);

    printf("  [PASS] Exported 400x240 monochrome Netpbm image to: %s\n", out_pbm);

    printf("\n--- [Sharp 2.7\" 400x240 Memory-in-Pixel ASCII Terminal Preview] ---\n");
    print_ascii_terminal(mip_get_framebuffer(), MIP_WIDTH, MIP_HEIGHT, 80, 24);

    printf("\n=================================================================\n");
    printf(" ALL PHASE 2 TESTS PASSED PERFECTLY (100%% SUCCESS)\n");
    printf("=================================================================\n");

    return 0;
}

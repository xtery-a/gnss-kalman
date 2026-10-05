/**
 * @file topo_map.c
 * @brief Zero-Heap ANSI C99 Topographic Map Renderer Implementation.
 *
 * Upgraded with:
 *  - Two-Pass White Halo (Inverted Outline) Route Renderer (4px White + 2px Black).
 *  - Dynamic 7x7 Chevron Vector Heading Arrow.
 *  - Real-time Navigation Telemetry HUD: XTE (Cross Track Error), DTG, COG, Scale.
 *  - Strictly zero heap allocation (CONFIG_HEAP_MEM_POOL_SIZE = 0).
 */

#include "topo_map.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 5x7 ASCII Monospace Font Table (Cols 0..4, LSB at top) */
static const uint8_t s_font5x7[128][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['"'] = {0x00, 0x07, 0x00, 0x07, 0x00},
    ['#'] = {0x14, 0x7F, 0x14, 0x7F, 0x14},
    ['$'] = {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['&'] = {0x36, 0x49, 0x55, 0x22, 0x50},
    ['\''] = {0x00, 0x05, 0x03, 0x00, 0x00},
    ['('] = {0x00, 0x1C, 0x22, 0x41, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1C, 0x00},
    ['*'] = {0x14, 0x08, 0x3E, 0x08, 0x14},
    ['+'] = {0x08, 0x08, 0x3E, 0x08, 0x08},
    [','] = {0x00, 0x50, 0x30, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00},
    [';'] = {0x00, 0x56, 0x36, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x41, 0x80},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14},
    ['>'] = {0x80, 0x41, 0x22, 0x14, 0x08},
    ['?'] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['@'] = {0x32, 0x49, 0x79, 0x41, 0x3E},
    ['A'] = {0x7C, 0x12, 0x11, 0x12, 0x7C},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
    ['['] = {0x00, 0x7F, 0x41, 0x41, 0x00},
    ['\\'] = {0x02, 0x04, 0x08, 0x10, 0x20},
    [']'] = {0x00, 0x41, 0x41, 0x7F, 0x00},
    ['^'] = {0x04, 0x02, 0x01, 0x02, 0x04},
    ['_'] = {0x40, 0x40, 0x40, 0x40, 0x40},
    ['m'] = {0x70, 0x08, 0x70, 0x08, 0x70},
    ['k'] = {0x7F, 0x10, 0x28, 0x44, 0x00},
    ['|'] = {0x00, 0x00, 0x7F, 0x00, 0x00},
};

void topo_map_init(void) {
    mip_display_init();
}

bool topo_map_load_rom(const uint8_t *rom_buf, size_t size) {
    if (!rom_buf || size != MIP_FRAMEBUFFER_SIZE) {
        return false;
    }
    mip_blit_framebuffer(rom_buf, size);
    return true;
}

void topo_map_draw_char_5x7(int16_t x, int16_t y, char c, bool inverted) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = ' ';
    const uint8_t *col_bytes = s_font5x7[idx];

    for (int col = 0; col < 5; col++) {
        uint8_t col_data = col_bytes[col];
        for (int row = 0; row < 7; row++) {
            int16_t px = x + col;
            int16_t py = y + row;
            if (px >= 0 && px < MIP_WIDTH && py >= 0 && py < MIP_HEIGHT) {
                bool bit = (col_data >> row) & 1;
                uint8_t color;
                if (inverted) {
                    color = bit ? MIP_COLOR_WHITE : MIP_COLOR_BLACK;
                } else {
                    color = bit ? MIP_COLOR_BLACK : MIP_COLOR_WHITE;
                }
                mip_draw_pixel(px, py, color);
            }
        }
    }
}

void topo_map_draw_string_5x7(int16_t x, int16_t y, const char *str, bool inverted) {
    if (!str) return;
    int16_t cursor_x = x;
    while (*str) {
        topo_map_draw_char_5x7(cursor_x, y, *str, inverted);
        cursor_x += 6;
        str++;
    }
}

void topo_map_render_peaks(const topo_peak_record_t *peaks, size_t count) {
    if (!peaks) return;
    size_t limit = count > TOPO_MAX_PEAKS ? TOPO_MAX_PEAKS : count;

    for (size_t i = 0; i < limit; i++) {
        int16_t px = peaks[i].x;
        int16_t py = peaks[i].y;
        int16_t ele = peaks[i].ele_m;

        /* Draw summit solid triangle ▲ (base 9px, height 7px) */
        for (int16_t dy = 0; dy < 7; dy++) {
            int16_t half_w = dy;
            int16_t cy = py - 6 + dy;
            if (cy >= TOPO_HEADER_HEIGHT && cy < (MIP_HEIGHT - TOPO_FOOTER_HEIGHT)) {
                for (int16_t cx = px - half_w; cx <= px + half_w; cx++) {
                    if (cx >= 0 && cx < MIP_WIDTH) {
                        mip_draw_pixel(cx, cy, MIP_COLOR_BLACK);
                    }
                }
            }
        }
        /* Inverted white center pixel */
        if ((py - 2) >= TOPO_HEADER_HEIGHT && (py - 2) < (MIP_HEIGHT - TOPO_FOOTER_HEIGHT)) {
            mip_draw_pixel(px, py - 2, MIP_COLOR_WHITE);
        }

        /* Altitude label string e.g. "4924m" */
        char label[16];
        snprintf(label, sizeof(label), "%dm", ele);
        topo_map_draw_string_5x7(px + 8, py - 5, label, false);
    }
}

void topo_map_draw_route_halo(const int16_t (*coords)[2], size_t count) {
    if (!coords || count < 2) return;

    /* PASS 1: Erase 4px white halo along the entire route */
    for (size_t i = 0; i < count - 1; i++) {
        mip_draw_line_thick(coords[i][0], coords[i][1],
                            coords[i + 1][0], coords[i + 1][1],
                            4, MIP_COLOR_WHITE);
    }

    /* PASS 2: Draw crisp 2px solid black core */
    for (size_t i = 0; i < count - 1; i++) {
        mip_draw_line_thick(coords[i][0], coords[i][1],
                            coords[i + 1][0], coords[i + 1][1],
                            2, MIP_COLOR_BLACK);
    }

    /* Start Marker: White halo circle + Concentric Black circle */
    int16_t sx = coords[0][0];
    int16_t sy = coords[0][1];
    for (int16_t dy = -5; dy <= 5; dy++) {
        for (int16_t dx = -5; dx <= 5; dx++) {
            if (dx * dx + dy * dy <= 25) {
                mip_draw_pixel(sx + dx, sy + dy, MIP_COLOR_WHITE);
            }
        }
    }
    for (int16_t dy = -4; dy <= 4; dy++) {
        for (int16_t dx = -4; dx <= 4; dx++) {
            int16_t d2 = dx * dx + dy * dy;
            if (d2 <= 16) {
                uint8_t c = (d2 <= 3 || d2 >= 10) ? MIP_COLOR_BLACK : MIP_COLOR_WHITE;
                mip_draw_pixel(sx + dx, sy + dy, c);
            }
        }
    }

    /* Finish Marker: White halo square + Filled Black square */
    int16_t fx = coords[count - 1][0];
    int16_t fy = coords[count - 1][1];
    for (int16_t dy = -4; dy <= 4; dy++) {
        for (int16_t dx = -4; dx <= 4; dx++) {
            mip_draw_pixel(fx + dx, fy + dy, MIP_COLOR_WHITE);
        }
    }
    for (int16_t dy = -3; dy <= 3; dy++) {
        for (int16_t dx = -3; dx <= 3; dx++) {
            mip_draw_pixel(fx + dx, fy + dy, MIP_COLOR_BLACK);
        }
    }
}

void topo_map_draw_chevron(int16_t x, int16_t y, int16_t heading_deg) {
    float rad = (float)heading_deg * (float)M_PI / 180.0f;
    float cos_h = cosf(rad);
    float sin_h = sinf(rad);

    /* 1. Punch 1px White Halo around Chevron (radius ~6px) */
    for (int16_t dy = -6; dy <= 6; dy++) {
        for (int16_t dx = -6; dx <= 6; dx++) {
            if (dx * dx + dy * dy <= 36) {
                mip_draw_pixel(x + dx, y + dy, MIP_COLOR_WHITE);
            }
        }
    }

    /* 2. Chevron vertices: Tip (0, -5), Left (-4, 4), Notch (0, 1), Right (4, 4) */
    struct { float px; float py; } pts[4] = {
        { 0.0f, -5.0f },
        { -4.0f,  4.0f },
        { 0.0f,  1.0f },
        {  4.0f,  4.0f }
    };

    int16_t rx[4], ry[4];
    for (int i = 0; i < 4; i++) {
        rx[i] = (int16_t)roundf((float)x + pts[i].px * cos_h - pts[i].py * sin_h);
        ry[i] = (int16_t)roundf((float)y + pts[i].px * sin_h + pts[i].py * cos_h);
    }

    /* Draw chevron polygon edges */
    mip_draw_line_thick(rx[0], ry[0], rx[1], ry[1], 1, MIP_COLOR_BLACK);
    mip_draw_line_thick(rx[1], ry[1], rx[2], ry[2], 1, MIP_COLOR_BLACK);
    mip_draw_line_thick(rx[2], ry[2], rx[3], ry[3], 1, MIP_COLOR_BLACK);
    mip_draw_line_thick(rx[3], ry[3], rx[0], ry[0], 1, MIP_COLOR_BLACK);

    /* Fill center core pixel */
    mip_draw_pixel(x, y, MIP_COLOR_BLACK);
    mip_draw_pixel(rx[0], ry[0], MIP_COLOR_BLACK);
}

void topo_map_render_osd(const topo_osd_info_t *osd) {
    if (!osd) return;

    /* Fill Header Bar with Black background (Scanlines 0..15) */
    for (int16_t y = 0; y < TOPO_HEADER_HEIGHT; y++) {
        for (int16_t x = 0; x < MIP_WIDTH; x++) {
            mip_draw_pixel(x, y, MIP_COLOR_BLACK);
        }
    }

    /* Fill Footer Bar with Black background (Scanlines 224..239) */
    for (int16_t y = MIP_HEIGHT - TOPO_FOOTER_HEIGHT; y < MIP_HEIGHT; y++) {
        for (int16_t x = 0; x < MIP_WIDTH; x++) {
            mip_draw_pixel(x, y, MIP_COLOR_BLACK);
        }
    }

    /* Top Status Bar: "GPS: 3D FIX   HDOP: 0.8   SATS: 14   PWR: 98% 4.12V" */
    char hdr_buf[128];
    snprintf(hdr_buf, sizeof(hdr_buf), "%s   HDOP: %.1f   SATS: %u   PWR: %u%% %.2fV",
             osd->gps_fix_str, osd->hdop, osd->sats, osd->pwr_pct, osd->vbatt_v);
    topo_map_draw_string_5x7(4, 4, hdr_buf, true);

    /* Bottom Navigation Telemetry: "XTE: 0m   DTG: 3.8km   COG: 315   |-- 500m --|" */
    char ftr_buf[128];
    char xte_str[16];
    if (osd->xte_m == 0) {
        snprintf(xte_str, sizeof(xte_str), "XTE: 0m");
    } else {
        snprintf(xte_str, sizeof(xte_str), "XTE: %dm %c",
                 (int)abs(osd->xte_m), osd->xte_m > 0 ? 'R' : 'L');
    }

    float dtg_km = (float)osd->dtg_m / 1000.0f;
    snprintf(ftr_buf, sizeof(ftr_buf), "%s   DTG: %.1fkm   COG: %03d   |-- %um --|",
             xte_str, dtg_km, osd->cog_deg, osd->scale_m);
    topo_map_draw_string_5x7(4, MIP_HEIGHT - 12, ftr_buf, true);
}

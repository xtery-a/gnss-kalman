/**
 * @file topo_map.h
 * @brief Zero-Heap ANSI C99 Topographic Terrain Map Renderer for Sharp 2.7" MIP Display.
 *
 * Upgraded with:
 *  - Two-Pass White Halo (Inverted Outline) Route Renderer (4px White + 2px Black).
 *  - Dynamic 7x7 Chevron Vector Heading Arrow.
 *  - Real-time Navigation Telemetry HUD: XTE (Cross Track Error), DTG, COG, Scale.
 *  - Zero dynamic heap allocation (CONFIG_HEAP_MEM_POOL_SIZE = 0).
 */

#ifndef TOPO_MAP_H
#define TOPO_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mip_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOPO_HEADER_HEIGHT       (16)
#define TOPO_FOOTER_HEIGHT       (16)
#define TOPO_MAP_VIEW_HEIGHT     (MIP_HEIGHT - TOPO_HEADER_HEIGHT - TOPO_FOOTER_HEIGHT) /* 208 px */

#define TOPO_MAX_PEAKS           (8)

typedef enum {
    TOPO_LOD_MACRO_500M = 0,   /* 500m scale, 100m/200m contours only */
    TOPO_LOD_MICRO_100M = 1    /* 100m scale, 20m/25m micro contours */
} topo_lod_mode_t;

typedef enum {
    TOPO_ORIENT_NORTH_UP = 0,  /* North is screen UP */
    TOPO_ORIENT_TRACK_UP = 1   /* Heading / COG is screen UP */
} topo_orientation_mode_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t ele_m;
} topo_peak_record_t;

typedef struct {
    char gps_fix_str[16];      /* e.g. "GPS: 3D FIX" */
    float hdop;                /* e.g. 0.8 */
    uint8_t sats;              /* e.g. 14 */
    uint8_t pwr_pct;           /* e.g. 98 */
    float vbatt_v;             /* e.g. 4.12 */
    int16_t xte_m;             /* Cross Track Error in meters (+ Right, - Left) */
    uint32_t dtg_m;            /* Distance to Go in meters */
    int16_t cog_deg;           /* Course Over Ground (0..359) */
    uint16_t scale_m;          /* Scale bar width: 500 or 100 */
    int16_t alt_m;             /* Current Altitude: e.g. 1870 */
} topo_osd_info_t;

/**
 * @brief Initializes the topographic rendering subsystem.
 */
void topo_map_init(void);

/**
 * @brief Blits pre-compiled or streamed 1-bit ROM terrain map directly into the MIP framebuffer.
 *        Automatically marks updated scanlines as dirty for DMA transfer.
 * @param rom_buf Pointer to 12,000-byte 1-bit monochrome bitmap.
 * @param size Buffer size (must equal MIP_FRAMEBUFFER_SIZE).
 * @return true if successful, false if size invalid or buffer NULL.
 */
bool topo_map_load_rom(const uint8_t *rom_buf, size_t size);

/**
 * @brief Draws summit peak markers (▲) with elevation labels onto the map.
 * @param peaks Array of summit records.
 * @param count Number of peaks.
 */
void topo_map_render_peaks(const topo_peak_record_t *peaks, size_t count);

/**
 * @brief Renders the active GPX route polyline with Two-Pass White Halo.
 *        Pass 1 punches a 4px white channel through underlying contours and hillshading.
 *        Pass 2 draws a crisp 2px solid black core down the center.
 * @param coords Array of [x, y] coordinates in screen space.
 * @param count Number of points.
 */
void topo_map_draw_route_halo(const int16_t (*coords)[2], size_t count);

/**
 * @brief Draws a 7x7 dynamic directional Chevron heading arrow at (x, y).
 *        Includes a 1px protective white halo to ensure high visibility.
 * @param x Horizontal pixel position (0..399).
 * @param y Vertical pixel position (0..239).
 * @param heading_deg Heading angle in degrees (0 = North/UP, 90 = East, etc.)
 */
void topo_map_draw_chevron(int16_t x, int16_t y, int16_t heading_deg);

/**
 * @brief Renders tactical top OSD status bar and bottom navigation telemetry bar.
 *        Formats: "GPS: 3D FIX   HDOP: 0.8   SATS: 14   PWR: 98% 4.12V"
 *                 "XTE: 0m   DTG: 3.8km   COG: 315   |-- 500m --|"
 * @param osd Pointer to OSD telemetry data.
 */
void topo_map_render_osd(const topo_osd_info_t *osd);

/**
 * @brief Draws a single 5x7 ASCII character into the framebuffer.
 */
void topo_map_draw_char_5x7(int16_t x, int16_t y, char c, bool inverted);

/**
 * @brief Draws a null-terminated 5x7 ASCII string into the framebuffer.
 */
void topo_map_draw_string_5x7(int16_t x, int16_t y, const char *str, bool inverted);

#ifdef __cplusplus
}
#endif

#endif /* TOPO_MAP_H */

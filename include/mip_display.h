/**
 * @file mip_display.h
 * @brief Zero-Heap Sharp 2.7" Memory-in-Pixel (LS027B7DH01A, 400x240) Display Driver,
 *        Dirty-Line DMA Engine & 2-Pixel Vector Graphics Runtime.
 *
 * Core Features:
 *  - 12,000-byte static BSS framebuffer (400x240, 1-bit monochrome).
 *  - 240-bit Dirty Line tracking mask (uint32_t dirty_lines[8]).
 *  - Sharp MIP SPI DMA packet packager ([Cmd | Addr | 50B Data | 2B Dummy]).
 *  - 1 Hz VCOM periodic inversion logic.
 *  - High-visibility 2-pixel brush Bresenham vector line renderer.
 *  - Fast byte-aligned horizontal/vertical line fills.
 *  - Normal and inverted 8x12 monospace bitmap font engine.
 */

#ifndef MIP_DISPLAY_H
#define MIP_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Physical Display Parameters (Sharp LS027B7DH01A)                          */
/* -------------------------------------------------------------------------- */
#define MIP_WIDTH                   (400)
#define MIP_HEIGHT                  (240)
#define MIP_ROW_BYTES               (MIP_WIDTH / 8)  /* 50 bytes per scanline */
#define MIP_FRAMEBUFFER_SIZE        (MIP_HEIGHT * MIP_ROW_BYTES) /* 12,000 bytes */

#define MIP_DIRTY_WORDS             (8)              /* 8 * 32 = 256 bits for 240 lines */

/* Sharp MIP SPI Frame Constants */
#define MIP_CMD_WRITE_LINE          (0x01U)
#define MIP_CMD_VCOM_MASK           (0x40U)
#define MIP_DMA_LINE_PACKET_SIZE    (54U)            /* 1B Cmd + 1B Addr + 50B Data + 2B Dummy */
#define MIP_MAX_DMA_STREAM_SIZE     (MIP_HEIGHT * MIP_DMA_LINE_PACKET_SIZE) /* 12,960 bytes */

/* Color definitions */
#define MIP_COLOR_WHITE             (0U)             /* Reflective / background */
#define MIP_COLOR_BLACK             (1U)             /* Absorptive / active pixel */

/* -------------------------------------------------------------------------- */
/* Driver Lifecycle & Buffer Management                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initializes the display driver, resets dirty masks, and clears framebuffer.
 */
void mip_display_init(void);

/**
 * @brief Clears the entire framebuffer to white (0x00) or black (0xFF)
 *        and marks all 240 lines as dirty.
 */
void mip_display_clear(uint8_t color);

/**
 * @brief Blits a full 12,000-byte frame directly into the display buffer and marks all scanlines dirty.
 */
void mip_blit_framebuffer(const uint8_t *src, size_t size);

/**
 * @brief Periodically called (e.g. 1 Hz timer) to toggle the VCOM polarity bit.
 * Prevents DC bias polarization degradation of the Sharp Memory LCD panel.
 */
void mip_display_vcom_toggle(void);

/**
 * @brief Returns a direct read-only pointer to the 12,000-byte framebuffer.
 */
const uint8_t *mip_get_framebuffer(void);

/**
 * @brief Returns a direct pointer to the 240-bit dirty line bitmask array (8 words).
 */
const uint32_t *mip_get_dirty_mask(void);

/**
 * @brief Manually marks a specific scanline as dirty.
 */
void mip_mark_line_dirty(uint16_t y);

/**
 * @brief Manually marks all 240 scanlines as dirty (forcing full refresh).
 */
void mip_mark_all_dirty(void);

/**
 * @brief Packs ONLY the modified (dirty) scanlines into the caller-provided SPI DMA buffer.
 * Each packed line adheres to Sharp MIP SPI line protocol:
 *   [Command (1B) | 1-based Line Address (1B) | 50 Bytes Pixel Data | 2 Bytes Dummy]
 *
 * Upon packing, clears the internal dirty line bitmask.
 *
 * @param dma_buf Output buffer (must be at least MIP_MAX_DMA_STREAM_SIZE bytes)
 * @param dma_len Pointer to receive the total byte length of packed DMA stream
 * @return Number of dirty lines packed (0 if frame was clean)
 */
uint16_t mip_prepare_dma_stream(uint8_t *dma_buf, uint16_t *dma_len);

/* -------------------------------------------------------------------------- */
/* Vector Graphics Primitives                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Sets a single pixel in the framebuffer and marks its scanline dirty.
 */
void mip_draw_pixel(int16_t x, int16_t y, uint8_t color);

/**
 * @brief Optimized fast horizontal line fill with byte-aligned word operations.
 */
void mip_draw_hline(int16_t x0, int16_t x1, int16_t y, uint8_t color);

/**
 * @brief Optimized fast vertical line fill.
 */
void mip_draw_vline(int16_t x, int16_t y0, int16_t y1, uint8_t color);

/**
 * @brief Draws a solid or wireframe rectangle.
 */
void mip_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill, uint8_t color);

/**
 * @brief Draws a thick vector line using Cohen-Sutherland boundary clipping and
 *        Bresenham integer arithmetic with a 2x2 brush for sunlight readability.
 *
 * @param x0 Start X
 * @param y0 Start Y
 * @param x1 End X
 * @param y1 End Y
 * @param thickness Stroke thickness (1 for 1px, 2 for 2px optical brush)
 * @param color Pixel color (MIP_COLOR_BLACK or MIP_COLOR_WHITE)
 */
void mip_draw_line_thick(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         uint8_t thickness, uint8_t color);

/* -------------------------------------------------------------------------- */
/* Bitmap Typography (8x12 Fixed-Pitch Monospace)                             */
/* -------------------------------------------------------------------------- */

#define MIP_FONT_WIDTH              (8)
#define MIP_FONT_HEIGHT             (12)

/**
 * @brief Renders a single 8x12 monospace ASCII glyph.
 *
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param c ASCII character (0x20..0x7E)
 * @param invert true for white text on solid black box, false for normal black text
 */
void mip_draw_char(int16_t x, int16_t y, char c, bool invert);

/**
 * @brief Renders a null-terminated string of 8x12 characters.
 */
void mip_draw_string(int16_t x, int16_t y, const char *str, bool invert);

#ifdef __cplusplus
}
#endif

#endif /* MIP_DISPLAY_H */

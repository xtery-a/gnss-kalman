/**
 * @file cgpx_engine.h
 * @brief Zero-Heap ANSI C99 CGPX Trajectory Decompression, AES-128-CTR Decryption,
 *        and 400x240 Memory-in-Pixel (MIP) Framebuffer Renderer Engine.
 *
 * Strict Embedded Invariants:
 *  - ZERO dynamic heap allocation (malloc/free/calloc strictly forbidden)
 *  - Self-contained AES-128-CTR cryptographic kernel
 *  - Minimal SRAM footprint (< 256 bytes streaming context)
 *  - Integer-based Cohen-Sutherland clipping and Bresenham rasterization
 *  - Static 12,000-byte (400x240 / 8) monochrome framebuffer
 */

#ifndef CGPX_ENGINE_H
#define CGPX_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Container Constants & Error Codes                                          */
/* -------------------------------------------------------------------------- */
#define CGPX_MAGIC_CONST        (0x58504743U)  /* "CGPX" */
#define CGPX_VERSION_1_0        (0x0100U)      /* v1.0   */
#define CGPX_FLAG_ENCRYPTED_CTR (0x0001U)      /* Bit 0 = AES-128-CTR */

#define CGPX_HEADER_SIZE_BYTES  (68U)

#define CGPX_SCREEN_WIDTH       (400)
#define CGPX_SCREEN_HEIGHT      (240)
#define CGPX_FRAMEBUFFER_SIZE   ((CGPX_SCREEN_WIDTH * CGPX_SCREEN_HEIGHT) / 8) /* 12,000 Bytes */

typedef enum {
    CGPX_OK                   =  0,
    CGPX_ERR_NULL_PTR         = -1,
    CGPX_ERR_BUFFER_TOO_SMALL = -2,
    CGPX_ERR_INVALID_MAGIC    = -3,
    CGPX_ERR_CRC_MISMATCH     = -4,
    CGPX_ERR_CORRUPT_STREAM   = -5,
    CGPX_ERR_EOF              = -6
} cgpx_err_t;

/* -------------------------------------------------------------------------- */
/* 68-Byte Packed Little-Endian Header Structure                              */
/* -------------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /**< 0x00: Magic 0x58504743 ("CGPX") */
    uint16_t version;        /**< 0x04: Format version (0x0100) */
    uint16_t flags;          /**< 0x06: Container flags (bit 0 = encrypted) */
    uint32_t point_count;    /**< 0x08: Total track points N */
    int32_t  ref_lat;        /**< 0x0C: Point 0 Latitude (1e7 scaled) */
    int32_t  ref_lon;        /**< 0x10: Point 0 Longitude (1e7 scaled) */
    int16_t  ref_ele;        /**< 0x14: Point 0 Elevation (meters) */
    int32_t  min_lat;        /**< 0x16: Bounding Box Min Latitude (1e7) */
    int32_t  max_lat;        /**< 0x1A: Bounding Box Max Latitude (1e7) */
    int32_t  min_lon;        /**< 0x1E: Bounding Box Min Longitude (1e7) */
    int32_t  max_lon;        /**< 0x22: Bounding Box Max Longitude (1e7) */
    int16_t  min_ele;        /**< 0x26: Bounding Box Min Elevation (meters) */
    int16_t  max_ele;        /**< 0x28: Bounding Box Max Elevation (meters) */
    uint8_t  nonce[16];      /**< 0x2A: AES-128-CTR IV / Initial Counter */
    uint32_t payload_size;   /**< 0x3A: Size of encrypted payload in bytes */
    uint32_t crc32;          /**< 0x3E: CRC-32 of encrypted payload */
    uint8_t  reserved[2];    /**< 0x42: Zero padding to align to 68 bytes */
} cgpx_header_t;
#pragma pack(pop)

/* Compile-time verification of header size */
typedef char cgpx_header_size_check[(sizeof(cgpx_header_t) == 68) ? 1 : -1];

/* -------------------------------------------------------------------------- */
/* Decoded Trajectory Point                                                   */
/* -------------------------------------------------------------------------- */
typedef struct {
    int32_t  lat;     /**< Latitude (1e7 scaled fixed point) */
    int32_t  lon;     /**< Longitude (1e7 scaled fixed point) */
    uint32_t time_s;  /**< Timestamp or elapsed seconds */
    int16_t  ele;     /**< Elevation in meters [-500, +9000] */
} cgpx_point_t;

/* -------------------------------------------------------------------------- */
/* Zero-Heap Streaming Reader Context (< 256 bytes)                           */
/* -------------------------------------------------------------------------- */
typedef struct {
    /* 64-bit pointers and 32-bit fields (36 bytes) */
    const uint8_t *payload; /**< Pointer into Flash or RAM container (8B or 4B) */
    uint32_t payload_len;   /**< Encrypted payload length (4B) */
    uint32_t payload_pos;   /**< Current read offset in payload (4B) */
    uint32_t current_idx;   /**< Current point index (4B) */
    uint32_t total_points;  /**< Total point count from header (4B) */
    int32_t  cur_lat;       /**< DPCM accumulated Latitude (4B) */
    int32_t  cur_lon;       /**< DPCM accumulated Longitude (4B) */
    uint32_t cur_time;      /**< DPCM accumulated timestamp/seconds (4B) */

    /* 16-bit and 8-bit state (4 bytes) */
    int16_t  cur_ele;       /**< DPCM accumulated Elevation (2B) */
    uint8_t  ks_idx;        /**< Current position in keystream [0..15] (1B) */
    uint8_t  is_encrypted;  /**< 1 if CTR encrypted, 0 if plaintext (1B) */

    /* Cryptographic block state (208 bytes) */
    uint8_t  counter[16];   /**< 128-bit big-endian counter (16B) */
    uint8_t  keystream[16]; /**< Current 16-byte decrypted keystream block (16B) */
    uint8_t  round_keys[176];/**< 11 round keys of 16 bytes (176B) */
} cgpx_reader_t;

/* Compile-time verification that reader context is strictly less than 256 bytes */
typedef char cgpx_reader_size_check[(sizeof(cgpx_reader_t) < 256) ? 1 : -1];

/* -------------------------------------------------------------------------- */
/* Viewport Projection Matrix                                                 */
/* -------------------------------------------------------------------------- */
typedef struct {
    int16_t width;
    int16_t height;
    int16_t margin;

    int32_t min_lat;
    int32_t max_lat;
    int32_t min_lon;
    int32_t max_lon;

    double cos_mean_lat;
    double scale;
    double x_offset;
    double y_offset;
} cgpx_viewport_t;

/* -------------------------------------------------------------------------- */
/* Public API Functions                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Computes standard IEEE 802.3 CRC-32 over memory buffer.
 */
uint32_t cgpx_crc32(const uint8_t *data, size_t length);

/**
 * @brief Initializes zero-heap streaming reader from CGPX blob.
 * Validates header magic, version, payload bounds, and CRC-32.
 * Expands AES-128 key schedule if encrypted.
 *
 * @param reader Pointer to caller-allocated reader context (< 256 bytes)
 * @param blob Pointer to beginning of CGPX container in Flash or RAM
 * @param blob_len Total size of container in bytes
 * @param key 16-byte AES-128 cryptographic key
 * @return CGPX_OK on success, or negative error code
 */
cgpx_err_t cgpx_reader_init(cgpx_reader_t *reader,
                            const uint8_t *blob,
                            size_t blob_len,
                            const uint8_t key[16]);

/**
 * @brief Streams and decodes the next point from the CGPX container on the fly.
 *
 * @param reader Initialized reader context
 * @param out_point Pointer to receive decoded point
 * @return 1 if point decoded successfully, 0 if EOF reached, negative on error
 */
int cgpx_reader_next_point(cgpx_reader_t *reader, cgpx_point_t *out_point);

/**
 * @brief Initializes screen projection viewport from CGPX header bounding box.
 * Preserves geographic aspect ratio using cos(mean_lat) metric correction.
 */
void cgpx_viewport_init(cgpx_viewport_t *vp,
                        const cgpx_header_t *hdr,
                        int16_t screen_width,
                        int16_t screen_height,
                        int16_t margin);

/**
 * @brief Projects geodetic fixed-point coordinates into 2D screen pixel space.
 */
void cgpx_project_point(const cgpx_viewport_t *vp,
                        int32_t lat,
                        int32_t lon,
                        int16_t *out_x,
                        int16_t *out_y);

/**
 * @brief Clears static 1-bit monochrome framebuffer.
 *
 * @param fb Framebuffer memory (e.g. 12,000 bytes)
 * @param size Framebuffer size in bytes
 * @param fill_byte Value to fill (0x00 for all white/clear, 0xFF for all black)
 */
void cgpx_fb_clear(uint8_t *fb, size_t size, uint8_t fill_byte);

/**
 * @brief Draws a 1-pixel line into 1-bit framebuffer using Bresenham algorithm
 *        with Cohen-Sutherland boundary clipping.
 */
void cgpx_fb_draw_line(uint8_t *fb,
                       int16_t width,
                       int16_t height,
                       int16_t x0,
                       int16_t y0,
                       int16_t x1,
                       int16_t y1);

/**
 * @brief High-level single-call function to decrypt, decompress, and render
 *        an entire CGPX trajectory into a 1-bit monochrome framebuffer.
 *        Requires zero dynamic memory.
 *
 * @param blob Pointer to CGPX binary container
 * @param blob_len Size of CGPX container
 * @param key 16-byte AES-128 key
 * @param fb Pointer to 1-bit framebuffer (e.g., 12,000 bytes for 400x240)
 * @param width Framebuffer pixel width (e.g., 400)
 * @param height Framebuffer pixel height (e.g., 240)
 * @param margin Border padding in pixels (e.g., 16)
 * @param out_points_rendered Optional pointer to receive total points drawn
 * @return CGPX_OK on success, or negative error code
 */
cgpx_err_t cgpx_render_route(const uint8_t *blob,
                             size_t blob_len,
                             const uint8_t key[16],
                             uint8_t *fb,
                             int16_t width,
                             int16_t height,
                             int16_t margin,
                             uint32_t *out_points_rendered);

#ifdef __cplusplus
}
#endif

#endif /* CGPX_ENGINE_H */

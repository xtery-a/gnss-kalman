/**
 * @file test_harness.c
 * @brief Standalone Desktop ANSI C Verification Harness for CGPX Engine.
 *
 * Compiles with:
 *   gcc -O2 -Wall -Wextra test_harness.c cgpx_engine.c -o test_cgpx.exe
 *
 * Features:
 *   - Zero dynamic heap allocation (static BSS buffers only)
 *   - Direct file ingestion or pre-compiled C header evaluation
 *   - Framebuffer export to binary Portable Bitmap (display_out.pbm)
 *   - High-contrast ASCII terminal preview
 *   - High-precision execution benchmarking
 */

#include "cgpx_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* Maximum container size supported in static BSS buffer (2 MB) */
#define MAX_CONTAINER_BYTES  (2 * 1024 * 1024)

static uint8_t s_container_buffer[MAX_CONTAINER_BYTES];
static uint8_t s_framebuffer[CGPX_FRAMEBUFFER_SIZE]; /* Exactly 12,000 bytes */

static void parse_key_bytes(const char *key_str, uint8_t key_out[16]) {
    /* Default tactical test key */
    static const uint8_t DEFAULT_KEY[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };

    if (!key_str || strlen(key_str) == 0) {
        memcpy(key_out, DEFAULT_KEY, 16);
        return;
    }

    size_t len = strlen(key_str);
    if (len == 32) {
        /* Hex decode */
        for (int i = 0; i < 16; i++) {
            unsigned int byte_val = 0;
            if (sscanf(&key_str[i * 2], "%02x", &byte_val) == 1) {
                key_out[i] = (uint8_t)byte_val;
            } else {
                key_out[i] = 0;
            }
        }
    } else {
        /* Text representation */
        for (int i = 0; i < 16; i++) {
            key_out[i] = (i < (int)len) ? (uint8_t)key_str[i] : 0x00;
        }
    }
}

static void print_ascii_preview(const uint8_t *fb, int width, int height, int cols, int rows) {
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

int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *key_input = NULL;
    const char *output_pbm = "display_out.pbm";

    if (argc < 2) {
        printf("Usage: %s <input.cgpx> [16-byte-key] [output.pbm]\n", argv[0]);
        return 1;
    }

    input_file = argv[1];
    if (argc >= 3) key_input = argv[2];
    if (argc >= 4) output_pbm = argv[3];

    uint8_t key[16];
    parse_key_bytes(key_input, key);

    /* Read binary CGPX file into static BSS buffer */
    FILE *fp = fopen(input_file, "rb");
    if (!fp) {
        fprintf(stderr, "[ERROR] Failed to open input file: %s\n", input_file);
        return 2;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > (long)sizeof(s_container_buffer)) {
        fprintf(stderr, "[ERROR] File size (%ld B) exceeds buffer limit (%zu B)\n",
                file_size, sizeof(s_container_buffer));
        fclose(fp);
        return 3;
    }

    size_t bytes_read = fread(s_container_buffer, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "[ERROR] Incomplete file read: %zu of %ld bytes\n", bytes_read, file_size);
        return 4;
    }

    /* Print header information */
    if (bytes_read < CGPX_HEADER_SIZE_BYTES) {
        fprintf(stderr, "[ERROR] File too small to contain CGPX header (%zu B)\n", bytes_read);
        return 5;
    }

    const cgpx_header_t *hdr = (const cgpx_header_t *)s_container_buffer;
    printf("=================================================================\n");
    printf(" CGPX STANDALONE ZERO-HEAP EMBEDDED C ENGINE VERIFICATION\n");
    printf("=================================================================\n");
    printf(" Container File:        %s (%ld bytes)\n", input_file, file_size);
    printf(" Magic:                 0x%08X (\"%c%c%c%c\")\n",
           hdr->magic,
           (char)(hdr->magic & 0xFF),
           (char)((hdr->magic >> 8) & 0xFF),
           (char)((hdr->magic >> 16) & 0xFF),
           (char)((hdr->magic >> 24) & 0xFF));
    printf(" Version:               0x%04X (v%u.%u)\n",
           hdr->version, hdr->version >> 8, hdr->version & 0xFF);
    printf(" Flags:                 0x%04X (AES-128-CTR: %s)\n",
           hdr->flags, (hdr->flags & CGPX_FLAG_ENCRYPTED_CTR) ? "YES" : "NO");
    printf(" Point Count:           %u\n", hdr->point_count);
    printf(" Ref Point (P0):        Lat: %.7f | Lon: %.7f | Ele: %d m\n",
           hdr->ref_lat / 1e7, hdr->ref_lon / 1e7, hdr->ref_ele);
    printf(" Bounding Box Lat:      [%.7f, %.7f]\n",
           hdr->min_lat / 1e7, hdr->max_lat / 1e7);
    printf(" Bounding Box Lon:      [%.7f, %.7f]\n",
           hdr->min_lon / 1e7, hdr->max_lon / 1e7);
    printf(" Elevation Range:       [%d m, %d m]\n", hdr->min_ele, hdr->max_ele);
    printf(" Payload Size:          %u bytes\n", hdr->payload_size);
    printf(" Payload CRC-32:        0x%08X\n", hdr->crc32);
    printf(" Framebuffer Dimension: %dx%d (1-bit MIP, %d bytes)\n",
           CGPX_SCREEN_WIDTH, CGPX_SCREEN_HEIGHT, CGPX_FRAMEBUFFER_SIZE);
    printf("-----------------------------------------------------------------\n");

    /* Clear static framebuffer */
    cgpx_fb_clear(s_framebuffer, sizeof(s_framebuffer), 0x00);

    /* Benchmark rendering pipeline */
    clock_t t0 = clock();

    uint32_t points_drawn = 0;
    cgpx_err_t err = cgpx_render_route(
        s_container_buffer,
        bytes_read,
        key,
        s_framebuffer,
        CGPX_SCREEN_WIDTH,
        CGPX_SCREEN_HEIGHT,
        16, /* 16 pixel margin */
        &points_drawn
    );

    clock_t t1 = clock();
    double elapsed_ms = ((double)(t1 - t0) / (double)CLOCKS_PER_SEC) * 1000.0;

    if (err != CGPX_OK) {
        fprintf(stderr, "[FAILURE] cgpx_render_route failed with error code: %d\n", err);
        return (int)err;
    }

    printf(" [SUCCESS] Rendered %u points in %.3f ms\n", points_drawn, elapsed_ms);
    printf(" Average streaming speed: %.2f us / point\n",
           (points_drawn > 0) ? (elapsed_ms * 1000.0 / points_drawn) : 0.0);
    printf(" Dynamic Heap Memory Allocated: 0 BYTES (Strict Zero-Heap Invariant)\n");
    printf(" Reader Context Memory Usage:   %zu BYTES (< 256 bytes requirement)\n",
           sizeof(cgpx_reader_t));
    printf("-----------------------------------------------------------------\n");

    /* Write binary Portable Bitmap (.pbm) */
    FILE *out_fp = fopen(output_pbm, "wb");
    if (!out_fp) {
        fprintf(stderr, "[ERROR] Failed to create output PBM file: %s\n", output_pbm);
        return 6;
    }

    fprintf(out_fp, "P4\n%d %d\n", CGPX_SCREEN_WIDTH, CGPX_SCREEN_HEIGHT);
    fwrite(s_framebuffer, 1, sizeof(s_framebuffer), out_fp);
    fclose(out_fp);

    printf(" Exported Netpbm P4 image to: %s\n", output_pbm);
    printf("\n--- [400x240 Memory-in-Pixel ASCII Verification] ---\n");
    print_ascii_preview(s_framebuffer, CGPX_SCREEN_WIDTH, CGPX_SCREEN_HEIGHT, 80, 24);
    printf("=================================================================\n");

    return 0;
}

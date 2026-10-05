/**
 * @file cgpx_engine.c
 * @brief Zero-Heap ANSI C99 CGPX Trajectory Decompression, AES-128-CTR Decryption,
 *        and 400x240 Memory-in-Pixel (MIP) Framebuffer Renderer Engine.
 */

#include "cgpx_engine.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------- */
/* Standard IEEE 802.3 CRC-32 Lookup Table & Implementation                   */
/* -------------------------------------------------------------------------- */
static const uint32_t CRC32_TABLE[256] = {
    0x00000000U, 0x77073096U, 0xEE0E612CU, 0x990951BAU, 0x076DC419U, 0x706AF48FU, 0xE963A535U, 0x9E6495A3U,
    0x0EDB8832U, 0x79DCB8A4U, 0xE0D5E91EU, 0x97D2D988U, 0x09B64C2BU, 0x7EB17CBDU, 0xE7B82D07U, 0x90BF1D91U,
    0x1DB71064U, 0x6AB020F2U, 0xF3B97148U, 0x84BE41DEU, 0x1ADAD47DU, 0x6DDDE4EBU, 0xF4D4B551U, 0x83D385C7U,
    0x136C9856U, 0x646BA8C0U, 0xFD62F97AU, 0x8A65C9ECU, 0x14015C4FU, 0x63066CD9U, 0xFA0F3D63U, 0x8D080DF5U,
    0x3B6E20C8U, 0x4C69105EU, 0xD56041E4U, 0xA2677172U, 0x3C03E4D1U, 0x4B04D447U, 0xD20D85FDU, 0xA50AB56BU,
    0x35B5A8FAU, 0x42B2986CU, 0xDBBBC9D6U, 0xACBCF940U, 0x32D86CE3U, 0x45DF5C75U, 0xDCD60DCFU, 0xABD13D59U,
    0x26D930ACU, 0x51DE003AU, 0xC8D75180U, 0xBFD06116U, 0x21B4F4B5U, 0x56B3C423U, 0xCFBA9599U, 0xB8BDA50FU,
    0x2802B89EU, 0x5F058808U, 0xC60CD9B2U, 0xB10BE924U, 0x2F6F7C87U, 0x58684C11U, 0xC1611DABU, 0xB6662D3DU,
    0x76DC4190U, 0x01DB7106U, 0x98D220BCU, 0xEFD5102AU, 0x71B18589U, 0x06B6B51FU, 0x9FBFE4A5U, 0xE8B8D433U,
    0x7807C9A2U, 0x0F00F934U, 0x9609A88EU, 0xE10E9818U, 0x7F6A0DBBU, 0x086D3D2DU, 0x91646C97U, 0xE6635C01U,
    0x6B6B51F4U, 0x1C6C6162U, 0x856530D8U, 0xF262004EU, 0x6C0695EDU, 0x1B01A57BU, 0x8208F4C1U, 0xF50FC457U,
    0x65B0D9C6U, 0x12B7E950U, 0x8BBEB8EAU, 0xFCB9887CU, 0x62DD1DDFU, 0x15DA2D49U, 0x8CD37CF3U, 0xFBD44C65U,
    0x4DB26158U, 0x3AB551CEU, 0xA3BC0074U, 0xD4BB30E2U, 0x4ADFA541U, 0x3DD895D7U, 0xA4D1C46DU, 0xD3D6F4FBU,
    0x4369E96AU, 0x346ED9FCU, 0xAD678846U, 0xDA60B8D0U, 0x44042D73U, 0x33031DE5U, 0xAA0A4C5FU, 0xDD0D7CC9U,
    0x5005713CU, 0x270241AAU, 0xBE0B1010U, 0xC90C2086U, 0x5768B525U, 0x206F85B3U, 0xB966D409U, 0xCE61E49FU,
    0x5EDEF90EU, 0x29D9C998U, 0xB0D09822U, 0xC7D7A8B4U, 0x59B33D17U, 0x2EB40D81U, 0xB7BD5C3BU, 0xC0BA6CADU,
    0xEDB88320U, 0x9ABFB3B6U, 0x03B6E20CU, 0x74B1D29AU, 0xEAD54739U, 0x9DD277AFU, 0x04DB2615U, 0x73DC1683U,
    0xE3630B12U, 0x94643B84U, 0x0D6D6A3EU, 0x7A6A5AA8U, 0xE40ECF0BU, 0x9309FF9DU, 0x0A00AE27U, 0x7D079EB1U,
    0xF00F9344U, 0x8708A3D2U, 0x1E01F268U, 0x6906C2FEU, 0xF762575DU, 0x806567CBU, 0x196C3671U, 0x6E6B06E7U,
    0xFED41B76U, 0x89D32BE0U, 0x10DA7A5AU, 0x67DD4ACCU, 0xF9B9DF6FU, 0x8EBEEFF9U, 0x17B7BE43U, 0x60B08ED5U,
    0xD6D6A3E8U, 0xA1D1937EU, 0x38D8C2C4U, 0x4FDFF252U, 0xD1BB67F1U, 0xA6BC5767U, 0x3FB506DDU, 0x48B2364BU,
    0xD80D2BDAU, 0xAF0A1B4CU, 0x36034AF6U, 0x41047A60U, 0xDF60EFC3U, 0xA867DF55U, 0x316E8EEFU, 0x4669BE79U,
    0xCB61B38CU, 0xBC66831AU, 0x256FD2A0U, 0x5268E236U, 0xCC0C7795U, 0xBB0B4703U, 0x220216B9U, 0x5505262FU,
    0xC5BA3BBEU, 0xB2BD0B28U, 0x2BB45A92U, 0x5CB36A04U, 0xC2D7FFA7U, 0xB5D0CF31U, 0x2CD99E8BU, 0x5BDEAE1DU,
    0x9B64C2B0U, 0xEC63F226U, 0x756AA39CU, 0x026D930AU, 0x9C0906A9U, 0xEB0E363FU, 0x72076785U, 0x05005713U,
    0x95BF4A82U, 0xE2B87A14U, 0x7BB12BAEU, 0x0CB61B38U, 0x92D28E9BU, 0xE5D5BE0DU, 0x7CDCEFB7U, 0x0BDBDF21U,
    0x86D3D2D4U, 0xF1D4E242U, 0x68DDB3F8U, 0x1FDA836EU, 0x81BE16CDU, 0xF6B9265BU, 0x6FB077E1U, 0x18B74777U,
    0x88085AE6U, 0xFF0F6A70U, 0x66063BCAU, 0x11010B5CU, 0x8F659EFFU, 0xF862AE69U, 0x616BFFD3U, 0x166CCF45U,
    0xA00AE278U, 0xD70DD2EEU, 0x4E048354U, 0x3903B3C2U, 0xA7672661U, 0xD06016F7U, 0x4969474DU, 0x3E6E77DBU,
    0xAED16A4AU, 0xD9D65ADCU, 0x40DF0B66U, 0x37D83BF0U, 0xA9BCAE53U, 0xDEBB9EC5U, 0x47B2CF7FU, 0x30B5FFE9U,
    0xBDBDF21CU, 0xCABAC28AU, 0x53B39330U, 0x24B4A3A6U, 0xBAD03605U, 0xCDD70693U, 0x54DE5729U, 0x23D967BFU,
    0xB3667A2EU, 0xC4614AB8U, 0x5D681B02U, 0x2A6F2B94U, 0xB40BBE37U, 0xC30C8EA1U, 0x5A05DF1BU, 0x2D02EF8DU
};

uint32_t cgpx_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        uint8_t table_idx = (uint8_t)((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8) ^ CRC32_TABLE[table_idx];
    }
    return crc ^ 0xFFFFFFFFU;
}

/* -------------------------------------------------------------------------- */
/* Self-Contained AES-128 Cryptographic Engine                                */
/* -------------------------------------------------------------------------- */
static const uint8_t AES_SBOX[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

static const uint8_t AES_RCON[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

static void aes128_key_expansion(const uint8_t key[16], uint8_t round_keys[176]) {
    memcpy(round_keys, key, 16);
    uint8_t temp[4];

    for (int i = 4; i < 44; i++) {
        temp[0] = round_keys[(i - 1) * 4 + 0];
        temp[1] = round_keys[(i - 1) * 4 + 1];
        temp[2] = round_keys[(i - 1) * 4 + 2];
        temp[3] = round_keys[(i - 1) * 4 + 3];

        if ((i % 4) == 0) {
            /* RotWord */
            uint8_t k = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = k;

            /* SubWord */
            temp[0] = AES_SBOX[temp[0]];
            temp[1] = AES_SBOX[temp[1]];
            temp[2] = AES_SBOX[temp[2]];
            temp[3] = AES_SBOX[temp[3]];

            /* Rcon XOR */
            temp[0] ^= AES_RCON[i / 4];
        }

        round_keys[i * 4 + 0] = round_keys[(i - 4) * 4 + 0] ^ temp[0];
        round_keys[i * 4 + 1] = round_keys[(i - 4) * 4 + 1] ^ temp[1];
        round_keys[i * 4 + 2] = round_keys[(i - 4) * 4 + 2] ^ temp[2];
        round_keys[i * 4 + 3] = round_keys[(i - 4) * 4 + 3] ^ temp[3];
    }
}

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

static void aes128_encrypt_block(const uint8_t in[16], uint8_t out[16], const uint8_t round_keys[176]) {
    uint8_t state[16];
    memcpy(state, in, 16);

    /* Round 0: AddRoundKey */
    for (int i = 0; i < 16; i++) {
        state[i] ^= round_keys[i];
    }

    /* Rounds 1 to 9 */
    for (int round = 1; round < 10; round++) {
        /* SubBytes */
        for (int i = 0; i < 16; i++) {
            state[i] = AES_SBOX[state[i]];
        }

        /* ShiftRows */
        uint8_t t;
        /* Row 1 */
        t = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
        /* Row 2 */
        t = state[2]; state[2] = state[10]; state[10] = t;
        t = state[6]; state[6] = state[14]; state[14] = t;
        /* Row 3 */
        t = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;

        /* MixColumns */
        for (int c = 0; c < 4; c++) {
            int idx = c * 4;
            uint8_t s0 = state[idx + 0];
            uint8_t s1 = state[idx + 1];
            uint8_t s2 = state[idx + 2];
            uint8_t s3 = state[idx + 3];

            uint8_t h0 = xtime(s0);
            uint8_t h1 = xtime(s1);
            uint8_t h2 = xtime(s2);
            uint8_t h3 = xtime(s3);

            state[idx + 0] = (uint8_t)(h0 ^ h1 ^ s1 ^ s2 ^ s3);
            state[idx + 1] = (uint8_t)(s0 ^ h1 ^ h2 ^ s2 ^ s3);
            state[idx + 2] = (uint8_t)(s0 ^ s1 ^ h2 ^ h3 ^ s3);
            state[idx + 3] = (uint8_t)(h0 ^ s0 ^ s1 ^ s2 ^ h3);
        }

        /* AddRoundKey */
        const uint8_t *rk = &round_keys[round * 16];
        for (int i = 0; i < 16; i++) {
            state[i] ^= rk[i];
        }
    }

    /* Round 10 */
    for (int i = 0; i < 16; i++) {
        state[i] = AES_SBOX[state[i]];
    }

    /* ShiftRows */
    uint8_t t;
    t = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
    t = state[2]; state[2] = state[10]; state[10] = t;
    t = state[6]; state[6] = state[14]; state[14] = t;
    t = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;

    /* AddRoundKey 10 */
    const uint8_t *rk10 = &round_keys[160];
    for (int i = 0; i < 16; i++) {
        out[i] = (uint8_t)(state[i] ^ rk10[i]);
    }
}

/**
 * @brief Standard 128-bit big-endian counter increment.
 */
static void inc_counter_128(uint8_t counter[16]) {
    for (int i = 15; i >= 0; i--) {
        if (++counter[i] != 0) {
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Zero-Heap Streaming Reader Implementation                                  */
/* -------------------------------------------------------------------------- */
static int reader_get_byte(cgpx_reader_t *r, uint8_t *out_b) {
    if (r->payload_pos >= r->payload_len) {
        return 0; /* EOF */
    }

    uint8_t raw = r->payload[r->payload_pos++];
    if (r->is_encrypted) {
        if (r->ks_idx >= 16) {
            /* Generate next keystream block */
            aes128_encrypt_block(r->counter, r->keystream, r->round_keys);
            inc_counter_128(r->counter);
            r->ks_idx = 0;
        }
        *out_b = (uint8_t)(raw ^ r->keystream[r->ks_idx++]);
    } else {
        *out_b = raw;
    }
    return 1;
}

static int reader_decode_varint(cgpx_reader_t *r, uint32_t *out_val) {
    uint32_t res = 0;
    int shift = 0;
    while (1) {
        uint8_t b;
        if (!reader_get_byte(r, &b)) {
            return CGPX_ERR_EOF;
        }
        res |= ((uint32_t)(b & 0x7FU)) << shift;
        if (!(b & 0x80U)) {
            break;
        }
        shift += 7;
        if (shift >= 35) {
            return CGPX_ERR_CORRUPT_STREAM;
        }
    }
    *out_val = res;
    return CGPX_OK;
}

static inline int32_t zigzag_decode32(uint32_t z) {
    return (int32_t)((z >> 1) ^ (-(int32_t)(z & 1U)));
}

cgpx_err_t cgpx_reader_init(cgpx_reader_t *reader,
                            const uint8_t *blob,
                            size_t blob_len,
                            const uint8_t key[16]) {
    if (!reader || !blob) {
        return CGPX_ERR_NULL_PTR;
    }
    if (blob_len < CGPX_HEADER_SIZE_BYTES) {
        return CGPX_ERR_BUFFER_TOO_SMALL;
    }

    const cgpx_header_t *hdr = (const cgpx_header_t *)blob;

    if (hdr->magic != CGPX_MAGIC_CONST) {
        return CGPX_ERR_INVALID_MAGIC;
    }

    size_t total_required = CGPX_HEADER_SIZE_BYTES + hdr->payload_size;
    if (blob_len < total_required) {
        return CGPX_ERR_BUFFER_TOO_SMALL;
    }

    /* Validate CRC32 of encrypted payload */
    const uint8_t *payload_ptr = blob + CGPX_HEADER_SIZE_BYTES;
    uint32_t computed_crc = cgpx_crc32(payload_ptr, hdr->payload_size);
    if (computed_crc != hdr->crc32) {
        return CGPX_ERR_CRC_MISMATCH;
    }

    /* Initialize crypto */
    reader->is_encrypted = (hdr->flags & CGPX_FLAG_ENCRYPTED_CTR) ? 1 : 0;
    if (reader->is_encrypted) {
        if (!key) {
            return CGPX_ERR_NULL_PTR;
        }
        aes128_key_expansion(key, reader->round_keys);
        memcpy(reader->counter, hdr->nonce, 16);
        reader->ks_idx = 16; /* Force generation of first keystream block on first byte */
    }

    /* Initialize bitstream tracking */
    reader->payload = payload_ptr;
    reader->payload_len = hdr->payload_size;
    reader->payload_pos = 0;

    /* Initialize point accumulators with Point 0 from header */
    reader->cur_lat = hdr->ref_lat;
    reader->cur_lon = hdr->ref_lon;
    reader->cur_ele = hdr->ref_ele;
    reader->cur_time = 0;

    reader->current_idx = 0;
    reader->total_points = hdr->point_count;

    return CGPX_OK;
}

int cgpx_reader_next_point(cgpx_reader_t *reader, cgpx_point_t *out_point) {
    if (!reader || !out_point) {
        return CGPX_ERR_NULL_PTR;
    }
    if (reader->current_idx >= reader->total_points) {
        return 0; /* All points decoded */
    }

    if (reader->current_idx == 0) {
        /* Return reference point 0 from header */
        out_point->lat = reader->cur_lat;
        out_point->lon = reader->cur_lon;
        out_point->ele = reader->cur_ele;
        out_point->time_s = reader->cur_time;
        reader->current_idx++;
        return 1;
    }

    /* Decode differential fields */
    uint32_t z_lat, z_lon, z_ele, d_time;
    cgpx_err_t err;

    if ((err = reader_decode_varint(reader, &z_lat)) != CGPX_OK) return err;
    if ((err = reader_decode_varint(reader, &z_lon)) != CGPX_OK) return err;
    if ((err = reader_decode_varint(reader, &z_ele)) != CGPX_OK) return err;
    if ((err = reader_decode_varint(reader, &d_time)) != CGPX_OK) return err;

    int32_t d_lat = zigzag_decode32(z_lat);
    int32_t d_lon = zigzag_decode32(z_lon);
    int32_t d_ele = zigzag_decode32(z_ele);

    reader->cur_lat += d_lat;
    reader->cur_lon += d_lon;
    reader->cur_ele = (int16_t)(reader->cur_ele + d_ele);
    reader->cur_time += d_time;

    out_point->lat = reader->cur_lat;
    out_point->lon = reader->cur_lon;
    out_point->ele = reader->cur_ele;
    out_point->time_s = reader->cur_time;
    reader->current_idx++;
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Viewport Projection Matrix Implementation                                  */
/* -------------------------------------------------------------------------- */
void cgpx_viewport_init(cgpx_viewport_t *vp,
                        const cgpx_header_t *hdr,
                        int16_t screen_width,
                        int16_t screen_height,
                        int16_t margin) {
    if (!vp || !hdr) return;

    vp->width = screen_width;
    vp->height = screen_height;
    vp->margin = margin;

    vp->min_lat = hdr->min_lat;
    vp->max_lat = hdr->max_lat;
    vp->min_lon = hdr->min_lon;
    vp->max_lon = hdr->max_lon;

    double mean_lat_deg = ((double)hdr->min_lat + (double)hdr->max_lat) / 2.0 / 1e7;
    double mean_lat_rad = mean_lat_deg * (M_PI / 180.0);
    vp->cos_mean_lat = cos(mean_lat_rad);
    if (vp->cos_mean_lat < 1e-4) {
        vp->cos_mean_lat = 1e-4;
    }

    double span_lon = (double)(hdr->max_lon - hdr->min_lon);
    double span_lat = (double)(hdr->max_lat - hdr->min_lat);

    double geo_w = span_lon * vp->cos_mean_lat;
    double geo_h = span_lat;

    if (geo_w < 1.0) geo_w = 1.0;
    if (geo_h < 1.0) geo_h = 1.0;

    double avail_w = (double)(screen_width - 2 * margin);
    double avail_h = (double)(screen_height - 2 * margin);

    double scale_x = avail_w / geo_w;
    double scale_y = avail_h / geo_h;
    vp->scale = (scale_x < scale_y) ? scale_x : scale_y;

    double rendered_w = geo_w * vp->scale;
    double rendered_h = geo_h * vp->scale;

    vp->x_offset = (double)margin + (avail_w - rendered_w) / 2.0;
    vp->y_offset = (double)margin + (avail_h - rendered_h) / 2.0;
}

void cgpx_project_point(const cgpx_viewport_t *vp,
                        int32_t lat,
                        int32_t lon,
                        int16_t *out_x,
                        int16_t *out_y) {
    if (!vp || !out_x || !out_y) return;

    double rel_lon = ((double)lon - (double)vp->min_lon) * vp->cos_mean_lat;
    double rel_lat = ((double)lat - (double)vp->min_lat);

    double px = vp->x_offset + rel_lon * vp->scale;
    double py = ((double)vp->height - 1.0) - (vp->y_offset + rel_lat * vp->scale);

    *out_x = (int16_t)floor(px + 0.5);
    *out_y = (int16_t)floor(py + 0.5);
}

/* -------------------------------------------------------------------------- */
/* 1-Bit Framebuffer Monochrome Rasterizer                                    */
/* -------------------------------------------------------------------------- */
void cgpx_fb_clear(uint8_t *fb, size_t size, uint8_t fill_byte) {
    if (fb && size > 0) {
        memset(fb, fill_byte, size);
    }
}

#define CS_INSIDE 0
#define CS_LEFT   1
#define CS_RIGHT  2
#define CS_BOTTOM 4
#define CS_TOP    8

static inline int cs_outcode(int16_t x, int16_t y, int16_t xmin, int16_t ymin, int16_t xmax, int16_t ymax) {
    int code = CS_INSIDE;
    if (x < xmin) code |= CS_LEFT;
    else if (x > xmax) code |= CS_RIGHT;
    if (y < ymin) code |= CS_TOP;
    else if (y > ymax) code |= CS_BOTTOM;
    return code;
}

static bool cs_clip_line(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1,
                         int16_t xmin, int16_t ymin, int16_t xmax, int16_t ymax) {
    int code0 = cs_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
    int code1 = cs_outcode(*x1, *y1, xmin, ymin, xmax, ymax);

    while (1) {
        if (!(code0 | code1)) {
            return true; /* Both points inside */
        } else if (code0 & code1) {
            return false; /* Trivial reject */
        } else {
            int code_out = code0 ? code0 : code1;
            int32_t x = 0, y = 0;
            int32_t x0_32 = *x0, y0_32 = *y0;
            int32_t x1_32 = *x1, y1_32 = *y1;

            if (code_out & CS_TOP) {
                x = (y1_32 != y0_32) ? (x0_32 + (x1_32 - x0_32) * (ymin - y0_32) / (y1_32 - y0_32)) : x0_32;
                y = ymin;
            } else if (code_out & CS_BOTTOM) {
                x = (y1_32 != y0_32) ? (x0_32 + (x1_32 - x0_32) * (ymax - y0_32) / (y1_32 - y0_32)) : x0_32;
                y = ymax;
            } else if (code_out & CS_RIGHT) {
                y = (x1_32 != x0_32) ? (y0_32 + (y1_32 - y0_32) * (xmax - x0_32) / (x1_32 - x0_32)) : y0_32;
                x = xmax;
            } else if (code_out & CS_LEFT) {
                y = (x1_32 != x0_32) ? (y0_32 + (y1_32 - y0_32) * (xmin - x0_32) / (x1_32 - x0_32)) : y0_32;
                x = xmin;
            }

            if (code_out == code0) {
                *x0 = (int16_t)x;
                *y0 = (int16_t)y;
                code0 = cs_outcode(*x0, *y0, xmin, ymin, xmax, ymax);
            } else {
                *x1 = (int16_t)x;
                *y1 = (int16_t)y;
                code1 = cs_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
            }
        }
    }
}

void cgpx_fb_draw_line(uint8_t *fb,
                       int16_t width,
                       int16_t height,
                       int16_t x0,
                       int16_t y0,
                       int16_t x1,
                       int16_t y1) {
    if (!fb || width <= 0 || height <= 0) return;

    if (!cs_clip_line(&x0, &y0, &x1, &y1, 0, 0, (int16_t)(width - 1), (int16_t)(height - 1))) {
        return;
    }

    int16_t dx = (int16_t)abs(x1 - x0);
    int16_t dy = (int16_t)abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);

    int16_t row_bytes = (int16_t)(width / 8);

    while (1) {
        if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height) {
            uint32_t idx = (uint32_t)y0 * (uint32_t)row_bytes + (uint32_t)(x0 >> 3);
            uint8_t mask = (uint8_t)(0x80U >> (x0 & 7));
            fb[idx] |= mask;
        }

        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err = (int16_t)(err - dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 < dx) {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

cgpx_err_t cgpx_render_route(const uint8_t *blob,
                             size_t blob_len,
                             const uint8_t key[16],
                             uint8_t *fb,
                             int16_t width,
                             int16_t height,
                             int16_t margin,
                             uint32_t *out_points_rendered) {
    if (!blob || !fb) {
        return CGPX_ERR_NULL_PTR;
    }

    cgpx_reader_t reader;
    cgpx_err_t err = cgpx_reader_init(&reader, blob, blob_len, key);
    if (err != CGPX_OK) {
        return err;
    }

    const cgpx_header_t *hdr = (const cgpx_header_t *)blob;
    cgpx_viewport_t vp;
    cgpx_viewport_init(&vp, hdr, width, height, margin);

    cgpx_point_t pt;
    int16_t prev_x = 0, prev_y = 0;
    uint32_t points_drawn = 0;

    int ret = cgpx_reader_next_point(&reader, &pt);
    if (ret == 1) {
        cgpx_project_point(&vp, pt.lat, pt.lon, &prev_x, &prev_y);
        points_drawn++;

        /* Plot initial pixel */
        if (prev_x >= 0 && prev_x < width && prev_y >= 0 && prev_y < height) {
            uint32_t idx = (uint32_t)prev_y * (uint32_t)(width / 8) + (uint32_t)(prev_x >> 3);
            fb[idx] |= (uint8_t)(0x80U >> (prev_x & 7));
        }

        while ((ret = cgpx_reader_next_point(&reader, &pt)) == 1) {
            int16_t cur_x = 0, cur_y = 0;
            cgpx_project_point(&vp, pt.lat, pt.lon, &cur_x, &cur_y);
            cgpx_fb_draw_line(fb, width, height, prev_x, prev_y, cur_x, cur_y);
            prev_x = cur_x;
            prev_y = cur_y;
            points_drawn++;
        }
    }

    if (ret < 0) {
        return (cgpx_err_t)ret;
    }

    if (out_points_rendered) {
        *out_points_rendered = points_drawn;
    }

    return CGPX_OK;
}

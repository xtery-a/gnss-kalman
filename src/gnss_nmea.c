/**
 * @file gnss_nmea.c
 * @brief High-Speed Zero-Heap Circular DMA NMEA Tokenizer & Dual-Band (L1/L5) LC29H Parser.
 */

#include "gnss_nmea.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* -------------------------------------------------------------------------- */
/* Helper Functions                                                           */
/* -------------------------------------------------------------------------- */

static uint8_t hex_to_nibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFFU;
}

/**
 * @brief Extract n-th comma-separated field from an NMEA sentence (0-indexed).
 * Returns true if field exists (even if empty), false if out of range.
 */
static bool get_field(const char *sentence, uint8_t field_idx, char *out_buf, size_t max_len) {
    if (!sentence || !out_buf || max_len == 0) return false;

    uint8_t current_field = 0;
    size_t s_idx = 0;
    size_t out_idx = 0;

    /* Skip leading '$' if present */
    if (sentence[0] == '$') s_idx++;

    while (sentence[s_idx] != '\0' && sentence[s_idx] != '*' && sentence[s_idx] != '\r' && sentence[s_idx] != '\n') {
        if (sentence[s_idx] == ',') {
            if (current_field == field_idx) {
                out_buf[out_idx] = '\0';
                return true;
            }
            current_field++;
            out_idx = 0;
        } else if (current_field == field_idx) {
            if (out_idx + 1 < max_len) {
                out_buf[out_idx++] = sentence[s_idx];
            }
        }
        s_idx++;
    }

    if (current_field == field_idx) {
        out_buf[out_idx] = '\0';
        return true;
    }

    out_buf[0] = '\0';
    return false;
}

/* -------------------------------------------------------------------------- */
/* Circular DMA Tokenizer Implementation                                      */
/* -------------------------------------------------------------------------- */

void gnss_tokenizer_init(gnss_tokenizer_t *tok) {
    if (!tok) return;
    memset(tok, 0, sizeof(gnss_tokenizer_t));
}

void gnss_dma_feed(gnss_tokenizer_t *tok, const uint8_t *data, size_t len) {
    if (!tok || !data || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        uint32_t next_head = (tok->head + 1U) & (GNSS_RING_BUF_SIZE - 1U);
        if (next_head == tok->tail) {
            /* Ring buffer overflow: advance tail to make room and count drop */
            tok->tail = (tok->tail + 1U) & (GNSS_RING_BUF_SIZE - 1U);
            tok->dropped_chars++;
        }
        tok->ring_buf[tok->head] = data[i];
        tok->head = next_head;
    }
}

bool gnss_tokenizer_poll(gnss_tokenizer_t *tok, char *out_sentence, size_t max_len) {
    if (!tok || !out_sentence || max_len == 0) return false;

    while (tok->tail != tok->head) {
        char c = (char)tok->ring_buf[tok->tail];
        tok->tail = (tok->tail + 1U) & (GNSS_RING_BUF_SIZE - 1U);

        if (c == '$') {
            tok->in_sentence = true;
            tok->line_pos = 0;
            tok->line_buf[tok->line_pos++] = c;
            continue;
        }

        if (!tok->in_sentence) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            tok->in_sentence = false;
            if (tok->line_pos < 4) {
                tok->line_pos = 0;
                continue;
            }

            tok->line_buf[tok->line_pos] = '\0';

            /* Validate NMEA Checksum */
            const char *star_ptr = strchr(tok->line_buf, '*');
            if (!star_ptr || (tok->line_pos - (star_ptr - tok->line_buf)) < 3) {
                tok->checksum_errors++;
                tok->line_pos = 0;
                continue;
            }

            uint8_t hi = hex_to_nibble(star_ptr[1]);
            uint8_t lo = hex_to_nibble(star_ptr[2]);
            if (hi > 0x0F || lo > 0x0F) {
                tok->checksum_errors++;
                tok->line_pos = 0;
                continue;
            }
            uint8_t expected_cs = (uint8_t)((hi << 4) | lo);

            /* Compute checksum between '$' and '*' */
            uint8_t computed_cs = 0;
            for (const char *p = tok->line_buf + 1; p < star_ptr; p++) {
                computed_cs ^= (uint8_t)(*p);
            }

            if (computed_cs != expected_cs) {
                tok->checksum_errors++;
                tok->line_pos = 0;
                continue;
            }

            /* Valid NMEA sentence ready */
            size_t copy_len = tok->line_pos;
            if (copy_len >= max_len) copy_len = max_len - 1;
            memcpy(out_sentence, tok->line_buf, copy_len);
            out_sentence[copy_len] = '\0';

            tok->sentences_parsed++;
            tok->line_pos = 0;
            return true;
        }

        if ((uint32_t)(tok->line_pos + 1U) < GNSS_MAX_SENTENCE_LEN) {
            tok->line_buf[tok->line_pos++] = c;
        } else {
            /* Sentence exceeds max length without newline: discard */
            tok->in_sentence = false;
            tok->line_pos = 0;
            tok->dropped_chars++;
        }
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* NMEA Sentence Parsing ($GNRMC and $GNGSV)                                  */
/* -------------------------------------------------------------------------- */

static int32_t parse_coord(const char *coord_str, char dir_char, bool is_longitude) {
    if (!coord_str || coord_str[0] == '\0') return 0;

    int deg_digits = is_longitude ? 3 : 2;
    size_t len = strlen(coord_str);
    if ((int)len < deg_digits) return 0;

    char deg_str[4] = {0};
    strncpy(deg_str, coord_str, deg_digits);
    deg_str[deg_digits] = '\0';
    int32_t deg = atoi(deg_str);

    double minutes = atof(coord_str + deg_digits);
    double total_deg = (double)deg + (minutes / 60.0);
    int32_t val_1e7 = (int32_t)(total_deg * 10000000.0 + 0.5);

    if (dir_char == 'S' || dir_char == 's' || dir_char == 'W' || dir_char == 'w') {
        val_1e7 = -val_1e7;
    }

    return val_1e7;
}

bool gnss_parse_sentence(const char *sentence,
                        gnss_pvt_t *pvt,
                        satellite_state_t *sats,
                        uint8_t max_sats) {
    if (!sentence || !pvt || !sats || max_sats == 0) return false;

    char token[32];

    /* ---------------------------------------------------------------------- */
    /* 1. Parse $GNRMC / $GPRMC (Recommended Minimum Navigation Information)  */
    /* ---------------------------------------------------------------------- */
    if (strstr(sentence, "RMC") != NULL) {
        /* Field 1: UTC Time */
        if (get_field(sentence, 1, token, sizeof(token)) && token[0] != '\0') {
            double time_sec = atof(token);
            uint32_t hh = (uint32_t)(time_sec / 10000);
            uint32_t mm = (uint32_t)((time_sec - (hh * 10000)) / 100);
            double ss = time_sec - (hh * 10000) - (mm * 100);
            pvt->utc_time_ms = (hh * 3600000U) + (mm * 60000U) + (uint32_t)(ss * 1000.0);
        }

        /* Field 2: Status ('A' = valid, 'V' = receiver warning) */
        if (get_field(sentence, 2, token, sizeof(token))) {
            pvt->valid = (token[0] == 'A');
        }

        /* Field 3 & 4: Latitude & N/S */
        char lat_buf[16] = {0};
        char ns_buf[4] = {0};
        get_field(sentence, 3, lat_buf, sizeof(lat_buf));
        get_field(sentence, 4, ns_buf, sizeof(ns_buf));
        if (lat_buf[0] != '\0' && ns_buf[0] != '\0') {
            pvt->lat_1e7 = parse_coord(lat_buf, ns_buf[0], false);
        }

        /* Field 5 & 6: Longitude & E/W */
        char lon_buf[16] = {0};
        char ew_buf[4] = {0};
        get_field(sentence, 5, lon_buf, sizeof(lon_buf));
        get_field(sentence, 6, ew_buf, sizeof(ew_buf));
        if (lon_buf[0] != '\0' && ew_buf[0] != '\0') {
            pvt->lon_1e7 = parse_coord(lon_buf, ew_buf[0], true);
        }

        /* Field 7: Ground Speed in Knots */
        if (get_field(sentence, 7, token, sizeof(token)) && token[0] != '\0') {
            double knots = atof(token);
            /* 1 knot = 0.514444 m/s = 514.444 mm/s */
            pvt->speed_mms = (int32_t)(knots * 514.444444 + 0.5);
        }

        /* Field 8: Track Made Good (Course) in Degrees */
        if (get_field(sentence, 8, token, sizeof(token)) && token[0] != '\0') {
            double course = atof(token);
            pvt->course_cd = (uint16_t)(course * 100.0 + 0.5);
        }

        /* Field 9: Date (DDMMYY) */
        if (get_field(sentence, 9, token, sizeof(token)) && token[0] != '\0') {
            pvt->utc_date = (uint32_t)atoi(token);
        }

        return true;
    }

    /* ---------------------------------------------------------------------- */
    /* 2. Parse $GNGGA / $GPGGA (Global Positioning System Fix Data)          */
    /* ---------------------------------------------------------------------- */
    if (strstr(sentence, "GGA") != NULL) {
        /* Field 6: Fix Quality */
        if (get_field(sentence, 6, token, sizeof(token))) {
            int qual = atoi(token);
            pvt->valid = (qual > 0);
        }

        /* Field 9: Altitude above MSL in meters */
        if (get_field(sentence, 9, token, sizeof(token)) && token[0] != '\0') {
            double alt_m = atof(token);
            pvt->alt_geo_mm = (int32_t)(alt_m * 1000.0 + 0.5);
        }

        return true;
    }

    /* ---------------------------------------------------------------------- */
    /* 3. Parse $GNGSV / $GPGSV (Satellites in View with L1/L5 Dual Band)     */
    /* ---------------------------------------------------------------------- */
    if (strstr(sentence, "GSV") != NULL) {
        /* Check for Signal ID (field 20 in NMEA 4.11 for LC29H) */
        bool is_l5 = false;
        char sig_buf[8] = {0};
        if (get_field(sentence, 20, sig_buf, sizeof(sig_buf)) && sig_buf[0] != '\0') {
            int sig_id = atoi(sig_buf);
            /* LC29H Signal IDs: 1=L1 C/A, 5=L5 (or 2/7/8 for Galileo/BeiDou high/low) */
            if (sig_id == 5 || sig_id == 2 || sig_id == 8) {
                is_l5 = true;
            }
        }

        /* Up to 4 satellites per GSV sentence */
        for (uint8_t slot = 0; slot < 4; slot++) {
            uint8_t base = (uint8_t)(4 + slot * 4);
            char prn_str[8] = {0};
            char elev_str[8] = {0};
            char azim_str[8] = {0};
            char cno_str[8] = {0};

            if (!get_field(sentence, base, prn_str, sizeof(prn_str)) || prn_str[0] == '\0') {
                continue;
            }
            get_field(sentence, (uint8_t)(base + 1), elev_str, sizeof(elev_str));
            get_field(sentence, (uint8_t)(base + 2), azim_str, sizeof(azim_str));
            get_field(sentence, (uint8_t)(base + 3), cno_str, sizeof(cno_str));

            uint8_t prn = (uint8_t)atoi(prn_str);
            if (prn == 0) continue;

            uint8_t elev = (uint8_t)atoi(elev_str);
            uint16_t azim = (uint16_t)atoi(azim_str);
            uint8_t cno = (uint8_t)atoi(cno_str);

            /* Locate existing satellite slot or find free/oldest slot */
            int slot_idx = -1;
            int free_idx = -1;
            for (uint8_t i = 0; i < max_sats; i++) {
                if (sats[i].prn == prn) {
                    slot_idx = i;
                    break;
                }
                if (sats[i].prn == 0 && free_idx < 0) {
                    free_idx = i;
                }
            }

            if (slot_idx < 0) {
                if (free_idx >= 0) {
                    slot_idx = free_idx;
                    sats[slot_idx].prn = prn;
                } else {
                    /* Table full: replace slot 0 or discard */
                    slot_idx = 0;
                    sats[slot_idx].prn = prn;
                }
            }

            sats[slot_idx].elevation_deg = elev;
            sats[slot_idx].azimuth_deg = azim;
            sats[slot_idx].last_update_ms = pvt->utc_time_ms;

            if (is_l5) {
                sats[slot_idx].cno_l5 = cno;
                sats[slot_idx].tracked_l5 = (cno > 0);
            } else {
                sats[slot_idx].cno_l1 = cno;
                sats[slot_idx].tracked_l1 = (cno > 0);
            }
        }

        /* Refresh aggregate satellite counts */
        uint8_t in_view = 0;
        uint8_t l1_cnt = 0;
        uint8_t l5_cnt = 0;
        for (uint8_t i = 0; i < max_sats; i++) {
            if (sats[i].prn != 0) {
                in_view++;
                if (sats[i].tracked_l1) l1_cnt++;
                if (sats[i].tracked_l5) l5_cnt++;
            }
        }
        pvt->sats_in_view = in_view;
        pvt->sats_l1_count = l1_cnt;
        pvt->sats_l5_count = l5_cnt;

        return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Skymask NLOS Rejection Filter                                              */
/* -------------------------------------------------------------------------- */

uint8_t gnss_apply_skymask(satellite_state_t *sats,
                          uint8_t sat_count,
                          const uint8_t skymask_lut_64[GNSS_SKYMASK_BINS],
                          gnss_pvt_t *pvt) {
    if (!sats || !skymask_lut_64 || sat_count == 0) return 0;

    uint8_t clean_count = 0;
    uint8_t nlos_count = 0;

    for (uint8_t i = 0; i < sat_count; i++) {
        if (sats[i].prn == 0) continue;

        /* Map 0..359 azimuth degrees to 0..63 bin index */
        uint16_t azim = sats[i].azimuth_deg % 360;
        uint8_t bin = (uint8_t)((azim * GNSS_SKYMASK_BINS) / 360U);
        if (bin >= GNSS_SKYMASK_BINS) bin = GNSS_SKYMASK_BINS - 1;

        uint8_t horizon_mask_deg = skymask_lut_64[bin];

        if (sats[i].elevation_deg < horizon_mask_deg) {
            /* Satellite is physically obstructed behind topographic obstacle */
            sats[i].is_nlos = true;
            nlos_count++;
        } else {
            sats[i].is_nlos = false;
            clean_count++;
        }
    }

    if (pvt) {
        pvt->sats_clean_count = clean_count;
        pvt->sats_nlos_count = nlos_count;
    }

    return clean_count;
}

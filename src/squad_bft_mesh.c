/**
 * @file squad_bft_mesh.c
 * @brief Zero-Heap ANSI C99 Squad Blue Force Tracking (BFT), Peer-to-Peer LoRa Mesh &
 *        Automated Alpine Crevasse / Avalanche Fall Detection Engine.
 *
 * Designed for Extreme Condition Split-Node GNSS Terminal (EXT-GNSS-SPEC-001):
 *  - 16-Byte Ultra-Short LoRa Air Frames (< 45 ms on-air dwell).
 *  - Kinematic IMU+Baro state machine for automated crevasse fall detection.
 *  - Real-time squad relative polar navigation vectors (Distance & Azimuth).
 *  - 400x240 Sharp MIP Tactical HUD integration with emergency banner overlay.
 *  - Strictly zero dynamic memory allocation (CONFIG_HEAP_MEM_POOL_SIZE = 0).
 */

#include "squad_bft_mesh.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------- */
/* Static BSS Storage (Zero Dynamic Allocation)                               */
/* -------------------------------------------------------------------------- */
static squad_member_t s_squad_table[SQUAD_MAX_MEMBERS];
static uint8_t        s_my_id = 1;

/* -------------------------------------------------------------------------- */
/* Internal CRC-8-CCITT Checksum Calculation (Polynomial 0x07, Init 0x00)     */
/* -------------------------------------------------------------------------- */
static uint8_t calc_crc8_ccitt(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00U;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------- */
/* Public API Implementation                                                  */
/* -------------------------------------------------------------------------- */

void squad_mesh_init(uint8_t my_id) {
    if (my_id >= 1 && my_id <= SQUAD_MAX_MEMBERS) {
        s_my_id = my_id;
    } else {
        s_my_id = 1;
    }

    memset(s_squad_table, 0, sizeof(s_squad_table));

    for (uint8_t i = 0; i < SQUAD_MAX_MEMBERS; i++) {
        s_squad_table[i].id = (uint8_t)(i + 1);
        snprintf(s_squad_table[i].callsign, sizeof(s_squad_table[i].callsign), "T%u", (unsigned int)(i + 1));
        s_squad_table[i].active = false;
        s_squad_table[i].is_stale = false;
        s_squad_table[i].is_lost = false;
    }

    /* Register local member slot */
    uint8_t local_idx = (uint8_t)(s_my_id - 1);
    s_squad_table[local_idx].active = true;
    s_squad_table[local_idx].battery_pct = 100;
}

bool squad_mesh_encode_beacon(const squad_member_t *self, uint8_t *out_buf, size_t *out_len) {
    if (!self || !out_buf || !out_len || *out_len < SQUAD_FRAME_SIZE) {
        return false;
    }

    squad_beacon_packet_t pkt;
    pkt.magic        = SQUAD_PACKET_MAGIC;
    pkt.member_id    = self->id;
    pkt.seq_num      = self->last_seq;
    pkt.status_flags = self->status_flags;
    pkt.lat_1e7      = self->lat_1e7;
    pkt.lon_1e7      = self->lon_1e7;
    pkt.alt_m        = self->alt_m;
    pkt.battery_pct  = self->battery_pct;
    pkt.crc8         = calc_crc8_ccitt((const uint8_t *)&pkt, SQUAD_FRAME_SIZE - 1);

    memcpy(out_buf, &pkt, SQUAD_FRAME_SIZE);
    *out_len = SQUAD_FRAME_SIZE;
    return true;
}

bool squad_mesh_decode_beacon(const uint8_t *in_buf, size_t in_len, uint32_t now_ms) {
    if (!in_buf || in_len != SQUAD_FRAME_SIZE) {
        return false;
    }

    const squad_beacon_packet_t *pkt = (const squad_beacon_packet_t *)in_buf;

    if (pkt->magic != SQUAD_PACKET_MAGIC) {
        return false;
    }

    if (pkt->member_id < 1 || pkt->member_id > SQUAD_MAX_MEMBERS) {
        return false;
    }

    uint8_t expected_crc = calc_crc8_ccitt(in_buf, SQUAD_FRAME_SIZE - 1);
    if (expected_crc != pkt->crc8) {
        return false;
    }

    uint8_t idx = (uint8_t)(pkt->member_id - 1);
    squad_member_t *member = &s_squad_table[idx];

    member->id           = pkt->member_id;
    snprintf(member->callsign, sizeof(member->callsign), "T%u", (unsigned int)pkt->member_id);
    member->lat_1e7      = pkt->lat_1e7;
    member->lon_1e7      = pkt->lon_1e7;
    member->alt_m        = pkt->alt_m;
    member->battery_pct  = pkt->battery_pct;
    member->status_flags = pkt->status_flags;
    member->last_seq     = pkt->seq_num;
    member->last_seen_ms = now_ms;
    member->active       = true;
    member->is_stale     = false;
    member->is_lost      = false;

    return true;
}

void squad_mesh_update_relatives(int32_t my_lat_1e7, int32_t my_lon_1e7, int16_t my_alt_m, uint32_t now_ms) {
    (void)my_alt_m;

    const double R_EARTH = 6371000.0;
    double lat0_rad = (double)my_lat_1e7 * 1e-7 * (M_PI / 180.0);
    double cos_lat0 = cos(lat0_rad);

    for (uint8_t i = 0; i < SQUAD_MAX_MEMBERS; i++) {
        squad_member_t *m = &s_squad_table[i];
        if (!m->active) {
            continue;
        }

        /* Update staleness timeouts (exclude local terminal itself) */
        if (m->id != s_my_id) {
            uint32_t elapsed = now_ms - m->last_seen_ms;
            if (elapsed >= SQUAD_LOST_TIMEOUT_MS) {
                m->is_lost = true;
                m->is_stale = true;
            } else if (elapsed >= SQUAD_STALE_TIMEOUT_MS) {
                m->is_stale = true;
                m->is_lost = false;
            } else {
                m->is_stale = false;
                m->is_lost = false;
            }
        }

        /* Geodetic distance and azimuth calculation */
        double d_lat_rad = ((double)m->lat_1e7 - (double)my_lat_1e7) * 1e-7 * (M_PI / 180.0);
        double d_lon_rad = ((double)m->lon_1e7 - (double)my_lon_1e7) * 1e-7 * (M_PI / 180.0);

        double dy = d_lat_rad * R_EARTH;
        double dx = d_lon_rad * cos_lat0 * R_EARTH;

        double dist = sqrt(dx * dx + dy * dy);
        double bearing_rad = atan2(dx, dy);
        double bearing_deg = bearing_rad * (180.0 / M_PI);
        if (bearing_deg < 0.0) {
            bearing_deg += 360.0;
        }

        m->rel_dist_m = (int16_t)round(dist);
        m->rel_bearing_deg = (int16_t)round(bearing_deg);
    }
}

void squad_mesh_process_kinematics(squad_fall_detector_t *det,
                                   float accel_norm_g,
                                   float baro_alt_m,
                                   uint32_t dt_ms,
                                   uint8_t *out_status) {
    if (!det || !out_status) {
        return;
    }

    switch (det->state) {
        case FALL_STATE_NORMAL:
            det->pre_fall_alt_m = baro_alt_m;
            /* Detect near-zero gravity condition (|a| < 0.35g) */
            if (accel_norm_g < 0.35f) {
                det->state = FALL_STATE_FREEFALL;
                det->state_timer_ms = 0;
                det->max_impact_g = 0.0f;
                det->immobility_timer_ms = 0;
            }
            break;

        case FALL_STATE_FREEFALL:
            det->state_timer_ms += dt_ms;
            /* Violent shock deceleration upon hitting crevasse floor or snow bridge */
            if (accel_norm_g >= 3.0f) {
                if (det->state_timer_ms >= 180U) {
                    /* Genuine vertical free-fall confirmed */
                    det->state = FALL_STATE_IMPACT_WAIT;
                    det->max_impact_g = accel_norm_g;
                    det->state_timer_ms = 0;
                } else {
                    /* Brief anomalous bump/jump (< 180 ms), reset to normal */
                    det->state = FALL_STATE_NORMAL;
                }
            } else if (det->state_timer_ms > 2500U) {
                /* Freefall timeout without impact (sensor anomaly), reset */
                det->state = FALL_STATE_NORMAL;
            }
            break;

        case FALL_STATE_IMPACT_WAIT:
            if (accel_norm_g > det->max_impact_g) {
                det->max_impact_g = accel_norm_g;
            }
            det->state_timer_ms += dt_ms;
            if (det->state_timer_ms >= 300U) {
                float dz = det->pre_fall_alt_m - baro_alt_m;
                det->fall_depth_m = dz;

                /* Drop must be significant (>= 3.0m) with high-G shock (>= 3.5g) */
                if (dz >= 3.0f && det->max_impact_g >= 3.5f) {
                    det->state = FALL_STATE_POST_IMMOBILITY;
                    det->state_timer_ms = 0;
                    det->immobility_timer_ms = 0;
                } else if (det->state_timer_ms > 2000U) {
                    det->state = FALL_STATE_NORMAL;
                }
            }
            break;

        case FALL_STATE_POST_IMMOBILITY:
            /* Immobility check: acceleration norm strictly near 1.0g (+/- 0.15g) */
            if (fabsf(accel_norm_g - 1.0f) <= 0.15f) {
                det->immobility_timer_ms += dt_ms;
                if (det->immobility_timer_ms >= 3000U) {
                    /* 3 seconds motionless post-fall: trigger crevasse emergency */
                    det->state = FALL_STATE_CREVASSE_ALARM;
                    *out_status |= SQUAD_STATUS_CREVASSE_FALL;
                }
            } else {
                /* Teammate is actively moving */
                det->state_timer_ms += dt_ms;
                if (det->state_timer_ms > 8000U) {
                    /* Active movement for > 8s without remaining motionless */
                    det->state = FALL_STATE_NORMAL;
                }
            }
            break;

        case FALL_STATE_CREVASSE_ALARM:
            *out_status |= SQUAD_STATUS_CREVASSE_FALL;
            break;

        case FALL_STATE_AVALANCHE_ALARM:
            *out_status |= SQUAD_STATUS_AVALANCHE_BURIAL;
            break;

        default:
            det->state = FALL_STATE_NORMAL;
            break;
    }
}

const squad_member_t *squad_mesh_get_member(uint8_t member_id) {
    if (member_id < 1 || member_id > SQUAD_MAX_MEMBERS) {
        return NULL;
    }
    return &s_squad_table[member_id - 1];
}

size_t squad_mesh_get_active_count(void) {
    size_t count = 0;
    for (uint8_t i = 0; i < SQUAD_MAX_MEMBERS; i++) {
        if (s_squad_table[i].active) {
            count++;
        }
    }
    return count;
}

const squad_member_t *squad_mesh_get_emergency_member(void) {
    for (uint8_t i = 0; i < SQUAD_MAX_MEMBERS; i++) {
        if (s_squad_table[i].active) {
            uint8_t emergency_mask = SQUAD_STATUS_CREVASSE_FALL |
                                     SQUAD_STATUS_AVALANCHE_BURIAL |
                                     SQUAD_STATUS_MANUAL_SOS;
            if (s_squad_table[i].status_flags & emergency_mask) {
                return &s_squad_table[i];
            }
        }
    }
    return NULL;
}

void squad_mesh_render_overlay(int32_t center_lat_1e7, int32_t center_lon_1e7, float scale_m_per_px) {
    if (scale_m_per_px <= 0.001f) {
        scale_m_per_px = 1.0f;
    }

    const double R_EARTH = 6371000.0;
    double lat0_rad = (double)center_lat_1e7 * 1e-7 * (M_PI / 180.0);
    double cos_lat0 = cos(lat0_rad);

    int16_t cx = (int16_t)(MIP_WIDTH / 2);
    int16_t cy = (int16_t)(MIP_HEIGHT / 2);

    /* Render tactical teammate icons & border off-screen indicators */
    for (uint8_t i = 0; i < SQUAD_MAX_MEMBERS; i++) {
        const squad_member_t *m = &s_squad_table[i];
        if (!m->active || m->id == s_my_id) {
            continue;
        }

        double d_lat_rad = ((double)m->lat_1e7 - (double)center_lat_1e7) * 1e-7 * (M_PI / 180.0);
        double d_lon_rad = ((double)m->lon_1e7 - (double)center_lon_1e7) * 1e-7 * (M_PI / 180.0);

        double dy_m = d_lat_rad * R_EARTH;
        double dx_m = d_lon_rad * cos_lat0 * R_EARTH;

        int16_t sx = (int16_t)round(cx + (dx_m / (double)scale_m_per_px));
        int16_t sy = (int16_t)round(cy - (dy_m / (double)scale_m_per_px));

        bool is_emergency = (m->status_flags & (SQUAD_STATUS_CREVASSE_FALL |
                                                SQUAD_STATUS_AVALANCHE_BURIAL |
                                                SQUAD_STATUS_MANUAL_SOS)) != 0;

        /* Viewport safety margin for in-screen rendering */
        const int16_t x_min = 20;
        const int16_t x_max = MIP_WIDTH - 20;
        const int16_t y_min = 20;
        const int16_t y_max = MIP_HEIGHT - 20;

        if (sx >= x_min && sx <= x_max && sy >= y_min && sy <= y_max) {
            /* IN VIEWPORT: Render 7x7 tactical icon + callsign tag */
            if (is_emergency) {
                /* Solid black emergency square with inverted center */
                mip_draw_rect((int16_t)(sx - 4), (int16_t)(sy - 4), 9, 9, true, MIP_COLOR_BLACK);
                mip_draw_pixel(sx, sy, MIP_COLOR_WHITE);
                mip_draw_string((int16_t)(sx + 6), (int16_t)(sy - 6), m->callsign, false);
                mip_draw_string((int16_t)(sx + 24), (int16_t)(sy - 6), "!", false);
            } else {
                /* Hollow tactical box */
                mip_draw_rect((int16_t)(sx - 3), (int16_t)(sy - 3), 7, 7, false, MIP_COLOR_BLACK);
                mip_draw_pixel(sx, sy, MIP_COLOR_BLACK);
                mip_draw_string((int16_t)(sx + 5), (int16_t)(sy - 6), m->callsign, false);
            }
        } else {
            /* OFF-SCREEN: Clamp ray from center to screen border */
            double vx = (double)(sx - cx);
            double vy = (double)(sy - cy);
            double tx = 1e6;
            double ty = 1e6;

            if (vx > 0.001) {
                tx = (double)(x_max - cx) / vx;
            } else if (vx < -0.001) {
                tx = (double)(x_min - cx) / vx;
            }

            if (vy > 0.001) {
                ty = (double)(y_max - cy) / vy;
            } else if (vy < -0.001) {
                ty = (double)(y_min - cy) / vy;
            }

            double t = (tx < ty) ? tx : ty;
            if (t > 0.0) {
                int16_t ex = (int16_t)round((double)cx + t * vx);
                int16_t ey = (int16_t)round((double)cy + t * vy);

                if (ex < x_min) ex = x_min;
                if (ex > x_max) ex = x_max;
                if (ey < y_min) ey = y_min;
                if (ey > y_max) ey = y_max;

                char tag[20];
                snprintf(tag, sizeof(tag), "%s %dm", m->callsign, (int)m->rel_dist_m);

                int16_t tag_x = (int16_t)(ex - 16);
                int16_t tag_y = (int16_t)(ey - 6);
                if (tag_x < 4) tag_x = 4;
                if (tag_x > MIP_WIDTH - 64) tag_x = (int16_t)(MIP_WIDTH - 64);
                if (tag_y < 4) tag_y = 4;
                if (tag_y > MIP_HEIGHT - 14) tag_y = (int16_t)(MIP_HEIGHT - 14);

                mip_draw_string(tag_x, tag_y, tag, is_emergency);
            }
        }
    }

    /* Check for squad emergency banner */
    const squad_member_t *em = squad_mesh_get_emergency_member();
    if (em != NULL) {
        /* Render high-contrast inverted banner across top of display */
        mip_draw_rect(8, 4, 384, 22, true, MIP_COLOR_BLACK);
        mip_draw_rect(10, 6, 380, 18, false, MIP_COLOR_WHITE);

        char banner_str[48];
        if (em->status_flags & SQUAD_STATUS_CREVASSE_FALL) {
            snprintf(banner_str, sizeof(banner_str), "! SOS: %s CREVASSE FALL (DIST: %dm) !",
                     em->callsign, (int)em->rel_dist_m);
        } else if (em->status_flags & SQUAD_STATUS_AVALANCHE_BURIAL) {
            snprintf(banner_str, sizeof(banner_str), "! SOS: %s AVALANCHE BURIAL (DIST: %dm) !",
                     em->callsign, (int)em->rel_dist_m);
        } else {
            snprintf(banner_str, sizeof(banner_str), "! SOS: %s EMERGENCY ALARM (DIST: %dm) !",
                     em->callsign, (int)em->rel_dist_m);
        }

        mip_draw_string(16, 9, banner_str, true);
    }
}

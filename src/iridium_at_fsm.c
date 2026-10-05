/**
 * @file iridium_at_fsm.c
 * @brief RockBLOCK 9603 (Iridium SBD) Asynchronous Non-Blocking AT Command FSM Implementation.
 */

#include "iridium_at_fsm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void iridium_fsm_init(iridium_fsm_t *fsm, const iridium_hw_ops_t *ops) {
    if (!fsm) return;
    memset(fsm, 0, sizeof(iridium_fsm_t));
    if (ops) fsm->ops = *ops;
    fsm->state = IRIDIUM_STATE_POWERED_OFF;
    fsm->last_error = IRIDIUM_ERR_NONE;

    if (fsm->ops.set_power_enable) {
        fsm->ops.set_power_enable(false); /* Start with P-MOSFET gate HIGH (0.0 uA) */
    }
}

bool iridium_fsm_queue_message(iridium_fsm_t *fsm, const uint8_t *data, uint16_t len) {
    if (!fsm || !data || len == 0 || len > IRIDIUM_MAX_SBD_BYTES) return false;
    if (fsm->session_active) return false; /* Busy with existing transmission */

    memcpy(fsm->tx_payload, data, len);
    fsm->tx_payload_len = len;
    fsm->session_active = true;
    fsm->last_error = IRIDIUM_ERR_NONE;
    fsm->csq_retries = 0;
    fsm->csq_bars = 0;

    /* Begin power-on sequence */
    fsm->state = IRIDIUM_STATE_POWERING_ON;
    if (fsm->ops.set_power_enable) {
        fsm->ops.set_power_enable(true);
    }
    return true;
}

void iridium_fsm_feed_rx(iridium_fsm_t *fsm, const char *data, size_t len) {
    if (!fsm || !data || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        if ((uint32_t)(fsm->rx_idx + 1U) < IRIDIUM_RX_BUF_SIZE) {
            fsm->rx_buf[fsm->rx_idx++] = data[i];
            fsm->rx_buf[fsm->rx_idx] = '\0';
        } else {
            /* Overflow wrap */
            fsm->rx_idx = 0;
            fsm->rx_buf[0] = '\0';
        }
    }
}

static void clear_rx_buffer(iridium_fsm_t *fsm) {
    fsm->rx_idx = 0;
    fsm->rx_buf[0] = '\0';
}

static void send_cmd(iridium_fsm_t *fsm, const char *cmd) {
    clear_rx_buffer(fsm);
    if (fsm->ops.uart_write && cmd) {
        fsm->ops.uart_write((const uint8_t *)cmd, strlen(cmd));
    }
}

void iridium_fsm_step(iridium_fsm_t *fsm, uint32_t now_ms) {
    if (!fsm) return;

    /* Check timeout if duration configured */
    if (fsm->timeout_duration_ms > 0 &&
        (now_ms - fsm->state_entry_time_ms) > fsm->timeout_duration_ms) {
        fsm->last_error = IRIDIUM_ERR_GATEWAY_TIMEOUT;
        fsm->state = IRIDIUM_STATE_ERROR;
        fsm->session_active = false;
        if (fsm->ops.set_power_enable) fsm->ops.set_power_enable(false);
        return;
    }

    switch (fsm->state) {
        case IRIDIUM_STATE_POWERED_OFF:
            /* Waiting for message */
            break;

        case IRIDIUM_STATE_POWERING_ON:
            /* Wait 1500 ms for 9603 cold boot & supercapacitor rail charge */
            if (fsm->state_entry_time_ms == 0) {
                fsm->state_entry_time_ms = now_ms;
            }
            if ((now_ms - fsm->state_entry_time_ms) >= 1500U) {
                fsm->state = IRIDIUM_STATE_PING_AT;
                fsm->state_entry_time_ms = now_ms;
                fsm->timeout_duration_ms = 5000U;
                send_cmd(fsm, "AT\r\n");
            }
            break;

        case IRIDIUM_STATE_PING_AT:
            if (strstr(fsm->rx_buf, "OK") != NULL) {
                fsm->state = IRIDIUM_STATE_DISABLE_ECHO;
                fsm->state_entry_time_ms = now_ms;
                fsm->timeout_duration_ms = 3000U;
                send_cmd(fsm, "ATE0\r\n");
            }
            break;

        case IRIDIUM_STATE_DISABLE_ECHO:
            if (strstr(fsm->rx_buf, "OK") != NULL) {
                fsm->state = IRIDIUM_STATE_CHECK_SIGNAL;
                fsm->state_entry_time_ms = now_ms;
                fsm->timeout_duration_ms = 10000U;
                send_cmd(fsm, "AT+CSQ\r\n");
            }
            break;

        case IRIDIUM_STATE_CHECK_SIGNAL: {
            char *p = strstr(fsm->rx_buf, "+CSQ:");
            if (p != NULL) {
                fsm->csq_bars = (uint8_t)atoi(p + 5);
                if (fsm->csq_bars >= 2U) {
                    /* Good satellite link: write binary buffer header */
                    char cmd_buf[32];
                    snprintf(cmd_buf, sizeof(cmd_buf), "AT+SBDWB=%u\r\n", fsm->tx_payload_len);
                    fsm->state = IRIDIUM_STATE_WRITE_SBD_HDR;
                    fsm->state_entry_time_ms = now_ms;
                    fsm->timeout_duration_ms = 5000U;
                    send_cmd(fsm, cmd_buf);
                } else {
                    /* Low signal: wait and retry up to 5 times */
                    fsm->csq_retries++;
                    if (fsm->csq_retries > 5U) {
                        fsm->last_error = IRIDIUM_ERR_NO_SATELLITE_SIGNAL;
                        fsm->state = IRIDIUM_STATE_ERROR;
                        fsm->session_active = false;
                        if (fsm->ops.set_power_enable) fsm->ops.set_power_enable(false);
                    } else {
                        /* Retry AT+CSQ after 1500 ms */
                        fsm->state_entry_time_ms = now_ms;
                        send_cmd(fsm, "AT+CSQ\r\n");
                    }
                }
            }
            break;
        }

        case IRIDIUM_STATE_WRITE_SBD_HDR:
            if (strstr(fsm->rx_buf, "READY") != NULL) {
                /* Send binary payload + 16-bit big-endian summation checksum */
                clear_rx_buffer(fsm);
                uint16_t checksum = 0;
                for (uint16_t i = 0; i < fsm->tx_payload_len; i++) {
                    checksum = (uint16_t)(checksum + fsm->tx_payload[i]);
                }
                uint8_t cs_bytes[2];
                cs_bytes[0] = (uint8_t)(checksum >> 8);
                cs_bytes[1] = (uint8_t)(checksum & 0xFFU);

                if (fsm->ops.uart_write) {
                    fsm->ops.uart_write(fsm->tx_payload, fsm->tx_payload_len);
                    fsm->ops.uart_write(cs_bytes, 2);
                }

                fsm->state = IRIDIUM_STATE_WRITE_SBD_DATA;
                fsm->state_entry_time_ms = now_ms;
                fsm->timeout_duration_ms = 5000U;
            }
            break;

        case IRIDIUM_STATE_WRITE_SBD_DATA:
            /* Expected response is "0\r\n" */
            if (strstr(fsm->rx_buf, "0") != NULL) {
                fsm->state = IRIDIUM_STATE_SEND_SBDIX;
                fsm->state_entry_time_ms = now_ms;
                fsm->timeout_duration_ms = 60000U; /* Satellite link can take up to 60s */
                send_cmd(fsm, "AT+SBDIX\r\n");
            }
            break;

        case IRIDIUM_STATE_SEND_SBDIX: {
            char *p = strstr(fsm->rx_buf, "+SBDIX:");
            if (p != NULL) {
                /* Format: +SBDIX: <MO status>, <MOMSN>, <MT status>, <MTMSN>, <MT len>, <MT queued> */
                int mo_st = 32, momsn = 0, mt_st = 0, mtmsn = 0, mt_len = 0, mt_queued = 0;
                sscanf(p + 7, "%d, %d, %d, %d, %d, %d",
                       &mo_st, &momsn, &mt_st, &mtmsn, &mt_len, &mt_queued);

                fsm->last_sbdix.mo_status = (uint8_t)mo_st;
                fsm->last_sbdix.momsn = (uint16_t)momsn;
                fsm->last_sbdix.mt_status = (uint8_t)mt_st;
                fsm->last_sbdix.mtmsn = (uint16_t)mtmsn;
                fsm->last_sbdix.mt_len = (uint16_t)mt_len;
                fsm->last_sbdix.mt_queued = (uint16_t)mt_queued;

                if (mo_st <= 4) {
                    /* Successfully transmitted to Iridium satellite constellation! */
                    fsm->state = IRIDIUM_STATE_CLEAR_BUFFER;
                    fsm->state_entry_time_ms = now_ms;
                    fsm->timeout_duration_ms = 5000U;
                    send_cmd(fsm, "AT+SBDD0\r\n");
                } else {
                    fsm->last_error = IRIDIUM_ERR_GATEWAY_TIMEOUT;
                    fsm->state = IRIDIUM_STATE_ERROR;
                    fsm->session_active = false;
                    if (fsm->ops.set_power_enable) fsm->ops.set_power_enable(false);
                }
            }
            break;
        }

        case IRIDIUM_STATE_CLEAR_BUFFER:
            if (strstr(fsm->rx_buf, "OK") != NULL) {
                /* Power down modem immediately to preserve battery */
                if (fsm->ops.set_power_enable) {
                    fsm->ops.set_power_enable(false);
                }
                fsm->state = IRIDIUM_STATE_SUCCESS;
                fsm->session_active = false;
            }
            break;

        case IRIDIUM_STATE_SUCCESS:
        case IRIDIUM_STATE_ERROR:
            /* Completed states */
            break;
    }
}

bool iridium_fsm_is_done(const iridium_fsm_t *fsm, bool *out_success) {
    if (!fsm) return false;
    if (fsm->state == IRIDIUM_STATE_SUCCESS) {
        if (out_success) *out_success = true;
        return true;
    }
    if (fsm->state == IRIDIUM_STATE_ERROR) {
        if (out_success) *out_success = false;
        return true;
    }
    return false;
}

/**
 * @file iridium_at_fsm.h
 * @brief RockBLOCK 9603 (Iridium 9603 SBD) Asynchronous Non-Blocking AT Command FSM.
 *
 * Implements complete Iridium Short Burst Data (SBD) protocol state machine:
 *  - Power-gating control via Si2301 P-MOSFET (0.0 uA sleep current).
 *  - Automated baud negotiation & echo suppression (ATE0).
 *  - Satellite signal quality check (AT+CSQ: 0..5 bars).
 *  - Binary SBD buffer packaging (AT+SBDWB) with 16-bit summation checksum.
 *  - Non-blocking network handshake (AT+SBDIX) with 60-second satellite timeout.
 *  - Buffer purge (AT+SBDD0) and clean power-down.
 *  - Zero dynamic heap allocation.
 */

#ifndef IRIDIUM_AT_FSM_H
#define IRIDIUM_AT_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRIDIUM_RX_BUF_SIZE     (256U)
#define IRIDIUM_MAX_SBD_BYTES   (340U) /* Iridium 9603 max MO message is 340 bytes */

/* FSM States */
typedef enum {
    IRIDIUM_STATE_POWERED_OFF = 0,
    IRIDIUM_STATE_POWERING_ON,
    IRIDIUM_STATE_PING_AT,
    IRIDIUM_STATE_DISABLE_ECHO,
    IRIDIUM_STATE_CHECK_SIGNAL,
    IRIDIUM_STATE_WRITE_SBD_HDR,
    IRIDIUM_STATE_WRITE_SBD_DATA,
    IRIDIUM_STATE_SEND_SBDIX,
    IRIDIUM_STATE_CLEAR_BUFFER,
    IRIDIUM_STATE_SUCCESS,
    IRIDIUM_STATE_ERROR
} iridium_state_t;

/* Failure Error Codes */
typedef enum {
    IRIDIUM_ERR_NONE = 0,
    IRIDIUM_ERR_NO_MODEM_RESPONSE,
    IRIDIUM_ERR_NO_SATELLITE_SIGNAL,
    IRIDIUM_ERR_BUFFER_WRITE_FAILED,
    IRIDIUM_ERR_GATEWAY_TIMEOUT,
    IRIDIUM_ERR_REGISTRATION_DENIED
} iridium_error_t;

/**
 * @brief Parsed +SBDIX response fields.
 */
typedef struct {
    uint8_t  mo_status;   /**< 0..2 = Success, 32 = No network service */
    uint16_t momsn;       /**< Mobile Originated Message Sequence Number */
    uint8_t  mt_status;   /**< Mobile Terminated status */
    uint16_t mtmsn;       /**< MT sequence number */
    uint16_t mt_len;      /**< MT payload length */
    uint16_t mt_queued;   /**< MT messages waiting at gateway */
} iridium_sbdix_result_t;

/**
 * @brief UART callbacks for hardware transmission & power pin gating.
 */
typedef struct {
    void (*set_power_enable)(bool on);      /**< Si2301 P-MOSFET Gate drive */
    void (*uart_write)(const uint8_t *data, size_t len);
} iridium_hw_ops_t;

/**
 * @brief RockBLOCK 9603 FSM Context.
 */
typedef struct {
    iridium_hw_ops_t       ops;
    iridium_state_t        state;
    iridium_error_t        last_error;
    iridium_sbdix_result_t last_sbdix;

    /* Outgoing Binary Message Buffer */
    uint8_t  tx_payload[IRIDIUM_MAX_SBD_BYTES];
    uint16_t tx_payload_len;

    /* Incoming Serial Byte Receiver Buffer */
    char     rx_buf[IRIDIUM_RX_BUF_SIZE];
    uint16_t rx_idx;

    /* Timing & State Deadlines */
    uint32_t state_entry_time_ms;
    uint32_t timeout_duration_ms;
    uint8_t  csq_bars;           /**< 0..5 satellite signal level */
    uint8_t  csq_retries;
    bool     session_active;
} iridium_fsm_t;

/**
 * @brief Initialize RockBLOCK Iridium state machine.
 */
void iridium_fsm_init(iridium_fsm_t *fsm, const iridium_hw_ops_t *ops);

/**
 * @brief Queue emergency or tactical SBD message for transmission.
 */
bool iridium_fsm_queue_message(iridium_fsm_t *fsm, const uint8_t *data, uint16_t len);

/**
 * @brief Feed incoming serial bytes received from 9603 USART (non-blocking).
 */
void iridium_fsm_feed_rx(iridium_fsm_t *fsm, const char *data, size_t len);

/**
 * @brief Periodic FSM step (called in main loop or 100 Hz timer).
 * @param now_ms System time tick in milliseconds.
 */
void iridium_fsm_step(iridium_fsm_t *fsm, uint32_t now_ms);

/**
 * @brief Check if satellite transmission session is complete.
 */
bool iridium_fsm_is_done(const iridium_fsm_t *fsm, bool *out_success);

#ifdef __cplusplus
}
#endif

#endif /* IRIDIUM_AT_FSM_H */

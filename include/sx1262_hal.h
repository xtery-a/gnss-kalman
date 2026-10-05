/**
 * @file sx1262_hal.h
 * @brief Semtech SX1262 (1W / +30 dBm EBYTE E22-900T30D) SPI Hardware Abstraction Layer.
 *
 * Implements complete Semtech SX1261/SX1262 SPI command protocol:
 *  - Radio modes: Sleep, Standby, FS, TX, RX.
 *  - Packet types: LoRa & GFSK.
 *  - RF Frequency configuration (32-bit register resolution for 868 / 915 MHz).
 *  - PA Config & TX Params (Power, RampTime for high-power front-end).
 *  - Modulation Params (Spreading Factor SF7-SF12, Bandwidth 125/250/500 kHz, Coding Rate 4/5-4/8).
 *  - Packet Params (Preamble length, explicit header, CRC on/off, IQ inversion).
 *  - Non-blocking FIFO Buffer R/W via SPI.
 *  - IRQ Mask handling (TxDone, RxDone, Timeout, CrcErr).
 */

#ifndef SX1262_HAL_H
#define SX1262_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SX1262 SPI Opcodes (Semtech DS.SX1261-2.W.APP Rev 2.1) */
#define SX126X_CMD_SET_SLEEP                  (0x84U)
#define SX126X_CMD_SET_STANDBY                (0x80U)
#define SX126X_CMD_SET_FS                     (0xC1U)
#define SX126X_CMD_SET_TX                     (0x83U)
#define SX126X_CMD_SET_RX                     (0x82U)
#define SX126X_CMD_SET_PACKET_TYPE            (0x8AU)
#define SX126X_CMD_GET_PACKET_TYPE            (0x11U)
#define SX126X_CMD_SET_RF_FREQUENCY           (0x86U)
#define SX126X_CMD_SET_PA_CONFIG              (0x95U)
#define SX126X_CMD_SET_TX_PARAMS              (0x8EU)
#define SX126X_CMD_SET_BUFFER_BASE_ADDRESS    (0x8FU)
#define SX126X_CMD_SET_MODULATION_PARAMS      (0x8BU)
#define SX126X_CMD_SET_PACKET_PARAMS          (0x8CU)
#define SX126X_CMD_SET_DIO_IRQ_PARAMS         (0x08U)
#define SX126X_CMD_GET_IRQ_STATUS             (0x12U)
#define SX126X_CMD_CLEAR_IRQ_STATUS           (0x02U)
#define SX126X_CMD_GET_STATUS                 (0xC0U)
#define SX126X_CMD_WRITE_BUFFER               (0x0EU)
#define SX126X_CMD_READ_BUFFER                (0x1EU)

/* Packet Types */
#define SX126X_PKT_TYPE_GFSK                  (0x00U)
#define SX126X_PKT_TYPE_LORA                  (0x01U)

/* Spreading Factors */
#define SX126X_LORA_SF7                       (0x07U)
#define SX126X_LORA_SF8                       (0x08U)
#define SX126X_LORA_SF9                       (0x09U)
#define SX126X_LORA_SF10                      (0x0AU)
#define SX126X_LORA_SF11                      (0x0BU)
#define SX126X_LORA_SF12                      (0x0CU)

/* Bandwidths */
#define SX126X_LORA_BW_125                    (0x04U) /* 125 kHz */
#define SX126X_LORA_BW_250                    (0x05U) /* 250 kHz */
#define SX126X_LORA_BW_500                    (0x06U) /* 500 kHz */

/* Coding Rates */
#define SX126X_LORA_CR_4_5                    (0x01U)
#define SX126X_LORA_CR_4_6                    (0x02U)
#define SX126X_LORA_CR_4_7                    (0x03U)
#define SX126X_LORA_CR_4_8                    (0x04U)

/* IRQ Masks */
#define SX126X_IRQ_TX_DONE                    (0x0001U)
#define SX126X_IRQ_RX_DONE                    (0x0002U)
#define SX126X_IRQ_PREAMBLE_DETECTED          (0x0004U)
#define SX126X_IRQ_SYNC_WORD_VALID            (0x0008U)
#define SX126X_IRQ_HEADER_VALID               (0x0010U)
#define SX126X_IRQ_HEADER_ERR                 (0x0020U)
#define SX126X_IRQ_CRC_ERR                    (0x0040U)
#define SX126X_IRQ_CAD_DONE                   (0x0080U)
#define SX126X_IRQ_CAD_DETECTED               (0x0100U)
#define SX126X_IRQ_TIMEOUT                    (0x0200U)

/**
 * @brief Low-level SPI bus callback interface.
 */
typedef struct {
    void (*nss_select)(bool active);
    void (*spi_transfer)(const uint8_t *tx, uint8_t *rx, size_t len);
    void (*delay_ms)(uint32_t ms);
    bool (*is_busy)(void);
} sx1262_bus_ops_t;

/**
 * @brief SX1262 Driver Handle.
 */
typedef struct {
    sx1262_bus_ops_t ops;
    uint32_t         rf_freq_hz;
    int8_t           tx_power_dbm;
    uint8_t          sf;
    uint8_t          bw;
    uint8_t          cr;
    uint8_t          preamble_len;
    bool             is_initialized;
    uint16_t         last_irq_status;
    uint32_t         tx_packet_count;
    uint32_t         rx_packet_count;
    uint32_t         crc_errors;
} sx1262_handle_t;

/**
 * @brief Initialize SX1262 radio controller with SPI bus interface.
 */
bool sx1262_init(sx1262_handle_t *dev, const sx1262_bus_ops_t *ops);

/**
 * @brief Configure RF center frequency in Hertz (e.g. 868000000).
 */
bool sx1262_set_frequency(sx1262_handle_t *dev, uint32_t freq_hz);

/**
 * @brief Configure LoRa modulation parameters.
 */
bool sx1262_set_lora_modulation(sx1262_handle_t *dev,
                                uint8_t sf,
                                uint8_t bw,
                                uint8_t cr,
                                bool ldro_enable);

/**
 * @brief Configure RF transmit power in dBm (up to +22 dBm; external FEM gives +30 dBm).
 */
bool sx1262_set_tx_power(sx1262_handle_t *dev, int8_t power_dbm);

/**
 * @brief Transmit raw payload via LoRa frame.
 */
bool sx1262_send_packet(sx1262_handle_t *dev, const uint8_t *payload, uint8_t len);

/**
 * @brief Check and clear interrupt flags.
 */
uint16_t sx1262_poll_irq(sx1262_handle_t *dev);

/**
 * @brief Put radio into low-power standby mode.
 */
bool sx1262_set_standby(sx1262_handle_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* SX1262_HAL_H */

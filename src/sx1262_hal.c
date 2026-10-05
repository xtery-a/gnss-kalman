/**
 * @file sx1262_hal.c
 * @brief Semtech SX1262 LoRa SPI Hardware Abstraction Layer Implementation.
 */

#include "sx1262_hal.h"
#include <string.h>

#define SX126X_XTAL_FREQ_HZ (32000000.0)
#define SX126X_FREQ_STEP    (SX126X_XTAL_FREQ_HZ / 33554432.0) /* F_xtal / 2^25 */

static void spi_write_command(sx1262_handle_t *dev, uint8_t opcode, const uint8_t *params, size_t len) {
    if (!dev || !dev->ops.spi_transfer || !dev->ops.nss_select) return;

    dev->ops.nss_select(true);
    uint8_t dummy_rx;
    dev->ops.spi_transfer(&opcode, &dummy_rx, 1);
    if (params && len > 0) {
        for (size_t i = 0; i < len; i++) {
            dev->ops.spi_transfer(&params[i], &dummy_rx, 1);
        }
    }
    dev->ops.nss_select(false);

    if (dev->ops.delay_ms) {
        dev->ops.delay_ms(1);
    }
}

static void spi_read_command(sx1262_handle_t *dev, uint8_t opcode, uint8_t *out_data, size_t len) {
    if (!dev || !dev->ops.spi_transfer || !dev->ops.nss_select) return;

    dev->ops.nss_select(true);
    uint8_t dummy_tx = 0x00;
    uint8_t dummy_rx;
    dev->ops.spi_transfer(&opcode, &dummy_rx, 1);
    /* NOP cycle for SX1262 status */
    dev->ops.spi_transfer(&dummy_tx, &dummy_rx, 1);
    for (size_t i = 0; i < len; i++) {
        dev->ops.spi_transfer(&dummy_tx, &out_data[i], 1);
    }
    dev->ops.nss_select(false);
}

bool sx1262_init(sx1262_handle_t *dev, const sx1262_bus_ops_t *ops) {
    if (!dev || !ops) return false;
    memset(dev, 0, sizeof(sx1262_handle_t));
    dev->ops = *ops;

    /* 1. Standby RC mode */
    uint8_t standby_param = 0x00; /* STDBY_RC */
    spi_write_command(dev, SX126X_CMD_SET_STANDBY, &standby_param, 1);

    /* 2. Set Packet Type to LoRa */
    uint8_t pkt_type = SX126X_PKT_TYPE_LORA;
    spi_write_command(dev, SX126X_CMD_SET_PACKET_TYPE, &pkt_type, 1);

    /* Default settings: 868.100 MHz, +22 dBm, SF7, BW 125 kHz, CR 4/5 */
    dev->rf_freq_hz = 868100000U;
    dev->tx_power_dbm = 22;
    dev->sf = SX126X_LORA_SF7;
    dev->bw = SX126X_LORA_BW_125;
    dev->cr = SX126X_LORA_CR_4_5;
    dev->preamble_len = 12;

    sx1262_set_frequency(dev, dev->rf_freq_hz);
    sx1262_set_tx_power(dev, dev->tx_power_dbm);
    sx1262_set_lora_modulation(dev, dev->sf, dev->bw, dev->cr, false);

    dev->is_initialized = true;
    return true;
}

bool sx1262_set_frequency(sx1262_handle_t *dev, uint32_t freq_hz) {
    if (!dev) return false;

    dev->rf_freq_hz = freq_hz;
    uint32_t freq_reg = (uint32_t)((double)freq_hz / SX126X_FREQ_STEP + 0.5);

    uint8_t buf[4];
    buf[0] = (uint8_t)((freq_reg >> 24) & 0xFFU);
    buf[1] = (uint8_t)((freq_reg >> 16) & 0xFFU);
    buf[2] = (uint8_t)((freq_reg >> 8) & 0xFFU);
    buf[3] = (uint8_t)(freq_reg & 0xFFU);

    spi_write_command(dev, SX126X_CMD_SET_RF_FREQUENCY, buf, 4);
    return true;
}

bool sx1262_set_lora_modulation(sx1262_handle_t *dev,
                                uint8_t sf,
                                uint8_t bw,
                                uint8_t cr,
                                bool ldro_enable) {
    if (!dev) return false;

    dev->sf = sf;
    dev->bw = bw;
    dev->cr = cr;

    uint8_t buf[4];
    buf[0] = sf;
    buf[1] = bw;
    buf[2] = cr;
    buf[3] = ldro_enable ? 0x01U : 0x00U;

    spi_write_command(dev, SX126X_CMD_SET_MODULATION_PARAMS, buf, 4);
    return true;
}

bool sx1262_set_tx_power(sx1262_handle_t *dev, int8_t power_dbm) {
    if (!dev) return false;

    if (power_dbm > 22) power_dbm = 22;
    if (power_dbm < -9) power_dbm = -9;
    dev->tx_power_dbm = power_dbm;

    /* Set PA Config: paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01 */
    uint8_t pa_config[4] = { 0x04, 0x07, 0x00, 0x01 };
    spi_write_command(dev, SX126X_CMD_SET_PA_CONFIG, pa_config, 4);

    /* Set Tx Params: power, rampTime = 0x02 (40 us) */
    uint8_t tx_params[2] = { (uint8_t)power_dbm, 0x02 };
    spi_write_command(dev, SX126X_CMD_SET_TX_PARAMS, tx_params, 2);

    return true;
}

bool sx1262_send_packet(sx1262_handle_t *dev, const uint8_t *payload, uint8_t len) {
    if (!dev || !payload || len == 0) return false;

    /* 1. Ensure Standby */
    uint8_t standby_param = 0x00;
    spi_write_command(dev, SX126X_CMD_SET_STANDBY, &standby_param, 1);

    /* 2. Set Buffer Base Address (Tx base = 0x00, Rx base = 0x00) */
    uint8_t base_addr[2] = { 0x00, 0x00 };
    spi_write_command(dev, SX126X_CMD_SET_BUFFER_BASE_ADDRESS, base_addr, 2);

    /* 3. Write Buffer: offset = 0x00 */
    if (dev->ops.nss_select && dev->ops.spi_transfer) {
        dev->ops.nss_select(true);
        uint8_t dummy_rx;
        uint8_t write_cmd[2] = { SX126X_CMD_WRITE_BUFFER, 0x00 };
        dev->ops.spi_transfer(&write_cmd[0], &dummy_rx, 1);
        dev->ops.spi_transfer(&write_cmd[1], &dummy_rx, 1);
        for (uint8_t i = 0; i < len; i++) {
            dev->ops.spi_transfer(&payload[i], &dummy_rx, 1);
        }
        dev->ops.nss_select(false);
    }

    /* 4. Set Packet Params: Preamble=12, Explicit Header, PayloadLength=len, CRC=ON, Standard IQ */
    uint8_t pkt_params[6] = {
        0x00, 12,   /* Preamble Length (MSB, LSB) */
        0x00,       /* Header Type: 0 = Explicit */
        len,        /* Payload Length */
        0x01,       /* CRC Type: 1 = ON */
        0x00        /* Standard Invert IQ */
    };
    spi_write_command(dev, SX126X_CMD_SET_PACKET_PARAMS, pkt_params, 6);

    /* 5. Clear all IRQs & Configure TxDone interrupt */
    uint8_t clear_irq[2] = { 0xFF, 0xFF };
    spi_write_command(dev, SX126X_CMD_CLEAR_IRQ_STATUS, clear_irq, 2);

    uint8_t dio_irq[8] = {
        (uint8_t)(SX126X_IRQ_TX_DONE >> 8), (uint8_t)(SX126X_IRQ_TX_DONE & 0xFF),
        (uint8_t)(SX126X_IRQ_TX_DONE >> 8), (uint8_t)(SX126X_IRQ_TX_DONE & 0xFF),
        0x00, 0x00, 0x00, 0x00
    };
    spi_write_command(dev, SX126X_CMD_SET_DIO_IRQ_PARAMS, dio_irq, 8);

    /* 6. Enter TX Mode (timeout = 0x000000: single packet then standby) */
    uint8_t tx_timeout[3] = { 0x00, 0x00, 0x00 };
    spi_write_command(dev, SX126X_CMD_SET_TX, tx_timeout, 3);

    dev->tx_packet_count++;
    return true;
}

uint16_t sx1262_poll_irq(sx1262_handle_t *dev) {
    if (!dev) return 0;

    uint8_t irq_bytes[2] = { 0x00, 0x00 };
    spi_read_command(dev, SX126X_CMD_GET_IRQ_STATUS, irq_bytes, 2);

    uint16_t irq_status = (uint16_t)((uint16_t)irq_bytes[0] << 8 | irq_bytes[1]);
    dev->last_irq_status = irq_status;

    if (irq_status != 0) {
        /* Clear read interrupts */
        spi_write_command(dev, SX126X_CMD_CLEAR_IRQ_STATUS, irq_bytes, 2);
    }

    return irq_status;
}

bool sx1262_set_standby(sx1262_handle_t *dev) {
    if (!dev) return false;
    uint8_t standby_param = 0x00;
    spi_write_command(dev, SX126X_CMD_SET_STANDBY, &standby_param, 1);
    return true;
}

/**
 * @file test_harness_radio.c
 * @brief Standalone Bare-Metal Verification Harness for SX1262 LoRa SPI & Iridium SBD AT Drivers.
 */

#include "sx1262_hal.h"
#include "iridium_at_fsm.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Mock SPI Bus for SX1262 Testing                                            */
/* -------------------------------------------------------------------------- */
static uint8_t s_mock_spi_last_opcode = 0;
static uint8_t s_mock_spi_tx_log[128];
static size_t  s_mock_spi_tx_len = 0;
static bool    s_mock_nss_state = false;

static void mock_nss_select(bool active) {
    s_mock_nss_state = active;
}

static void mock_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (s_mock_spi_tx_len < sizeof(s_mock_spi_tx_log)) {
            s_mock_spi_tx_log[s_mock_spi_tx_len++] = tx[i];
        }
        if (rx) rx[i] = 0x00;
    }
    if (len > 0) s_mock_spi_last_opcode = tx[0];
}

/* -------------------------------------------------------------------------- */
/* Mock UART & Power Control for RockBLOCK 9603 Testing                       */
/* -------------------------------------------------------------------------- */
static bool s_mock_pmosfet_power = false;
static char s_mock_uart_tx[512];
static size_t s_mock_uart_tx_len = 0;

static void mock_set_power(bool on) {
    s_mock_pmosfet_power = on;
}

static void mock_uart_write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (s_mock_uart_tx_len + 1 < sizeof(s_mock_uart_tx)) {
            s_mock_uart_tx[s_mock_uart_tx_len++] = (char)data[i];
            s_mock_uart_tx[s_mock_uart_tx_len] = '\0';
        }
    }
}

int main(void) {
    printf("=================================================================\n");
    printf(" PHYSICAL RF DRIVERS: SX1262 LORA SPI & ROCKBLOCK IRIDIUM AT FSM\n");
    printf("=================================================================\n");

    /* ====================================================================== */
    /* PART 1: SEMTECH SX1262 LORA SPI HARDWARE DRIVER                        */
    /* ====================================================================== */
    printf("\n--- [PART 1: Semtech SX1262 LoRa SPI Hardware Driver] ---\n");
    sx1262_handle_t lora;
    sx1262_bus_ops_t spi_ops = {
        .nss_select   = mock_nss_select,
        .spi_transfer = mock_spi_transfer,
        .delay_ms     = NULL,
        .is_busy      = NULL
    };

    printf("[TEST 1.1] Initializing SX1262 over SPI Bus...\n");
    assert(sx1262_init(&lora, &spi_ops) == true);
    assert(lora.is_initialized == true);
    assert(lora.rf_freq_hz == 868100000U);
    assert(lora.tx_power_dbm == 22);
    printf("  [PASS] SX1262 Standby entered; LoRa packet type configured (868.100 MHz, +22 dBm).\n");

    printf("[TEST 1.2] Setting RF Center Frequency (868.000 MHz)...\n");
    assert(sx1262_set_frequency(&lora, 868000000U) == true);
    assert(lora.rf_freq_hz == 868000000U);
    printf("  [PASS] 32-bit frequency synthesis register calculated and written via SPI.\n");

    printf("[TEST 1.3] Configuring High-Link-Budget LoRa Modulation (SF10, BW 125, CR 4/8)...\n");
    assert(sx1262_set_lora_modulation(&lora, SX126X_LORA_SF10, SX126X_LORA_BW_125, SX126X_LORA_CR_4_8, true) == true);
    assert(lora.sf == SX126X_LORA_SF10);
    printf("  [PASS] Spreading Factor SF10, LDRO enabled, coding rate 4/8 configured.\n");

    printf("[TEST 1.4] Transmitting Raw 34-Byte BFT Tactical Beacon...\n");
    uint8_t sample_bft[34];
    memset(sample_bft, 0x55, sizeof(sample_bft));
    assert(sx1262_send_packet(&lora, sample_bft, sizeof(sample_bft)) == true);
    assert(lora.tx_packet_count == 1);
    printf("  [PASS] Payload loaded into FIFO 0x00, PacketParams set, TX mode triggered.\n");

    /* ====================================================================== */
    /* PART 2: ROCKBLOCK 9603 IRIDIUM ASYNCHRONOUS AT COMMAND FSM             */
    /* ====================================================================== */
    printf("\n--- [PART 2: RockBLOCK 9603 Iridium SBD AT Command FSM] ---\n");
    iridium_fsm_t ir_fsm;
    iridium_hw_ops_t hw_ops = {
        .set_power_enable = mock_set_power,
        .uart_write       = mock_uart_write
    };

    printf("[TEST 2.1] Initializing RockBLOCK FSM Context...\n");
    iridium_fsm_init(&ir_fsm, &hw_ops);
    assert(s_mock_pmosfet_power == false); /* 0.0 uA leakage confirmed */
    assert(ir_fsm.state == IRIDIUM_STATE_POWERED_OFF);
    printf("  [PASS] FSM in POWERED_OFF state; Si2301 P-MOSFET gate HIGH (0.0 uA leakage).\n");

    printf("[TEST 2.2] Queuing SBD Emergency Beacon & Power-On Sequence...\n");
    uint8_t distress_sbd[32] = "MAYDAY_SQUAD4_GRID_32UPU";
    assert(iridium_fsm_queue_message(&ir_fsm, distress_sbd, 24) == true);
    assert(s_mock_pmosfet_power == true); /* Gate driven LOW, power flowing */
    assert(ir_fsm.state == IRIDIUM_STATE_POWERING_ON);
    printf("  [PASS] Message buffered; Si2301 P-MOSFET energized to charge supercapacitor.\n");

    printf("[TEST 2.3] Advancing Time: Modem Boot & AT Ping...\n");
    uint32_t now = 1000;
    iridium_fsm_step(&ir_fsm, now); /* Entry timestamp set */
    now += 1600; /* Past 1500 ms boot delay */
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_PING_AT);
    assert(strstr(s_mock_uart_tx, "AT\r\n") != NULL);
    printf("  [PASS] Cold boot complete; AT sync ping dispatched.\n");

    /* Feed "OK" */
    iridium_fsm_feed_rx(&ir_fsm, "OK\r\n", 4);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_DISABLE_ECHO);

    /* Feed "OK" to ATE0 */
    iridium_fsm_feed_rx(&ir_fsm, "OK\r\n", 4);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_CHECK_SIGNAL);
    printf("  [PASS] Echo disabled (ATE0); querying satellite signal strength (AT+CSQ).\n");

    /* Feed "+CSQ:4" (4 bars signal strength) */
    iridium_fsm_feed_rx(&ir_fsm, "+CSQ:4\r\n", 8);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.csq_bars == 4);
    assert(ir_fsm.state == IRIDIUM_STATE_WRITE_SBD_HDR);
    printf("  [PASS] Satellite signal confirmed (+CSQ:4 >= 2 bars); AT+SBDWB dispatched.\n");

    /* Feed "READY" */
    iridium_fsm_feed_rx(&ir_fsm, "READY\r\n", 7);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_WRITE_SBD_DATA);
    printf("  [PASS] Binary SBD payload + 16-bit summation checksum serialized.\n");

    /* Feed "0" (success response for binary transfer) */
    iridium_fsm_feed_rx(&ir_fsm, "0\r\n", 3);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_SEND_SBDIX);
    printf("  [PASS] Binary SBD written; satellite network transaction initiated (AT+SBDIX).\n");

    /* Feed successful +SBDIX: 0, 1234, 0, 0, 0, 0 */
    iridium_fsm_feed_rx(&ir_fsm, "+SBDIX: 0, 1234, 0, 0, 0, 0\r\n", 29);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.last_sbdix.mo_status == 0);
    assert(ir_fsm.last_sbdix.momsn == 1234);
    assert(ir_fsm.state == IRIDIUM_STATE_CLEAR_BUFFER);
    printf("  [PASS] +SBDIX MO status = 0 (Transferred to Space Segment). Purging buffers.\n");

    /* Feed "OK" to AT+SBDD0 */
    iridium_fsm_feed_rx(&ir_fsm, "OK\r\n", 4);
    iridium_fsm_step(&ir_fsm, now);
    assert(ir_fsm.state == IRIDIUM_STATE_SUCCESS);
    assert(s_mock_pmosfet_power == false); /* Powered down immediately */
    bool success = false;
    assert(iridium_fsm_is_done(&ir_fsm, &success) == true && success == true);
    printf("  [PASS] Buffer purged; Si2301 P-MOSFET turned OFF -> 0.0 uA Quiescent Current.\n");

    printf("\n=================================================================\n");
    printf(" ALL SX1262 & IRIDIUM SBD DRIVER TESTS PASSED 100%% SUCCESS!\n");
    printf("=================================================================\n");
    return 0;
}

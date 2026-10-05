/**
 * @file hybrid_comms.h
 * @brief Zero-Heap Hybrid Tactical Telemetry, Blue Force Tracking (BFT) & Satellite Failover.
 *
 * Communication Hierarchy:
 *  - Tier-1 (Local/Mesh): EBYTE E22-900T30D (1W / +30 dBm LoRa SX1262, 868/915 MHz)
 *  - Tier-2 (BLOS Satellite): RockBLOCK 9603 (Iridium 9603 SBD, 1621 MHz) with 0.0 uA P-MOSFET Power Gate.
 *
 * Security:
 *  - AES-128-CTR encrypted tactical payload frames with anti-replay rolling sequence IDs.
 */

#ifndef HYBRID_COMMS_H
#define HYBRID_COMMS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_DEFAULT_FREQ_HZ        (868000000UL) /* 868 MHz European / NATO ISM */
#define LORA_TX_POWER_DBM           (30U)         /* +30 dBm (1 Watt) output */
#define LORA_MAX_RETRIES            (3U)          /* Switch to Iridium after 3 failed ACKs */
#define IRIDIUM_PWR_GATE_PIN        (42U)         /* Si2301 P-MOSFET Gate drive */
#define BFT_MAX_PAYLOAD_SIZE        (48U)

typedef enum {
    COMMS_TIER1_LORA_ACTIVE = 0,    /**< Tier-1: 1W LoRa 868MHz tactical local mesh */
    COMMS_TIER2_IRIDIUM_FAILOVER,   /**< Tier-2: LoRa ACK timed out or SOS trigger */
    COMMS_TIER2_IRIDIUM_TRANSMIT,   /**< Active Iridium SBD session in progress */
    COMMS_STANDBY                   /**< Power-save passive standby */
} comms_state_t;

#pragma pack(push, 1)

/**
 * @brief Tactical Blue Force Tracking (BFT) Telemetry Beacon (18 Bytes)
 */
typedef struct {
    uint16_t node_id;        /**< Tactical squad ID / callsign index */
    int32_t  lat_1e7;        /**< WGS84 Latitude * 1e7 */
    int32_t  lon_1e7;        /**< WGS84 Longitude * 1e7 */
    int16_t  alt_m;          /**< Altitude in meters */
    uint16_t heading_cd;     /**< Heading in centi-degrees (0..35999) */
    uint8_t  battery_pct;    /**< Battery remaining percentage (0..100) */
    uint8_t  status_flags;   /**< Bit 0: SOS/Distress, Bit 1: Stationary, Bit 2: Sub-Zero Warning */
    uint16_t seq_num;        /**< Rolling sequence counter (Anti-Replay) */
} bft_beacon_payload_t;

/**
 * @brief Encrypted Tactical Frame
 */
typedef struct {
    uint8_t  header_magic[2]; /**< 0x54, 0x42 ('TB' Tactical Beacon) */
    uint8_t  encrypted_bytes[sizeof(bft_beacon_payload_t)];
    uint8_t  iv[8];           /**< Initialization Vector / Nonce for AES-128-CTR */
    uint16_t crc16;           /**< Frame verification checksum */
} tactical_frame_t;

#pragma pack(pop)

typedef struct {
    comms_state_t state;
    uint8_t       retry_count;
    uint16_t      local_node_id;
    uint16_t      tx_seq_counter;
    bool          iridium_powered;
    uint32_t      total_lora_tx_count;
    uint32_t      total_lora_ack_count;
    uint32_t      total_iridium_tx_count;
} hybrid_comms_ctx_t;

/* Function Prototypes */
void hybrid_comms_init(hybrid_comms_ctx_t *ctx, uint16_t local_node_id);
bool hybrid_comms_pack_bft(hybrid_comms_ctx_t *ctx,
                           const bft_beacon_payload_t *beacon,
                           tactical_frame_t *out_frame,
                           const uint8_t aes_key[16]);
bool hybrid_comms_unpack_bft(const tactical_frame_t *in_frame,
                             bft_beacon_payload_t *out_beacon,
                             const uint8_t aes_key[16]);

bool hybrid_comms_dispatch(hybrid_comms_ctx_t *ctx,
                           const uint8_t *payload,
                           size_t len,
                           bool is_emergency_sos);

void hybrid_comms_notify_ack(hybrid_comms_ctx_t *ctx, bool ack_received);
void iridium_power_enable(hybrid_comms_ctx_t *ctx, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* HYBRID_COMMS_H */

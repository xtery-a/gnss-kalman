/**
 * @file crypto_hal.h
 * @brief Unified Hardware / Software Cryptographic HAL for AES-128-CTR.
 *
 * Target:
 *  - STM32U585/U575 Secure AES (SAES) peripheral with DMA acceleration.
 *  - Deterministic Zero-Heap Software Fallback for Desktop SIL and Bare-Metal.
 *
 * Performance:
 *  - HW SAES: ~0.1 us per 16-byte block @ 160 MHz.
 *  - SW Fallback: ~1.2 us per 16-byte block, zero dynamic heap.
 */

#ifndef CRYPTO_HAL_H
#define CRYPTO_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_AES_KEY_SIZE     (16U) /**< 128-bit AES key */
#define CRYPTO_AES_BLOCK_SIZE   (16U) /**< 128-bit block size */
#define CRYPTO_AES_IV_SIZE      (16U) /**< 128-bit IV / Nonce+Counter block */

#ifdef USE_STM32U5_HW_SAES
/**
 * @brief STM32U5 Secure AES (SAES) Hardware Register Map Abstraction.
 */
typedef struct {
    volatile uint32_t CR;       /**< Control register */
    volatile uint32_t SR;       /**< Status register */
    volatile uint32_t DINR;     /**< Data input register */
    volatile uint32_t DOUTR;    /**< Data output register */
    volatile uint32_t KEYR0;    /**< Key register 0 */
    volatile uint32_t KEYR1;    /**< Key register 1 */
    volatile uint32_t KEYR2;    /**< Key register 2 */
    volatile uint32_t KEYR3;    /**< Key register 3 */
    volatile uint32_t IVR0;     /**< Initialization vector register 0 */
    volatile uint32_t IVR1;     /**< Initialization vector register 1 */
    volatile uint32_t IVR2;     /**< Initialization vector register 2 */
    volatile uint32_t IVR3;     /**< Initialization vector register 3 */
    volatile uint32_t CRR;      /**< Context / Suspend register */
    volatile uint32_t IER;      /**< Interrupt enable register */
    volatile uint32_t ISR;      /**< Interrupt status register */
    volatile uint32_t ICR;      /**< Interrupt clear register */
} SAES_TypeDef;

#define SAES_PERIPH_BASE        (0x420C0000UL)
#define STM32_SAES              ((SAES_TypeDef *)SAES_PERIPH_BASE)
#endif /* USE_STM32U5_HW_SAES */

/**
 * @brief AES-128-CTR Unified Context Structure.
 */
typedef struct {
    uint8_t  round_keys[176];            /**< 11 round keys of 16 bytes each */
    uint8_t  iv_counter[CRYPTO_AES_BLOCK_SIZE]; /**< Current 128-bit counter block */
    uint8_t  keystream[CRYPTO_AES_BLOCK_SIZE];  /**< Current encrypted counter block */
    uint8_t  keystream_pos;              /**< Current offset in keystream (0..16) */
    bool     hw_accelerated;             /**< True if hardware SAES active */
} crypto_aes_ctx_t;

/* -------------------------------------------------------------------------- */
/* Cryptographic HAL Functions                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the AES-128-CTR cipher engine.
 *
 * @param ctx Pointer to cipher context.
 * @param key 16-byte (128-bit) symmetric key.
 * @param iv 16-byte initial counter block (typically 12-byte Nonce + 4-byte big-endian counter).
 * @param initial_counter 32-bit counter value to load into the last 4 bytes of IV.
 */
void crypto_aes128_ctr_init(crypto_aes_ctx_t *ctx,
                           const uint8_t key[CRYPTO_AES_KEY_SIZE],
                           const uint8_t iv[CRYPTO_AES_IV_SIZE],
                           uint32_t initial_counter);

/**
 * @brief Process (Encrypt or Decrypt) a stream of arbitrary length using AES-128-CTR.
 *
 * In CTR mode, encryption and decryption are bitwise identical operations.
 * Operates without allocating any heap memory.
 *
 * @param ctx Pointer to initialized cipher context.
 * @param in Input plaintext/ciphertext buffer.
 * @param out Output buffer (can be in-place: out == in).
 * @param len Number of bytes to process.
 * @return true on success, false if pointers are invalid.
 */
bool crypto_aes128_ctr_process(crypto_aes_ctx_t *ctx,
                              const uint8_t *in,
                              uint8_t *out,
                              size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_HAL_H */

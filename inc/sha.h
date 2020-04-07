/* parasoft-begin-suppress MISRA2012-RULE-3_1_b-2, "Do not embed // comment marker inside C-style comment", DRV-5199 */
/******************************************************************************
 * Copyright (C) 2019 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************
 *
 * sha.h
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#ifndef SHA_H
#define SHA_H

#include "cdn_stdint.h"

/* Size of SHA1 hash in bytes */
#define SHA1_HASH_SIZE_IN_BYTES 20U
/* Size of SHA256 hash in bytes */
#define SHA256_HASH_SIZE_IN_BYTES 32U
/* Size of (both SHA1 and SHA256) block in bytes */
#define SHA_BLOCK_SIZE_IN_BYTES 64U
/* Number of bytes reserved for 'message length' in final SHA block */
#define SHA_MESSAGE_LENGTH_SIZE_IN_BYTES 8U
/* Number of available bytes in finish message (8B is needed to store message length) */
#define SHA_FINISH_MESSAGE_DATA_SIZE_IN_BYTES 56U
/* Mask used to extraction of number of pending bytes from total processed */
#define SHA_PENDING_DATA_MASK 0x0000003FU
/* Base value of HMAC inner padding */
#define HMAC_BLOCK_SIZED_IPAD_VAL 0x36U
/* Base value of HMAC outer padding */
#define HMAC_BLOCK_SIZED_OPAD_VAL 0x5CU

typedef struct {
    uint32_t total[2];          /*!< number of bytes processed  */
    uint8_t buffer[64];   /*!< data block being processed */
    uint8_t ipad[64];     /*!< HMAC: inner padding        */
    uint8_t opad[64];     /*!< HMAC: outer padding        */
}
Sha256Context_t;

typedef struct {
    uint32_t total[2];          /*!< number of bytes processed  */
    uint8_t buffer[64];   /*!< data block being processed */
}
Sha1Context_t;

/**
 * Cleanup number of processed bytes in SHA-256 context setup
 * and start SHA256 module.
 * @param[in] ctx, pointer to SHA256 context
 */
void sha256_starts(Sha256Context_t *ctx);

/**
 * Initialize context by 0s and start SHA256 module
 * @param[in] ctx, pointer to SHA256 context
 */
void sha256_init(Sha256Context_t* ctx);

/**
 * Process SHA256 buffer
 * @param[in] ctx, pointer to SHA256 context
 * @param[in] input, data to be hashed
 * @param[in] inputLen, length of input buffer in bytes
 */
void sha256_update(Sha256Context_t *ctx, const uint8_t* input, uint32_t inputLen);

/**
 * Finish hashing by add padding (if required) and message length
 * @param[in] ctx, pointer to SHA256 context
 * @param[out] output, buffer with hashed data
 */
void sha256_finish(Sha256Context_t* ctx, uint8_t* output);

/**
 * Generate SHA256 hash for input data (initialize, update and finish)
 * @param[in] input, data to be hashed
 * @param[in] inputLen, length of input data in bytes
 * @param[out] output, buffer with hashed data
 */
void sha256(const uint8_t* input, uint32_t inputLen, uint8_t output[SHA256_HASH_SIZE_IN_BYTES]);

/* parasoft-begin-suppress METRICS-36-3, "Function is called from more than 5 functions, DRV-3823 */
/**
 * Generate HMAC (Hash Message Authentication Code) involving SHA256 hash function
 * @param[in] key, HMAC secret key
 * @param[in] keyLen, length of secret key in bytes
 * @param[in] inputBuffer, buffer with data to be authenticated
 * @param[in] inputLen, input data length in bytes
 * @para[out] hmacBuffer, buffer with HMAC
 */
void sha256_hmac(const uint8_t* key, uint32_t keyLen, const uint8_t* inputBuffer, uint32_t inputLen, uint8_t hmacBuffer[SHA256_HASH_SIZE_IN_BYTES]);
/* parasoft-end-suppress METRICS-36-3 */

/**
 * Generate SHA1 hash for input data (initialize, update and finish)
 * @param[in] input, data to be hashed
 * @param[in] inputLen, length of input data in bytes
 * @param[out] output, buffer with hashed data
 */
void sha1(const uint8_t *input, uint32_t inputLen, uint8_t output[SHA1_HASH_SIZE_IN_BYTES]);

#endif /* SHA_H */

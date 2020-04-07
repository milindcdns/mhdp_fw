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
 * sha.c
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#include "sha.h"
#include "utils.h"
#include "reg.h"

typedef enum {
    SHA_1,
    SHA_256,
} ShaAlg_t;

/**
 * Used to add number of processed data to context
 * @param[in] total, pointer to SHA256 context total processed bytes buffer
 * @param[in] addVal, number of processed bytes
 */
static inline void addBytes(uint32_t* total, uint32_t addVal)
{
    /* Update number of processed bytes */
    total[0] += addVal;

    /* Used to handle integer overflow */
    if (total[0] < addVal) {
        total[1]++;
    }
}

/**
 * Returns number of data in bytes stored in context buffer but not processed
 * @param[in] total, pointer to context buffer with number of processed bytes
 * @return number of bytes in buffer but not processed
 */
static inline uint8_t getNumberOfPendingDataInBytes(const uint32_t* total)
{
    /* Size of SHA256 block is 64B (512b). To store it is needed 7b ([6:0]), so it means
       that on bits [5:0] is stored number of pending data (packed into SHA256 buffer, but not processed) */
    return (uint8_t)total[0] & SHA_PENDING_DATA_MASK;
}

/**
 * Convert number of processed bytes in context to number of processed bits
 * @param[in] total, pointer to context buffer with number of processed bytes
 * @param[in] msgLenInBits, buffer with number of processed bits in big-endian order
 */
static inline void getMessageSizeInBits(const uint32_t* total, uint8_t* msgLenInBits)
{
    /* Maximum size of message in bytes is 2^61 - 1,
       so 3 MSB of msgLenInBytes are never used */

    /* numOfBits = numOfBytes * 8U, it same as shifting << 3 */
    uint32_t low = safe_shift32(LEFT, total[0], 3U);

    /* Get 3 MSB bit of msgLenInBytes[0] as LSB bits of high */
    uint32_t high = safe_shift32(RIGHT, total[0], 29U);

    /* Get data from msgLenInBytes[1] */
    high |= safe_shift32(LEFT, total[1], 3U);

    setBe32(high, msgLenInBits);
    setBe32(low, &msgLenInBits[4]);
}

/**
 * Calculate number of bytes needed to be completed from padding array
 * @param[in] pendingDataBytes, number of not processed data in context buffer
 * returns number of bytes needed to padding
 */
static inline uint8_t getNumberOfPaddingBytes(uint8_t pendingDataBytes)
{
    uint8_t paddingBytes;

    if (pendingDataBytes < SHA_FINISH_MESSAGE_DATA_SIZE_IN_BYTES) {
        /* Fulfill data part of finish buffer with padding */
        paddingBytes = SHA_FINISH_MESSAGE_DATA_SIZE_IN_BYTES - pendingDataBytes;
    } else {
        /* Not enough size for message length - fulfill buffer with padding and
           fulfill data part of finish buffer with padding */
        paddingBytes = (SHA_BLOCK_SIZE_IN_BYTES + SHA_FINISH_MESSAGE_DATA_SIZE_IN_BYTES) - pendingDataBytes;
    }

    return paddingBytes;
}

/**
 * Used to calculate inner and outre padding when HMAC is used
 * @param[in] K, pointer to secret key
 * @param[in] keyLen, length of secret key in bytes
 * @param[out] innerKeyPadding, pointer to inner padding buffer
 * @param[out] outerKeyPadding, pointer to outer padding buffer
 */
static inline void generateBlockSizedKeyPadding(const uint8_t* K, uint32_t keyLen, uint8_t* innerKeyPadding, uint8_t* outerKeyPadding)
{
    uint8_t i;

    for (i = 0U; i < SHA_BLOCK_SIZE_IN_BYTES; i++) {
        /* Assign base value to padding buffer */
        innerKeyPadding[i] = HMAC_BLOCK_SIZED_IPAD_VAL;
        outerKeyPadding[i] = HMAC_BLOCK_SIZED_OPAD_VAL;

        if (i < keyLen) {
            /* XOR each padding value with key if is long enough */
            innerKeyPadding[i] ^= K[i];
            outerKeyPadding[i] ^= K[i];
        }
    }
}

/**
 * Read output of SHA1 module and put it into buffer
 * @param[in] hash, buffer with hashed data
 */
static inline void get_sha256_hash(uint8_t hash[SHA256_HASH_SIZE_IN_BYTES])
{
    uint8_t i;

    /* Using macro makes impossible to use address increment.
       Array with values are used to avoid VOCF violation */
    uint32_t sha256_hash_regs[] = {
            RegRead(SHA_256_DATA_OUT_0),
            RegRead(SHA_256_DATA_OUT_1),
            RegRead(SHA_256_DATA_OUT_2),
            RegRead(SHA_256_DATA_OUT_3),
            RegRead(SHA_256_DATA_OUT_4),
            RegRead(SHA_256_DATA_OUT_5),
            RegRead(SHA_256_DATA_OUT_6),
            RegRead(SHA_256_DATA_OUT_7),
    };

    uint32_t numOfShaOutRegs = SHA256_HASH_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_UINT32T;

    for (i = 0U; i < (uint8_t)numOfShaOutRegs; i++) {
        setBe32(sha256_hash_regs[i], &hash[i * 4U]);
    }

    /* Reset SHA module */
    RegWrite(CRYPTO22_CONFIG, 0U);
}

/**
 * Read output of SHA1 module and put it into buffer
 * @param[in] hash, buffer with hashed data
 */
static inline void get_sha1_hash(uint8_t hash[SHA1_HASH_SIZE_IN_BYTES])
{
    uint8_t i;

    /* Using macro makes impossible to use address increment.
       Array with values are used to avoid VOCF violation */
    uint32_t sha1_hash_regs_val[] = {
            RegRead(CRYPTO14_SHA1_V_VALUE_0),
            RegRead(CRYPTO14_SHA1_V_VALUE_1),
            RegRead(CRYPTO14_SHA1_V_VALUE_2),
            RegRead(CRYPTO14_SHA1_V_VALUE_3),
            RegRead(CRYPTO14_SHA1_V_VALUE_4),
    };

    uint32_t numOfShaOutRegs = SHA1_HASH_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_UINT32T;

    for (i = 0U; i < (uint8_t)numOfShaOutRegs; i++) {
          setLe32(sha1_hash_regs_val[i], &hash[i * 4U]);
    }

    /* Reset SHA-1 module */
    RegWrite(HDCP_CRYPTO_CONFIG, RegFieldSet(HDCP_CRYPTO_CONFIG, CRYPTO_SW_RST, 0U));
    RegWrite(HDCP_CRYPTO_CONFIG, 0U);
}

/**
 * Data processing for SHA256 module
 * @param[in] data, pointer to SHA block
 */
static void sha256_process(const uint8_t data[SHA_BLOCK_SIZE_IN_BYTES])
{
    uint8_t i;
    uint32_t regVal;
    uint8_t offset = 0U;
    const uint32_t mask = RegFieldSet(CRYPTO22_STATUS, SHA256_NEXT_MESSAGE_ST, 0U);

    /* Put data into module */
    for (i = 0U ; i < (SHA_BLOCK_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_UINT32T); i++) {
        regVal = getBe32(&data[offset]);
        offset += NUMBER_OF_BYTES_IN_UINT32T;

        RegWrite(SHA_256_DATA_IN, regVal);
    }

    /* Wait for ACK from module */
    do {
        regVal = RegRead(CRYPTO22_STATUS);
    } while ((mask & regVal) == 0U);
}

/**
 * Data processing for SHA1 module
 * @param[in] data, pointer to SHA block
 */
static void sha1_process(const uint8_t data[64] )
{
    const uint32_t mask = RegFieldSet(CRYPTO14_STATUS, SHA1_NEXT_MSG, 0U);
    uint32_t regVal;
    uint8_t i;
    uint8_t offset = 0U;

    /* TODO: mbeberok, what mean this value? */
    RegWrite(CRYPTO14_BLOCKS_NUM, 0xEFFFFFFFU);

    /* Put data into module */
    for (i = 0U;i < (SHA_BLOCK_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_UINT32T); i++){
        regVal = getBe32(&data[offset]);
        offset += NUMBER_OF_BYTES_IN_UINT32T;

        RegWrite(CRYPTO14_SHA1_MSG_DATA, regVal);
    }

    /* Wait for ACK from module */
    do {
        regVal = RegRead(CRYPTO14_STATUS);
    } while ((mask & regVal) == 0U);
}

/**
 * Common function (both SHA1 and SHA256) used to update SHA context buffer
 * @param[in] total, number (in bytes) of totally processed data so far
 * @param[in] pointer to SHA context buffer
 * @param[in] input, pointer to buffer with data to be processed
 * @param[in] inputLen, length of input data in bytes
 * @param[in] shaAlg, used SHA algorithm (SHA1 or SHA256)
 */
static void sha_update(uint32_t* total, uint8_t* ctxBuffer, const uint8_t* input, uint32_t inputLen, ShaAlg_t shaAlg)
{
    /* Number of bytes is stored in context buffer but not processed by SHA256*/
    uint32_t pendingDataLen;
    /* Number of bytes need to fill the buffer full */
    uint32_t dataNumToFullBlock;
    /* Number of input bytes to process */
    uint32_t dataNumToProcess = inputLen;
    /* Pointer to input processed byte */
    const uint8_t* processedBytePtr = input;
    /* Pointer to process function */
    void (*sha_process)(const uint8_t* data);

    if (dataNumToProcess != 0U) {

        sha_process = (shaAlg == SHA_256) ? &sha256_process : &sha1_process;

        pendingDataLen = getNumberOfPendingDataInBytes(total);
        /* SHA256 input is organized into 512b (64B) blocks */
        dataNumToFullBlock = SHA_BLOCK_SIZE_IN_BYTES - pendingDataLen;

        addBytes(total, dataNumToProcess);

        /* Check if any pending data in SHA buffer and sufficient amount of data
           in input buffer (to fulfill SHA block) */
        if ((pendingDataLen != 0U) && (dataNumToProcess >= dataNumToFullBlock)) {
            CPS_BufferCopy(&ctxBuffer[pendingDataLen], processedBytePtr, dataNumToFullBlock);
            sha_process(ctxBuffer);
            dataNumToProcess -= dataNumToFullBlock;
            /* Update pointer to data by amount of precessed data */
            processedBytePtr = &processedBytePtr[dataNumToFullBlock];
            /* Any pending data */
            pendingDataLen = 0U;
        }

        /* Process rest of data in input, if there is any SHA256 block */
        while (dataNumToProcess >= SHA_BLOCK_SIZE_IN_BYTES) {
            sha_process(processedBytePtr);
            dataNumToProcess -= SHA_BLOCK_SIZE_IN_BYTES;
            /* Update pointer to data - next SHA256 block*/
            processedBytePtr = &processedBytePtr[SHA_BLOCK_SIZE_IN_BYTES];
        }

        /* Copy data to SHA256 buffer if required */
        if (dataNumToProcess > 0U) {
            CPS_BufferCopy(&ctxBuffer[pendingDataLen], processedBytePtr, dataNumToProcess);
        }
    }
}

/**
 * Common function (both SHA1 and SHA256) used to finish hashing
 * @param[in] total, number (in bytes) of totally processed data so far
 * @param[in] pointer to SHA context buffer
 * @param[out] output, pointer to hash buffer
 * @param[in] shaAlg, used SHA algorithm (SHA1 or SHA256)
 */
/* parasoft-begin-suppress MISRA2012-RULE-8_13_a-4 "Pass parameter with const specifier, DRV-4037 */
/* parasoft-begin-suppress METRICS-39-3 "The value of VCOF is higher than 4", DRV-5191 */
static void sha_finish(uint32_t* total, uint8_t* ctxBuffer, uint8_t* output, ShaAlg_t shaAlg)
{
    /* Array used to padding messages */
    static const uint8_t shaPaddingArray[SHA_BLOCK_SIZE_IN_BYTES] = {
        0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    uint8_t pendingData;
    uint8_t paddingBytes;
    uint8_t msgLenInBits[SHA_MESSAGE_LENGTH_SIZE_IN_BYTES];

    getMessageSizeInBits(total, msgLenInBits);

    /* Get number of pending bytes in SHA256 buffer to calculate number of padding bytes */
    pendingData = getNumberOfPendingDataInBytes(total);

    /* Each message must be closed with length of message in bits (required 8B)
       If enough space in buffer, need to fulfill buffer with padding data, process
       and put in next buffer 56B of padding data */
    paddingBytes = getNumberOfPaddingBytes(pendingData);

    /* Make message padding */
    sha_update(total, ctxBuffer, shaPaddingArray, paddingBytes, shaAlg);

    /* Finish message with message length */
    sha_update(total, ctxBuffer, msgLenInBits, SHA_MESSAGE_LENGTH_SIZE_IN_BYTES, shaAlg);

    /* Get hashed data from module registers */
    if (shaAlg == SHA_256) {
        get_sha256_hash(output);
    } else {
        get_sha1_hash(output);
    }

}
/* parasoft-end-suppress MISRA2012-RULE-8_13_a-4 */
/* parasoft-end-suppress METRICS-39-3 */

/* parasoft-begin-suppress METRICS-36-3, "Function is called from more than 5 functions, DRV-3823 */
void sha256_starts(Sha256Context_t *ctx)
{
    /* Set to 1 to start SHA-256 */
    RegWrite(CRYPTO22_CONFIG, RegFieldSet(CRYPTO22_CONFIG, SHA_256_START, 0U));

    /* Cleanup number of processed bytes */
    ctx->total[0] = 0U;
    ctx->total[1] = 0U;
}
/* parasoft-end-suppress METRICS-36-3 */

void sha256_init(Sha256Context_t* ctx)
{
    /* Cleanup context */
    (void)memset(ctx, 0, sizeof(Sha256Context_t));

    /* Start SHA256 module */
    sha256_starts(ctx);

}

void sha256_update(Sha256Context_t *ctx, const uint8_t* input, uint32_t inputLen)
{
    /* Call update function for SHA256 */
    sha_update(ctx->total, ctx->buffer, input, inputLen, SHA_256);
}

void sha256_finish(Sha256Context_t *ctx, uint8_t output[SHA256_HASH_SIZE_IN_BYTES])
{
    /* Call finish function for SHA256 */
    sha_finish(ctx->total, ctx->buffer, output, SHA_256);
}

void sha256(const uint8_t* input, uint32_t inputLen, uint8_t output[SHA256_HASH_SIZE_IN_BYTES])
{
    /* Create context */
    Sha256Context_t ctx = {{0}, {0}, {0}, {0}};

    /* Hash input data */
    sha256_starts(&ctx);
    sha256_update(&ctx, input, inputLen);
    sha256_finish(&ctx, output);
}

/**
 * Start HMAC-SHA256 context
 * @param[in] ctx, pointer to SHA256 context
 * @param[in] key, pointer to buffer with secret key
 * @param[in] inputKeyLen, length of secret key in bytes
 */
static void sha256_hmac_starts(Sha256Context_t *ctx, const uint8_t* key, uint32_t inputKeyLen)
{
    /* Definition of HMAC taken from:
       "HMAC: Keyed-Hashing for Message Authentication, RFC-2104" */

    uint32_t i;
    uint32_t keyLen = inputKeyLen;
    uint8_t shaOutput[SHA256_HASH_SIZE_IN_BYTES];
    /* Block-sized key derived from the secret key*/
    const uint8_t* Kprime = key;

    /* If K is larger than block size, calculate K', if not - K' is K */
    if(keyLen > SHA_BLOCK_SIZE_IN_BYTES) {
        sha256(key, keyLen, shaOutput);
        keyLen = SHA256_HASH_SIZE_IN_BYTES;
        Kprime = shaOutput;
    }

    /* Get padding */
    generateBlockSizedKeyPadding(Kprime, keyLen, ctx->ipad, ctx->opad);

    sha256_starts(ctx);

    sha256_update(ctx, ctx->ipad, SHA_BLOCK_SIZE_IN_BYTES);
}

/**
 * Wrapper to SHA256 update function for HMAC
 * @param[in] ctx, pointer to SHA256 context
 * @param[in] input, buffer to be hashed
 * @parampin] inputLen, size of input buffer in bytes
 */
static void sha256_hmac_update(Sha256Context_t *ctx, const uint8_t* input, uint32_t inputLen)
{
    sha256_update(ctx, input, inputLen);
}

/**
 * Finish hashing by add padding (if required) and message length for HMAC-SHA256
 * @param[in] ctx, pointer to SHA256 context
 * @param[out] hash, buffer with hashed data
 */
static void sha256_hmac_finish(Sha256Context_t *ctx, uint8_t hash[SHA256_HASH_SIZE_IN_BYTES])
{
    sha256_finish(ctx, hash);

    /* Now in 'hash' is hash value of SHA256((K' XOR ipad) | message) */
    sha256_starts(ctx);
    sha256_update(ctx, ctx->opad, SHA_BLOCK_SIZE_IN_BYTES);
    sha256_update(ctx, hash, SHA256_HASH_SIZE_IN_BYTES);
    sha256_finish(ctx, hash);
}

void sha256_hmac(const uint8_t* key, uint32_t keyLen, const uint8_t* inputBuffer, uint32_t inputLen, uint8_t hmacBuffer[SHA256_HASH_SIZE_IN_BYTES])
{
    /* Create SHA256 context */
    Sha256Context_t ctx = {{0}, {0}, {0}, {0}};

    /* Hash input data with HMAC */
    sha256_hmac_starts( &ctx, key, keyLen);
    sha256_hmac_update( &ctx, inputBuffer, inputLen );
    sha256_hmac_finish( &ctx, hmacBuffer);
}

void sha1(const uint8_t *input, uint32_t inputLen, uint8_t output[SHA1_HASH_SIZE_IN_BYTES])
{
    Sha1Context_t ctx = {{0}, {0}};

    /* Cleanup context */
    ctx.total[0] = 0U;
    ctx.total[1] = 0U;

    sha_update(ctx.total, ctx.buffer, input, inputLen, SHA_1);
    sha_finish(ctx.total, ctx.buffer, output, SHA_1);
}



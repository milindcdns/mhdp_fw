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
 * pkcs1.c
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#include "libHandler.h"
#include "pkcs1.h"
#include "asn1.h"
#include "sha.h"
#include "cps.h"
#include "cdn_errno.h"

/* Helper structure used to temporarily store calculation data */
typedef struct {
    Ipi_t buffer;
    Ipi_t exponent_e;
    Ipi_t modulus_n;
} PublicKeyHlp_t;

/**
 * Used to generate seed in public key encryption process
 * @param[out] output, pointer to seed
 * @param[in] len, number of bytes in seed
 */
static void myrand(uint8_t *output, uint32_t len)
{
    uint32_t i;

    for( i = 0U; i < len; i++) {
        output[i] = (uint8_t)i;
    }
}

/**
 * Used to cleanup public key auxiliary structure
 * @param[in] pubKeyHlp, pointer to auxiliary public key structure
 */
static void cleanup_pub_key_helper(PublicKeyHlp_t* pubKeyHlp)
{
    ipi_free(&pubKeyHlp->buffer);
    ipi_free(&pubKeyHlp->exponent_e);
    ipi_free(&pubKeyHlp->modulus_n);
}

/**
 * Used to convert byte buffers into BigInt structures
 * @param[out] pubKeyHlp, pointer to auxiliary public key structure
 * @param[in] pkcsHelper, pointer to structure with pointers with data
 * @return CDN_EOK if conversion succeeds
 * @return CDN_ENOSPC if not enough space to convert data
 */
static inline uint32_t convert_buffers_into_ipis(PublicKeyHlp_t* pubKeyHlp, PkcsParam_t* pkcsHelper)
{
    uint32_t retVal;

    /* Convert input buffer to ipi */
    retVal  = ipi_rd_binary(&pubKeyHlp->buffer, pkcsHelper->input.ptr, pkcsHelper->input.size);
    retVal |= ipi_rd_binary(&pubKeyHlp->exponent_e, pkcsHelper->exponent_e.ptr, pkcsHelper->exponent_e.size);
    retVal |= ipi_rd_binary(&pubKeyHlp->modulus_n, pkcsHelper->modulus_n.ptr, pkcsHelper->modulus_n.size);

    return retVal;
}

/**
 * Public key generator
 * @param[in,out] pkcHelper, pointer to auxiliary structure
 * @return CDN_EOK if public key generation succeeds
 * @return CDN_EINVAL if invalid input data
 * @return CDN_ENOSPC if not enough space to convert input data
 */
static uint32_t public_key_operation(PkcsParam_t* pkcsHelper)
{
    uint32_t retVal;
    Buffer_t* output;

    static PublicKeyHlp_t pubKeyHelper = {EMPTY_IPI, EMPTY_IPI, EMPTY_IPI};

    if (lib_handler.rsa_index == 0U) {
        /* Initialize */
        cleanup_pub_key_helper(&pubKeyHelper);

        retVal = convert_buffers_into_ipis(&pubKeyHelper, pkcsHelper);

        if (retVal == CDN_EOK) {

            retVal = CDN_EINPROGRESS;

            if (ipi_cmp(&pubKeyHelper.buffer, &pubKeyHelper.modulus_n) != COMPARISON_RESULT_LOWER) {
                retVal = CDN_EINVAL;
            }
        }

        lib_handler.rsa_index = 1U;

    } else {

        retVal = ipi_exp_mod(&pubKeyHelper.buffer,
                             &pubKeyHelper.buffer,
                             &pubKeyHelper.exponent_e,
                             &pubKeyHelper.modulus_n);

        if (retVal == CDN_EOK) {
            /* Operation is finished without errors */
            output = &pkcsHelper->output;
            retVal = ipi_wr_binary(&pubKeyHelper.buffer, output->ptr, output->size);
            /* Cleanup here to avoid unclean if above returns CDN_EOK */
            cleanup_pub_key_helper(&pubKeyHelper);
        }
    }

    /* Cleanup temporary data if error occurred */
    if ((retVal != CDN_EINPROGRESS) && (retVal != CDN_EOK)) {
        cleanup_pub_key_helper(&pubKeyHelper);
    }

    return retVal;
}

/**
 * Generate and apply the MGF1 operation (from PKCS#1 v2.1) to a buffer.
 * @param[in] dst, pointer to masked buffer
 * @param[in] dlen, length of masked buffer
 * @param[in] src, pointer to buffer based on which the mask will be generated
 * @param[in] slen, length of masking buffer
 * @param[in] sha256_ctx, pointer to current SHA256 context
 */
static void mgf_mask (uint8_t* dst, uint32_t dlen, uint8_t* src,
                      uint32_t slen, Sha256Context_t* sha256_ctx)
{
    uint8_t mask[SHA256_HASH_SIZE_IN_BYTES] = { 0U };
    uint8_t counter[4] = { 0U };

    uint8_t* masked_buffer = dst;
    uint32_t data_length = dlen;

    uint8_t i;
    uint8_t use_len;

    while( data_length > 0U )
    {
        /* If number of data to mask is greater than max,
           take maximum available (size of SHA256 hash) */
        if(data_length < SHA256_HASH_SIZE_IN_BYTES) {
            use_len = (uint8_t)data_length;
        } else {
            use_len = SHA256_HASH_SIZE_IN_BYTES;
        }

        /* Do SHA256 hashing */
        sha256_starts(sha256_ctx);
        sha256_update(sha256_ctx, src, slen );
        sha256_update(sha256_ctx, counter, 4 );
        sha256_finish(sha256_ctx, mask );

        /* Apply mask into buffer */
        for( i = 0U; i < use_len; i++) {
            *masked_buffer ^= mask[i];
            masked_buffer++;
        }

        /* For each hash function amount of done hashing so far is concatenated */
        counter[3]++;

        /* Update number of bytes to process */
        data_length -= (uint32_t)use_len;
    }

}

/**
 * Simply loop used to fill data block with 0's
 * @param[out] ps_block, pointer to PS block
 * @param[in] ps_size, number of needed 0's in ps_block
 */
static inline void gen_ps_block(uint8_t* ps_block, uint32_t ps_size)
{
    (void)memset(ps_block, 0, ps_size);
}

/**
 * Returns size of PS block
 * @param[in] msgSize, size of encoded message in bytes
 * @param[in] dataBlockLen, size of data block in bytes
 */
static inline uint32_t get_ps_size(uint32_t msgSize, uint32_t dataBlockLen)
{
    return dataBlockLen - msgSize - SHA256_HASH_SIZE_IN_BYTES - 1U;
}

/**
 * Auxiliary function used to generate data block
 * @param[out] dataBlock, pointer to data block buffer
 * @param[in] dataBlockLen, size of data block in bytes
 * @param[in] msg, pointer with message which will be encoded
 * @param[in] msgSize, size of message in bytes
 */
static void generate_data_block(uint8_t* dataBlock, uint32_t dataBlockLen, const uint8_t* msg, uint32_t msgSize)
{
    uint32_t offset = 0U;
    uint32_t ps_size = get_ps_size(msgSize, dataBlockLen);
    uint8_t i;

    /* Generate pHash, for padding message */
    sha256(NULL, 0U, &dataBlock[offset]);
    offset += SHA256_HASH_SIZE_IN_BYTES;

    /* Generate PS block */
    gen_ps_block(&dataBlock[offset], ps_size);
    offset += ps_size;

    /* Generate end of padding */
    dataBlock[offset] = 0x01U;
    offset++;

    /* Copy message into dataBlock */
    CPS_BufferCopy(&dataBlock[offset], msg, msgSize);
}

uint32_t pkcs1_rsaes_oaep_encrypt(PkcsParam_t* pkcsParams)
{
    static PkcsParam_t local_pkcs;

    Sha256Context_t sha256_ctx;

    Buffer_t* output_buffer;
    Buffer_t* input_buffer;

    /* Seed if length of hash length (for SHA256 - 32B) */
    uint8_t* seed;
    /* Data block: 0x00 | pHash | PS | 0x01 | message, where:
       - pHash is length of hash length (SHA256 -32B)
       - PS is length of 'encryptedMessageSize - messageSize - 2 * hashSize - 1' 0 octets string
       - message is input data
     */
    uint8_t* dataBlock;
    uint32_t dataBlockLen;
    uint32_t retVal;

    if (lib_handler.rsa_index == 0U)
    {
        /* Generate encrypted message (look at RFC 3447, page 16)
         * 0x00 | seed xor seedMask | DB xor dbMask */
        output_buffer = &pkcsParams->output;
        input_buffer = &pkcsParams->input;

        seed = &output_buffer->ptr[1];
        dataBlock = &output_buffer->ptr[SHA256_HASH_SIZE_IN_BYTES + 1U];

        /* First byte must be 0x00 */
        output_buffer->ptr[0] = 0x00U;

        /* Generate seed */
        myrand(seed, SHA256_HASH_SIZE_IN_BYTES);

        /* Construct DB */
        dataBlockLen = output_buffer->size - SHA256_HASH_SIZE_IN_BYTES - 1U;
        generate_data_block(dataBlock, dataBlockLen, input_buffer->ptr, input_buffer->size);

        sha256_init(&sha256_ctx);

        /* Apply dbMask to DB */
        mgf_mask(dataBlock, dataBlockLen, seed, SHA256_HASH_SIZE_IN_BYTES, &sha256_ctx);

        /* Apply seedMask to seed */
        mgf_mask(seed, SHA256_HASH_SIZE_IN_BYTES, dataBlock, dataBlockLen, &sha256_ctx);

        /* Encrypted message (input data for public_key_operation) is stored in output,
         * so it's needed to make copy of input parameter and point input in local_pkcs
         * as output in pkcsParams */
        local_pkcs = *pkcsParams;
        local_pkcs.input = local_pkcs.output;
    }


    return public_key_operation(&local_pkcs);
}

/**
 * Used to update internal data during padding cheecking
 * @param[in, out] address, pointer to padding byte
 * @param[in, out] size, number of verified bytes so far
 */
static void update_padding(const uint8_t** address, uint32_t* size)
{
    (*address)++;
    (*size)++;
}

/**
 * Used to check if padding is correct
 * @param[in, out] buffer, address of current verifying byte
 * @param[in] sigLen, size of signature in bytes
 * @param[out] paddingSize, number of padding bytes
 * @return CDN_EOK if padding is correct
 * @return CDN_EINVAL if padding is incorrect
 */
static uint32_t checkPadding(const uint8_t** buffer, uint32_t signLen, uint32_t* paddingSize)
{
    uint32_t retVal = CDN_EOK;
    const uint8_t* padding = *buffer;

    /* Key should start with 0x00 */
    if(*padding != 0U) {
        retVal = CDN_EINVAL;
    }

    /* Next should be SIGN_SCHEME */
    if (retVal == CDN_EOK) {

        /* Go to next byte */
        update_padding(&padding, paddingSize);

         if (*padding != CDN_PKCS1_SIGN_SCHEME) {
            retVal = CDN_EINVAL;
        }

         /* Go to next byte */
         update_padding(&padding, paddingSize);
    }

    /* Next should be sequence of 0xFF. Check as long as 0x00 will be detected */
    while((retVal == CDN_EOK) &&  (*padding != 0U)) {

        if ((*paddingSize > signLen) || (*padding != 0xFFU)) {
            retVal = CDN_EINVAL;
        }

        /* Go to next byte */
        update_padding(&padding, paddingSize);
    }

    /* Go to next byte */
    update_padding(&padding, paddingSize);

    /* Update temporary buffer address */
    *buffer = padding;

    return retVal;

}

/**
 * Used to verify single ASN.1 data structure (tag and length octets)
 * @param[in, out] tagCheckerHlp, pointer to auxiliary structure
 * @param[in] tag, expected tag value
 * @param[in] len, expected value of length octets
 */
static uint32_t verify_single_tag(TagCheckerHlp_t* tagCheckerHlp, uint8_t tag, uint32_t len)
{
    tagCheckerHlp->expected_length = len;
    tagCheckerHlp->tag = tag;
    return asn1_check_tag(tagCheckerHlp);
}

/**
 * Used to initialize auxiliary structure used to tag checking
 * @param[out] tagCheckerHlp, pointer to auxiliary structure
 * @param[in] pubKey, pointer to data buffer
 * @param[in] remainingData, number of remaining bytes to analyze
 */
static void init_tag_checker_helper(TagCheckerHlp_t* tagCheckerHlp, const uint8_t** pubKey, const uint32_t* remainingData)
{
    tagCheckerHlp->buffer = *pubKey;

    tagCheckerHlp->buffer_size = *remainingData;
    tagCheckerHlp->processed_bytes = 0U;

    /* Need to set before each use of helper */
    tagCheckerHlp->expected_length = 0U;
    tagCheckerHlp->tag = 0U;
}

/**
 * Finish verifying tags by updating internal data
 * @param[in] tagCheckerHlp, pointer to auxiliary structure
 * @param[out] pubKey, pointer to data buffer
 * @param[out] remainingData, number of remaining bytes to analyze
 */
static void finish_verifying_tags(const TagCheckerHlp_t* tagCheckerHlp, const uint8_t** pubKey, uint32_t* remainingData)
{
    *pubKey = tagCheckerHlp->buffer;
    *remainingData -= tagCheckerHlp->processed_bytes;
}

/**
 * Used to verify if tags are same as expected in ASN.1 data structure
 * @param[in, out] pubKey, pointer to data buffer
 * @param[in, out] remainingData, number of remaining bytes to analyze
 */
static uint32_t verify_tags(const uint8_t** pubKey, uint32_t* remainingData)
{
    uint32_t retVal = CDN_EOK;
    uint32_t exp_size = *remainingData - CDN_ASN1_BUFFER_UPDATE_SIZE;

    TagCheckerHlp_t tagCheckerHlp;

    init_tag_checker_helper(&tagCheckerHlp, pubKey, remainingData);

    /* Verify first tag */
    retVal = verify_single_tag(&tagCheckerHlp,
                               ASN1_CONSTRUCTED | ASN1_SEQUENCE,
                               exp_size);

    /* Verify second tag */
    if (retVal == CDN_EOK) {
        /* Second tag is same as first, only length is different */
        exp_size -= SHA256_HASH_SIZE_IN_BYTES;
        exp_size -= (2U * (uint16_t)CDN_ASN1_BUFFER_UPDATE_SIZE);
        retVal = verify_single_tag(&tagCheckerHlp,
                                   ASN1_CONSTRUCTED | ASN1_SEQUENCE,
                                   exp_size);
    }

    /* Verify third tag */
    if (retVal == CDN_EOK) {

        retVal = verify_single_tag(&tagCheckerHlp,
                                   ASN1_OID,
                                   (uint32_t)CDN_ASN1_SHA256_SIZE);
    }

    /* Check if data was hashed using SHA256 */
    if (retVal == CDN_EOK) {

        if (!check_if_hashed_by_sha256(tagCheckerHlp.buffer)) {
            retVal = CDN_EINVAL;
        }

        /* Update pointer to buffer up to ASN1_SHA256_REPRESENTATION size */
        tagCheckerHlp.buffer = &tagCheckerHlp.buffer[CDN_ASN1_SHA256_SIZE];
        tagCheckerHlp.processed_bytes += (uint32_t)CDN_ASN1_SHA256_SIZE;
    }

    /* Verify fourth tag */
    if (retVal == CDN_EOK) {
        retVal = verify_single_tag(&tagCheckerHlp,
                                   ASN1_NULL,
                                   0U);
    }

    /* Verify fifth tag */
    if (retVal == CDN_EOK) {
        retVal = verify_single_tag(&tagCheckerHlp,
                                   ASN1_OCTET_STRING,
                                   SHA256_HASH_SIZE_IN_BYTES);
    }

    /* Update buffer position and number of remaining data */
    finish_verifying_tags(&tagCheckerHlp, pubKey, remainingData);

    return retVal;

}

/**
 * Returns CDN_EOK if hash is same as expected, otherwise CDN_EINVAL
 */
static inline uint32_t verify_hash(const uint8_t* hash, const uint8_t* calcHash, uint32_t hashSize)
{
    return (memcmp(hash, calcHash, hashSize) != 0) ? CDN_EINVAL : CDN_EOK;
}

/**
 * Used to verify public key (checks padding, tags and hash)
 * @param[in] pubKey, pointer to data buffer
 * @param[in] hashedKey, expected value of hash
 * @param[in] keySize, size of hash in bytes
 */
static uint32_t verify_public_key(const uint8_t* pubKey, const uint8_t* hashedKey, uint32_t keySize)
{
    uint32_t paddingSize = 0U;
    uint32_t remainingData;

    /* To avoid changing parameter address during process */
    const uint8_t* keyBufferPtr = pubKey;

    uint32_t retVal = checkPadding(&keyBufferPtr, keySize, &paddingSize);

    if (retVal == CDN_EOK) {
        /* Set real size of ASN.1 data structure */
        remainingData = keySize - paddingSize;
        retVal = verify_tags(&keyBufferPtr, &remainingData);
    }

    if ((retVal == CDN_EOK) && (remainingData == SHA256_HASH_SIZE_IN_BYTES)) {
        /* Check if hash is same as expected */
        retVal = verify_hash(keyBufferPtr, hashedKey, SHA256_HASH_SIZE_IN_BYTES);
    } else {
        retVal = CDN_EINVAL;
    }

    return retVal;

}

void set_pkcs_parameter(Buffer_t* param, uint8_t* buffer, uint32_t size)
{
    /* Set buffer pointer and size */
    param->ptr = buffer;
    param->size = size;
}


uint32_t pkcs1_v15_rsassa_verify(PkcsParam_t* pkcsParam, const uint8_t* hash)
{
    uint32_t retVal;

    /* Generate public key */
    retVal = public_key_operation(pkcsParam);

    if (retVal == CDN_EOK) {
        /* Calculate hash from generated public key and check if is as expected */
        retVal = verify_public_key(pkcsParam->output.ptr, hash, pkcsParam->modulus_n.size);
    }

    return retVal;
}

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
 * asn1parse.c
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#include "asn1.h"
#include "utils.h"
#include "cdn_errno.h"

/**
 * Returns 'true' if value of tag equals expected, otherwise 'false'
 */
static inline bool check_if_tag_correct(uint8_t tag, uint8_t expTag)
{
	return tag == expTag;
}


/**
 * Update buffer address to next data structure
 * @param[out] new_buf, address of next data structure
 * @param[in] old_buf, address of current data structure
 * @param[in] offset, number of length octets in current structure
 */
static inline void update_buffer_address(const uint8_t** new_buf, const uint8_t* old_buf, uint8_t offset)
{
	/* Move buffer pointer ahead */
	*new_buf = &(old_buf[offset + CDN_ASN1_BUFFER_UPDATE_SIZE]);
}

/**
 * Returns length from encoding structure.
 * @param[out] length, value of length read from block
 * @param[in] lenPtr, pointer to encoding buffer
 * @param[in] octNum, number of bytes reserved for length octets
 */
static uint32_t get_length(uint32_t* length, const uint8_t* lenPtr, uint8_t octNum)
{
	uint32_t retVal = CDN_EINVAL;

	/* Cleanup value to avoid trash data */
	*length = 0U;

	/* Check if number of length octets is in permissible range */
	if ((octNum > 0U) && (octNum <= CDN_ASN1_MAX_OCTETS_NUM)) {

		retVal = CDN_EOK;

		if (octNum == 1U) {
			*length = (uint32_t)*lenPtr;
		} else if (octNum == 2U) {
			*length = getBe16(lenPtr);
		} else if (octNum == 3U) {
			*length = getBe24(lenPtr);
		} else {
			/* For octNum equals 4U */
			*length = getBe32(lenPtr);
		}
	}

	return retVal;
}

/**
 * Returns length from encoding structure when definite long form is used
 * @param[in] buf, pointer to encoding buffer
 * @param[in] end, address of last byte in buffer
 * @param[out] len, value of length read from block
 * @param[out] octNum, number of bytes reserved for length octets
 */
static uint32_t get_long_format_length(const uint8_t* buf, uint32_t bufSize, uint32_t processedBytes, uint32_t* len, uint8_t* octNum)
{
	uint32_t retVal = CDN_EINVAL;

	*octNum = buf[0] & CDN_ASN1_SHORT_FORMAT_MASK;

	if ((processedBytes + *octNum) < bufSize) {
		retVal = get_length(len, &buf[1], *octNum);
	}

	return retVal;
}

/**
 * Returns length from encoding structure
 * @param[in] buf, pointer to encoding buffer
 * @param[in] end, address of last byte in buffer
 * @param[out] len, value of length read from block
 * @param[out] octNum, number of bytes reserved for length octets
 */
static uint32_t asn1_get_len(const uint8_t* buf, uint32_t bufSize, uint32_t processedBytes, uint32_t* len, uint8_t* octNum)
{
	uint32_t retVal = CDN_EINVAL;
	uint8_t i;

    if (processedBytes < bufSize) {

    	/* Check if 'short' or 'long' form is used */
    	if ((buf[0] & CDN_ASN1_LONG_FORMAT_MASK) != 0U) {
    		retVal = get_long_format_length(buf, bufSize, processedBytes, len, octNum);
    	} else {
    		/* If 'short' used no length octets occurred */
    		*len = buf[0];
    		*octNum = 0U;
    		retVal = CDN_EOK;
    	}

    }

    if (retVal == CDN_EOK) {
        if ((*octNum + *len + processedBytes) > bufSize) {
            retVal = CDN_EINVAL;
        }
    }

    return retVal;
}

uint32_t asn1_check_tag(TagCheckerHlp_t* tagCheckerHlp)
{
	uint32_t retVal = CDN_EINVAL;
	/* Number of read length octets. If short format should be 0U */
	uint8_t octRead;
	bool is_tag_correct;
	uint32_t length;

	const uint32_t buffer_size = tagCheckerHlp->buffer_size;
	const uint8_t* buf = tagCheckerHlp->buffer;
    uint32_t* processed_bytes = &tagCheckerHlp->processed_bytes;

	/* Calculate if tag is correct */
	is_tag_correct  = (*processed_bytes < buffer_size);
	is_tag_correct  = (check_if_tag_correct(buf[0], tagCheckerHlp->tag)) && (is_tag_correct);

	if (is_tag_correct) {
		/* Get value of length octets */
		retVal = asn1_get_len(&(buf[1]), buffer_size, *processed_bytes, &length, &octRead);
	}

	if (retVal == CDN_EOK) {

		update_buffer_address(&tagCheckerHlp->buffer, buf, octRead);

		/* Update number of bytes to be verify */
		*processed_bytes += CDN_ASN1_BUFFER_UPDATE_SIZE;

		/* Check if length octets value is same expected */
		if (length != tagCheckerHlp->expected_length) {
			retVal = CDN_EINVAL;
		}
	}

	return retVal;
}

bool check_if_hashed_by_sha256(const uint8_t* buffer)
{
	static const uint8_t val[] = CDN_ASN1_SHA256_REPRESENTATION;
	return if_buffers_equal(val, buffer, (uint32_t)CDN_ASN1_SHA256_SIZE);
}

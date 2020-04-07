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
 *asn1.h
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#ifndef CDN_ASN1_H
#define CDN_ASN1_H

#include "ipi_calc.h"
#include "utils.h"

/* Maximum number of length octets in ASN.1 */
#define CDN_ASN1_MAX_OCTETS_NUM                        4U

#define CDN_ASN1_LONG_FORMAT_MASK                      0x80U
#define CDN_ASN1_SHORT_FORMAT_MASK                     0x7FU

#define CDN_ASN1_BUFFER_UPDATE_SIZE					   2U

#define CDN_ASN1_SHA256_REPRESENTATION "\x60\x86\x48\x01\x65\x03\x04\x02\x01"
#define CDN_ASN1_SHA256_SIZE (sizeof(CDN_ASN1_SHA256_REPRESENTATION) - 1U)

/* \} name */

/**
 * DER constants
 * These constants comply with DER encoded the ANS1 type tags.
 * DER encoding uses hexadecimal representation.
 * An example DER sequence is:
 * - 0x02 -- tag indicating INTEGER
 * - 0x01 -- length in octets
 * - 0x05 -- value
 */
#define ASN1_OCTET_STRING            0x04U
#define ASN1_NULL                    0x05U
#define ASN1_OID                     0x06U
#define ASN1_SEQUENCE                0x10U
#define ASN1_CONSTRUCTED             0x20U

typedef struct {
	/* Pointer to buffer with tag */
	const uint8_t* buffer;
	/* Expected tag value */
	uint8_t tag;
	/* Size of buffer, used to checking if address is in range */
	uint32_t buffer_size;
	/* Expected value of 'length' field for tag*/
	uint32_t expected_length;
	/* Number of processed bytes to check */
	uint32_t processed_bytes;
} TagCheckerHlp_t;

/**
 * Used to verify that ASN.1 data structure is same as expected
 * @param[in/out] tagCheckerHlp, pointer to helper strucutre
 */
uint32_t asn1_check_tag(TagCheckerHlp_t* tagCheckerHlp);

/**
 * Used to verify that ASN.1 data structure was hashed using SHA256
 * @param[in] buffer, pointer to  SHA256 representation read from ASN.1 data strucutre
 */
bool check_if_hashed_by_sha256(const uint8_t* buffer);

#endif /* asn1.h */

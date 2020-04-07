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
 * pkcs1.h
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#ifndef PKCS1_H
#define PKCS1_H

#include "ipi_calc.h"

#define CDN_PKCS1_SIGN_SCHEME 0x01U    	// indicates Signature schemes

typedef struct {
	/* Pointer to buffer */
	uint8_t* ptr;
	/* Size of buffer */
	uint32_t size;
} Buffer_t;

typedef struct {
	Buffer_t input;
	Buffer_t output;
	Buffer_t modulus_n;
	Buffer_t exponent_e;
} PkcsParam_t;

/**
 * Used to set simply parameter from PkcsParam_t structure
 * @param[out] param, pointer to data which are set
 * @param[in] buffer, pointer to allocated buffer
 * @param[in] size, size of allocated buffer in bytes
 */
void set_pkcs_parameter(Buffer_t* param, uint8_t* buffer, uint32_t size);

/**
 * Implementation of the PKCS#1 v2.1 RSAES-OAEP-ENCRYPT function
 * @param[in] pkcsParams, pointer to auxiliary structure
 * @return CDN_EOK if encryption succeed
 * @return CDN_EINVAL if invalid input parameters
 * @return CDN_ENOSPC if not enough space on device
 * @return CDN_EINPROGRESS if operation was not done yet
 */
uint32_t pkcs1_rsaes_oaep_encrypt(PkcsParam_t* pkcsParams);

/**
 * Implementation of the PKCS#1 v1.5 RSASSA-VERIFY function
 * @param[in] pkcsParams, pointer to auxiliary structure
 * @param[in] hash, pointer to buffer with expected hash
 * @return CDN_EOK if encryption succeed
 * @return CDN_EINVAL if invalid input parameters
 * @return CDN_EINPROGRESS if operation was not done yet
 */
uint32_t pkcs1_v15_rsassa_verify(PkcsParam_t* pkcsParam, const uint8_t* hash);

#endif /* PKCS1_H */

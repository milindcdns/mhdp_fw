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
 * aes.h
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#ifndef AES_H
#define AES_H

/* Size of AES-32 module data in bytes */
#define AES_CRYPT_DATA_SIZE_IN_BYTES 16U

/*
 * Used to pass AES-32 input key word into module
 * @param[in] key, AES-32 key
 */
void aes_setkey(const uint8_t* key);

/*
 * Pass data into module and receive encrypted data
 * @param[in] input, data to be encrypted
 * @param[out] output, encrypted data
 */
void aes_crypt(const uint8_t* input, uint8_t* output);

#endif /* AES_H */

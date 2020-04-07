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
 * ipi_calc.h
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#ifndef IPI_CALC_H
#define IPI_CALC_H

#include "cdn_stdint.h"

#define EMPTY_IPI {IPI_POSITIVE_VAL, 0U, NULL}

/** Maximum size allowed for an IPI (in number of limbs). */
#define CDN_IPI_MAX_LIMBS 10000U

/** Used as result of number comparing */
typedef enum {
    /* Left-hand equals right-hand */
    COMPARISON_RESULT_EQUAL = 0U,
    /* Left-hand is greater than right-hand */
    COMPARISON_RESULT_GREATER = 1U,
    /* Left-hand is lower than right-hand */
    COMPARISON_RESULT_LOWER = 2U
} ComparisonResult_t;

typedef enum {
    IPI_NEGATIVE_VAL,
    IPI_POSITIVE_VAL
} IpiSign_t;

/** IPI structure */
typedef struct {
    IpiSign_t sign;        /* Integer sign */
    uint16_t num_limbs; /* Total number of limbs */
    uint32_t* ptr;      /* Pointer to least significant limb */
} Ipi_t;


/**
 * Compare signed values
 * @param[in] X, Left-hand IPI
 * @param[in] Y, Right-hand IPI
 * @return COMPARISON_RESULT_GREATER if X > Y,
 *         COMPARISON_RESULT_EQUAL if X = Y,
 *         COMPARISON_RESULT_LOWER if X < Y
 */
ComparisonResult_t ipi_cmp(const Ipi_t* X, const Ipi_t* Y);

/**
 * Unallocate IPI
 * @param[in] src_ipi,  pointer to IPI to unallocate
 */
void ipi_free( Ipi_t *src_ipi );

/**
 * Import X from unsigned binary data, big endian
 * @param[out] dest_ipi, destination IPI
 * @param[in] buf, source buffer
 * @param[in] bufLen, source buffer size
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if memory allocation failed,
 *         CDN_EINVAL if input parameters are invalid
 */
uint32_t ipi_rd_binary(Ipi_t *dest_ipi, const uint8_t* srcBuf, uint32_t bufLen);

/**
 * Export X into unsigned binary data, big endian
 * @param[in] src_ipi, pointer to source IPI
 * @param[out] buf, pointer to destination buffer
 * @param[in] bufLen, destination buffer size
 * @return CDN_EOK if successful,
 *         CDN_EINVAL, if bufer isn't large enough
 */
uint32_t ipi_wr_binary( const Ipi_t *src_ipi, uint8_t* destBuf, uint32_t bufLen );

/**
 * Sliding-window exponentiation: dest_ipi = A^E mod N
 * @param[out] dest_ipi, pointer to destination IPI
 * @param[in] A, pointer to left-hand IPI
 * @param[in] E, pointer to exponent IPI
 * @param[in] N, pointer to modular IPI
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if memory allocation failed,
 *         CDN_EINVAL if N is negative or even or if E is negative
 *         CDN_EINPROGRESS if operation is not done yet
 */
uint32_t ipi_exp_mod( Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *E, const Ipi_t *N);

#endif /* IPI_CALC_H */

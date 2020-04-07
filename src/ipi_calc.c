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
 * ipi_calc.c
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

/* For specification of implementation please look to:
 * "Handbook of Applied Cryptography, by A. Menezes, P. van Oorschot, and S. Vanstone,
 *  CRC Press, 1996"
 */

/* parasoft-begin-suppress METRICS-36-3 "Function is called more than 5 different functions in translation unit", DRV-3823 */
/* parasoft-begin-suppress METRICS-41-3 "Number of block of comments per statement should be greater than 0.2", DRV-3849 */

#include "libHandler.h"
#include "ipi_calc.h"
#include "utils.h"
#include "cps_drv.h"
#include <stdlib.h>
#include "cdn_errno.h"
#include "cdn_stdtypes.h"

/* Number of Characters Per Limb */
#define CHARS_PER_LIMB 4U
/* Bits Per Limb  */
#define BITS_PER_LIMB (CHARS_PER_LIMB << 3)
/* Bits Per Half Limb */
#define BITS_PER_HALF_LIMB  (CHARS_PER_LIMB << 2)
/* Maximum value of one limb */
#define LIMB_MAX UINT32_MAX

/* Describes dimensions of matrix used during MUL operation */
#define MUL_MATRIX_DIMENSION_SIZE 2U

#define LIMB_TO_BIT_ITERATOR_MASK 0x00000001U

#define MONTGOMERY_INIT_MASK 0x00000004U

/** Pack of functions used as steps in exponential calculation process */
static uint32_t exp_prereq_calc_r2_handler(void);
static uint32_t exp_prereq_calc_mod_handler(void);
static uint32_t exp_prereq_calc_x_dash_handler(void);
static uint32_t exp_prereq_calc_a_handler(void);
static uint32_t calculate_g(void);
static uint32_t correct_exp_result(void);
static uint32_t do_sliding_window_exp(void);

/** Pack of functions used as steps in dividing process */

/** Realizes point 14.20.2 from Handbook of Applied Cryptography */
static uint32_t make_base_q(void);

static uint32_t do_divide_iteration(void);

typedef struct {
    /* Number of limbs to iterate */
    uint16_t limbs_number;
    /* Number of bits to iterate in current limb */
    uint8_t bit_number;
    /* Pointer to iterated data */
    const uint32_t* ipiPtr;
    /* If iteration was done */
    bool isDone;
} Limb2BitIterator_t;

typedef struct {
    /* Temporary ipi data */
    Ipi_t T;
    /* Copy of imput data */
    Ipi_t A;
    /* Temporary ipi data */
    Ipi_t Z;
    /* Pointer to result ipi */
    Ipi_t* ipiPtr;
    /* Pointer to modulus ipi */
    const Ipi_t* N;
    /* Ipi array used to calc helper window values */
    Ipi_t W[64];
    /* Flag if input ipi is negative or not */
    bool isNeg;
    /* Initialization value of Montgomery multiplication */
    uint32_t mm;
    /* Size of used window */
    uint8_t window_size;
    /* Iterator to go through exponent bits */
    Limb2BitIterator_t e_iterator;
} ExpModHelper_t;

typedef struct {
    /* Local copy of X */
    Ipi_t X;
    /* Local copy of Y */
    Ipi_t Y;
    /* Local copy of Q */
    Ipi_t Q;
    /* Temporary ipi */
    Ipi_t T1;
    /* Temporary ipi */
    Ipi_t T2;
    /* Current limb of X */
    uint16_t n;
    /* Current limb of Y */
    uint16_t t;
    /* Iteration counter */
    uint16_t i;
    /* Current align (based on last limb) */
    uint8_t align;
    /* Initialization flag */
    bool is_initialized;
} IpiDivHlp_t;

static ExpModHelper_t expModHlp;
static IpiDivHlp_t ipiDivHlp;

/**
 * Used to clean up data in ipi buffer
 * @param[in] ptr, pointer to ipi buffer
 * @param[in] size, size of data to clean
 */
static void ipi_buffer_cleanup(uint32_t* ptr, uint32_t size)
{
    /* Check if size to clean is greater than 0 */
    if (size > 0U) {
        (void)memset(ptr, 0, size);
    }
}

/**
 * Convert between bits and number of ipi digits(limbs).
 * @param[in] bits, numer of bits
 * @return number of limbs
 */
static inline uint32_t bits_to_limbs(uint32_t bits)
{
    uint32_t result = ((bits + BITS_PER_LIMB) - 1U) / BITS_PER_LIMB;
    return result;
}

/**
 * Convert between chars and number of ipi digits(limbs).
 * @param[in] characters, numer of used bytes (chars)
 * @return number of limbs
 */
static inline uint32_t chars_to_limbs(uint32_t characters)
{
    uint32_t result = ((characters + CHARS_PER_LIMB) - 1U) / CHARS_PER_LIMB;
    return result;
}

/**
 * Returns value of limbs which contain data
 * @param[in] A, pointer to Ipi
 * @return number of used limbs
 */
static inline uint16_t get_num_of_used_limbs(const Ipi_t* A)
{
    uint16_t usedLimbs;

    /* Break if first limb with 0's occured */
    for (usedLimbs = (A->num_limbs); usedLimbs > 0U; usedLimbs--) {
        if (A->ptr[usedLimbs -1U] != 0U) {
            break;
        }
    }

    return usedLimbs;
}

static inline ComparisonResult_t check_left_hand(IpiSign_t lh_sign) {

    ComparisonResult_t result = COMPARISON_RESULT_LOWER;

    if (lh_sign == IPI_POSITIVE_VAL) {
        result = COMPARISON_RESULT_GREATER;
    }

    return result;
}

static inline ComparisonResult_t check_right_hand(IpiSign_t rh_sign) {

    ComparisonResult_t result = COMPARISON_RESULT_LOWER;

    if (rh_sign == IPI_NEGATIVE_VAL) {
        result = COMPARISON_RESULT_GREATER;
    }

    return result;
}

/**
 * Compare beetween specified number of two Ipis limbs
 * @param[in] X, first Ipi to compare
 * @param[in] Y, second Ipi to compare
 * @param[in] numOfLimbs, number of limbs to check
 * @param[in] isAbs, if input Ipis are significant or absolute
 * @return COMPARISON_RESULT_GREATER if X > Y,
 *         COMPARISON_RESULT_EQUAL if X = Y,
 *         COMPARISON_RESULT_LOWER if X < Y
 */
static inline ComparisonResult_t ipi_cmp_limbs(const Ipi_t* X, const Ipi_t* Y, uint32_t numOfLimbs, bool isAbs)
{
    /* Assumption that IPI's are same */
    ComparisonResult_t retVal = COMPARISON_RESULT_EQUAL;

    uint32_t i;

    for (i = numOfLimbs; i > 0U; i--) {

        if (X->ptr[i - 1U] > Y->ptr[i - 1U]) {
            retVal = isAbs ? COMPARISON_RESULT_GREATER : check_left_hand(X->sign);
        } else if (Y->ptr[i - 1U] > X->ptr[i - 1U]) {
            retVal = isAbs ? COMPARISON_RESULT_LOWER : check_right_hand(Y->sign);
        } else {
            /* If limbs are same, check next */
            continue;
        }

        /* Stop checking if difference was found */
        break;
    }

    return retVal;
}

static inline ComparisonResult_t compare_with_different_signs(IpiSign_t xSign) {
    ComparisonResult_t result = check_left_hand(xSign);
    return result;
}

static inline ComparisonResult_t compare_with_same_signs(const Ipi_t* X, const Ipi_t* Y) {

    ComparisonResult_t retVal;

    uint16_t x_limbs = get_num_of_used_limbs(X);
    uint16_t y_limbs = get_num_of_used_limbs(Y);

    if ((x_limbs == 0U) && (y_limbs == 0U)) {
        retVal = COMPARISON_RESULT_EQUAL;
    } else if (x_limbs > y_limbs) {
        retVal = check_left_hand(X->sign);
    } else if (y_limbs > x_limbs){
        retVal = check_right_hand(Y->sign);
    } else {
        /* If here, x_limbs and y_limbs are same */
        retVal = ipi_cmp_limbs(X, Y, x_limbs, false);
    }

    return retVal;
}

static inline bool check_if_signs_different(const Ipi_t* X, const Ipi_t* Y) {
    bool result = X->sign != Y->sign;
    return result;
}

static inline bool check_if_signs_same(const Ipi_t* X, const Ipi_t* Y) {
    bool result = X->sign == Y->sign;
    return result;
}

static inline bool check_if_signs_positive(const Ipi_t* X, const Ipi_t* Y) {
    bool result = ((X->sign == IPI_POSITIVE_VAL) && (Y->sign == IPI_POSITIVE_VAL));
    return result;
}

/**
 * Compare signed values (between two Ipis)
 * @param[in] X, Left-hand Ipi
 * @param[in] Y, Right-hand Ipi
 * @return COMPARISON_RESULT_GREATER if X > Y,
 *         COMPARISON_RESULT_EQUAL if X = Y,
 *         COMPARISON_RESULT_LOWER if X < Y
 */
ComparisonResult_t ipi_cmp(const Ipi_t* X, const Ipi_t* Y)
{
    ComparisonResult_t retVal;

    bool diff_signs = check_if_signs_different(X, Y);

    if (diff_signs) {
        retVal = compare_with_different_signs(X->sign);
    } else {
        retVal = compare_with_same_signs(X, Y);
    }

    return retVal;
}

/**
 * Compare unsigned values (between two Ipis)
 * @param[in] X, Left-hand Ipi
 * @param[in] Y, Right-hand Ipi
 * @return COMPARISON_RESULT_GREATER if X > Y,
 *         COMPARISON_RESULT_EQUAL if X = Y,
 *         COMPARISON_RESULT_LOWER if X < Y
 */
static ComparisonResult_t ipi_cmp_abs( const Ipi_t *X, const Ipi_t *Y )
{
    uint32_t x_limbs, y_limbs;
    ComparisonResult_t retVal;

    x_limbs = get_num_of_used_limbs(X);
    y_limbs = get_num_of_used_limbs(Y);

    if((x_limbs == 0U) && (y_limbs == 0U)) {
        retVal = COMPARISON_RESULT_EQUAL;
    } else if (x_limbs > y_limbs) {
        retVal = COMPARISON_RESULT_GREATER;
    } else if (y_limbs > x_limbs) {
        retVal = COMPARISON_RESULT_LOWER;
    } else {
        /* If here, x_limbs and y_limbs are same */
        retVal = ipi_cmp_limbs(X, Y, x_limbs, true);
    }

    return retVal;
}

/**
 * Compare signed values if Ipi and integer
 * @param[in] X, left-hand IPI
 * @param[in] z, the integer value to compare to
 * @return COMPARISON_RESULT_GREATER if X > z,
 *         COMPARISON_RESULT_EQUAL if X = z,
 *         COMPARISON_RESULT_LOWER if X < z
 */
static ComparisonResult_t ipi_cmp_int( const Ipi_t* X, int32_t z)
{
    Ipi_t Y;
    uint32_t p;

    if (z < 0) {
        p = (uint32_t)(-z);
        Y.sign = IPI_NEGATIVE_VAL;
    } else {
        p = (uint32_t)z;
        Y.sign = IPI_POSITIVE_VAL;
    }

    Y.num_limbs = 1U;
    Y.ptr = &p;

    return ipi_cmp(X, &Y);
}

/**
 * Initialize one Ipi
 * @param[in] src_ipi, pointer to Ipi to initialize.
 */
static void ipi_init(Ipi_t* src_ipi)
{
    src_ipi->sign = IPI_POSITIVE_VAL;
    src_ipi->num_limbs = 0U;
    src_ipi->ptr = NULL;
}

/**
 * Unallocate one IPI
 * @param src_ipi, pointer to Ipi to unallocate.
 */
void ipi_free( Ipi_t *src_ipi )
{
    if (src_ipi != NULL) {

        if( src_ipi->ptr != NULL ) {
            MEM_free(src_ipi->ptr);
        }

        ipi_init(src_ipi);
    }
}

/**
 * Expand the Ipi by attaching the specified number(nblimbs) of limbs
 * @param[in, out] src_ipi, IPI to grow
 * @param[in] nblimbs, target number of limbs
 * @return CDN_EOK if successful,
 *         CDN_EINVAL if to many limbs needed,
 *         CDN_ENOMEM if not enough memory
 */
static uint32_t ipi_grow(Ipi_t* seed_ipi, uint16_t nblimbs)
{
    uint32_t* p;
    uint32_t retVal = CDN_EOK;
    uint16_t size;

    if((nblimbs == 0U) || (nblimbs > CDN_IPI_MAX_LIMBS)) {
        retVal =  CDN_EINVAL;
    }

    if (retVal == CDN_EOK) {

        if( seed_ipi->num_limbs < nblimbs ) {

            /* Allocate memory for expanded Ipi */
            size = nblimbs * CHARS_PER_LIMB;
            p = MEM_malloc(size);

            if (p == NULL) {
                /* Not enough memory for expanded Ipi */
                retVal = CDN_ENOMEM;
            } else {
                ipi_buffer_cleanup(p, size);

                if( seed_ipi->ptr != NULL ) {
                    /* Copy data from Ipi to new one and free old */
                    size = seed_ipi->num_limbs * CHARS_PER_LIMB;
                    CPS_BufferCopy((uint8_t*)p, (uint8_t*)seed_ipi->ptr, size);
                    MEM_free(seed_ipi->ptr);
                }

                seed_ipi->num_limbs = nblimbs;
                seed_ipi->ptr = p;
            }
        }
    }

    return retVal;
}

/*
 * Return the number of used bits for all limbs
 * @param[in] src_ipi, pointer to Ipi
 * @return number of used bits by Ipi
 */
static uint32_t ipi_msb_bitsnum(const Ipi_t* src_ipi)
{
    uint16_t x_last_limb = get_num_of_used_limbs(src_ipi) - 1U;
    uint8_t bitNum;
    uint32_t mask;

    /* Break when first bit from MSB side is not 0 */
    for (bitNum = (uint8_t)BITS_PER_LIMB; bitNum > 0U; bitNum--) {
        mask = safe_shift32l(1U, (bitNum - 1U));
        if ((src_ipi->ptr[x_last_limb] & mask) != 0U) {
            break;
        }
    }

    return ((uint32_t)x_last_limb * BITS_PER_LIMB) + (uint32_t)bitNum;
}

/*
 * Copy the contents of src_ipi into dest_ipi
 * @param[in] dest_ipi, pointer to destination Ipi
 * @param[in] src_ipi, pointer to source Ipi
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if not enough memory
 */
static uint32_t ipi_copy(Ipi_t* dest_ipi, const Ipi_t* src_ipi )
{
    uint32_t retVal = CDN_EOK;
    uint16_t i;

    if (dest_ipi != src_ipi) {

        if (src_ipi->ptr == NULL) {
            ipi_free( dest_ipi );
            retVal = CDN_EOK;
        } else {

            i = get_num_of_used_limbs(src_ipi);
            retVal = ipi_grow(dest_ipi, i);

            if (retVal == CDN_EOK) {
                dest_ipi->sign = src_ipi->sign;
                ipi_buffer_cleanup(dest_ipi->ptr, ((uint32_t)dest_ipi->num_limbs * CHARS_PER_LIMB));

                /* Check if data to copy */
                if (i != 0U) {
                    CPS_BufferCopy((uint8_t*)dest_ipi->ptr, (uint8_t*)src_ipi->ptr, ((uint32_t)i * CHARS_PER_LIMB));
                }
            }
        }

    }

    return retVal;
}

/**
 * Set value from integer
 * @param[out] dest_ipi, IPI to set
 * @param[in] z, literal value to use
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if memory allocation failed
 */
static uint32_t ipi_lset(Ipi_t *dest_ipi, int32_t z)
{
    uint32_t retVal;

    retVal = ipi_grow(dest_ipi, 1);

    if (retVal == CDN_EOK) {

        /* Clean up Ipi */
        ipi_buffer_cleanup(dest_ipi->ptr, ((uint32_t)dest_ipi->num_limbs * CHARS_PER_LIMB));

        /* Set sign and value of Ipi */
        if (z < 0) {
            dest_ipi->ptr[0] = (uint32_t)(-z);
            dest_ipi->sign = IPI_NEGATIVE_VAL;
        } else {
            dest_ipi->ptr[0] = (uint32_t)z;
            dest_ipi->sign = IPI_POSITIVE_VAL;
        }
    }

    return  retVal;
}

/**
 * Used to grow-up Ipi and set 0 value
 * @param[in] ipiPtr, pointer to ipi structure
 * @param[in] size, expected size of growed ipi
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enough memory
*/
static uint32_t ipi_setup(Ipi_t* ipiPtr, uint16_t size) {

    uint32_t retVal = ipi_grow(ipiPtr, size);

    /* Set-up dest_ipi initial value as 0 */
    if (retVal == CDN_EOK) {
        retVal = ipi_lset(ipiPtr, 0);
    }

    return retVal;
}


/*
 * Do a left-shift on ipi, src_ipi <<= count
 * @param[in] src_ipi, pointer to shifted Ipi
 * @param[in] count, number of bits to shift
 * @return Ipi after shifting
 */
static uint32_t ipi_shift_l(Ipi_t* src_ipi, uint32_t count )
{
    uint32_t retVal = CDN_EINVAL;
    uint32_t* limb;
    /* Number of used bits after shift */
    uint32_t newBitsNum;
    /* Number of full limbs to shift */
    uint32_t limbsToShift;
    /* Number of bits to shift (without full limbs) */
    uint8_t bitsToShift;
    uint32_t carry;
    uint32_t temp;
    uint32_t i;

    limbsToShift = count / BITS_PER_LIMB;
    bitsToShift  = (uint8_t)count % BITS_PER_LIMB;

    newBitsNum = ipi_msb_bitsnum( src_ipi ) + count;

    if (newBitsNum <= (CDN_IPI_MAX_LIMBS * BITS_PER_LIMB)) {
        retVal = ipi_grow(src_ipi, (uint16_t)bits_to_limbs(newBitsNum));
    }

    if (retVal == CDN_EOK) {

        limb = src_ipi->ptr;

        if (limbsToShift > 0U) {
            /* Shift full limbs to left */
            for (i = (src_ipi->num_limbs); i > limbsToShift; i--) {
                limb[i - 1U] = limb[i - limbsToShift - 1U];
            }

            /* Fill rest of limbs with 0s */
            for ( ; i > 0U; i--) {
                limb[i - 1U] = 0U;
            }
        }

        if( bitsToShift > 0U ) {
                carry = 0U;

                /* Shift bits starting from 'limbsToShfit, because bits on left are 0s */
                for (i = limbsToShift; i < src_ipi->num_limbs; i++) {
                    temp = safe_shift32r(limb[i], (BITS_PER_LIMB - bitsToShift));
                    limb[i] = safe_shift32l(limb[i], bitsToShift);
                    limb[i] |= carry;
                    carry = temp;
                }
            }
    }

    return retVal;
}

/*
 * Do a right-shift on ipi, src_ipi >>= count
 * @param[in] src_ipi, pointer to shifted Ipi
 * @param[in] count, number of bits to shift
 * @return Ipi after shifting
 */
static uint32_t ipi_shift_r( Ipi_t *src_ipi, uint32_t count )
{
    /* Number of full limbs to shift */
    uint32_t limbsToShift;
    /* Number of bits to shift (without full limbs) */
    uint8_t bitsToShift;
    /* Number of source limbs */
    uint16_t number_of_limbs = src_ipi->num_limbs;
    /* Pointer to used limb when bit shifting is done */
    uint32_t* used_limb;

    uint32_t i;
    uint32_t carry = 0U;
    uint32_t temp;
    uint32_t retVal = CDN_EOK;

    limbsToShift = count / BITS_PER_LIMB;
    bitsToShift = (uint8_t)count % BITS_PER_LIMB;

    if (limbsToShift >= number_of_limbs) {
        retVal = ipi_lset(src_ipi, 0);
    } else {
        /* Shift-right full limbs */
        if (limbsToShift > 0U) {

            for (i = 0U; i < (number_of_limbs - limbsToShift); i++) {
                src_ipi->ptr[i] = src_ipi->ptr[i + limbsToShift];
            }

            for ( ; i < number_of_limbs; i++) {
                /* Fill rest of limbs with 0s */
                src_ipi->ptr[i] = 0;
            }
        }

        /* Shift-right bits */
        if (bitsToShift > 0U) {

            for (i = number_of_limbs; i > 0U; i--) {
                used_limb = &src_ipi->ptr[i - 1U];

                temp = safe_shift32l(*used_limb, (BITS_PER_LIMB - bitsToShift));
                *used_limb = safe_shift32r(*used_limb, bitsToShift);
                *used_limb |= carry;
                carry = temp;
            }

        }
    }

    return retVal;
}

/*
 * Helper for ipi subtraction
 * @param[in] limbNum, number of limbs to calculate
 * @param[in] subtrahed, pointer to subtrahend
 * @param[in] minuend, pointer to minuend
 */
static void ipi_sub_hlp(uint16_t limbNum, const uint32_t* subtrahend, uint32_t* minuend)
{
    uint16_t i;
    uint32_t carry = 0U;
    uint32_t z;

    /* Make operations of each limb */
    for (i = 0U; i < limbNum; i++) {
        z = bool_to_uint(minuend[i] <  carry);
        minuend[i] -=  carry;

        carry = bool_to_uint(minuend[i] < subtrahend[i]) + z;
        minuend[i] -= subtrahend[i];
    }

    /* Correct result */
    while( carry != 0U ) {
        z = bool_to_uint(minuend[i] < carry);
        minuend[i] -= carry;
        carry = z;
        i++;
    }
}

/*
 * Unsigned subtraction: dest_ipi = |A| - |B|  (HAC 14.9)
 * @param[in] dest_ipi, pointer Ipi with result
 * @param[in] A, pointer to dividend
 * @param[in] B, pointer to divider
 * @result CDN_EOK if success,
 *         CDN_ENOMEM if not memory to make operation,
 *         CDN_EINVAL if A < B (result should be lesser than 0)
 */
static uint32_t ipi_sub_abs( Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *B )
{
    Ipi_t tempB;
    uint32_t retVal;
    uint16_t numLimbs;
    bool if_b_is_dest = (B == dest_ipi);

    ipi_init(&tempB);

    if (ipi_cmp_abs(A, B) == COMPARISON_RESULT_LOWER) {
        retVal = CDN_EINVAL;
    } else {
        /* Make copy of B to avoid overwritten when B is destination */
        retVal = ipi_copy(&tempB, B);
    }

    if ((retVal == CDN_EOK) && (dest_ipi != A)) {
        retVal = ipi_copy(dest_ipi, A);
    }

    if (retVal == CDN_EOK) {

        /* Dest_ipi should always be positive as a result of unsigned subtractions. */
        dest_ipi->sign = IPI_POSITIVE_VAL;

        numLimbs = get_num_of_used_limbs(&tempB);

        ipi_sub_hlp(numLimbs, tempB.ptr, dest_ipi->ptr );
    }

    /* If extra memory space was used, free it */
    ipi_free(&tempB);

    return retVal;
}

/*
 * Helper for ipi addition
 * @param[in] dest_ipi, pointer to result/first factor
 * @param[in] X, pointer to second factor
 * @param[in] limbNum, number of limbs to calculate
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enough memory to make operation
 */
static uint32_t ipi_add_hlp(Ipi_t* dest_ipi, const Ipi_t* X, uint16_t usedLimbs)
{
    uint32_t retVal = CDN_EOK;
    uint16_t i;
    uint32_t carry = 0U;
    uint32_t tmp;

    for (i = 0U; i < usedLimbs; i++) {
        /* Used because it might happen that dest_ipi == X */
        tmp = X->ptr[i];
        dest_ipi->ptr[i] +=  carry;
        carry  = bool_to_uint(dest_ipi->ptr[i] < carry);
        dest_ipi->ptr[i] += tmp;
        carry += bool_to_uint(dest_ipi->ptr[i] < tmp);
    }

    /* Correct result */
    while(carry != 0U)
    {
        if(i >= dest_ipi->num_limbs) {

            retVal = ipi_grow(dest_ipi, i + 1U);

            if (retVal != CDN_EOK) {
                break;
            }
        }

        dest_ipi->ptr[i] += carry;
        carry = bool_to_uint(dest_ipi->ptr[i] < carry);
        i++;
    }

    return retVal;
}
/*
 * Unsigned addition: dest_ipi = |A| + |B|  (HAC 14.7)
 */
static uint32_t ipi_add_abs(Ipi_t *dest_ipi, const Ipi_t* A, const Ipi_t* B)
{
    const Ipi_t* X;
    /* Number of limbs which are not 0s */
    uint16_t usedLimbs;
    uint32_t i;

    uint32_t retVal = CDN_EOK;

    /* Short add is made (dest += X) so data prepare is needed */
    if (dest_ipi == B) {
        /* dest += A */
        X = A;
    } else if (dest_ipi == A) {
        /* dest += B */
        X = B;
    } else {
        /* dest = A + B */
        X = B;
        retVal = ipi_copy(dest_ipi, A);
    }

    if (retVal == CDN_EOK) {

        /* Dest_ipi should always be positive as a result of unsigned additions */
        dest_ipi->sign = IPI_POSITIVE_VAL;

        usedLimbs = get_num_of_used_limbs(X);

        retVal = ipi_grow(dest_ipi, usedLimbs);
    }

    if (retVal == CDN_EOK) {
        retVal = ipi_add_hlp(dest_ipi, X, usedLimbs);
    }

    return retVal;
}

/**
 * Signed addition: dest_ipi = A + B
 * @param[in] dest_ipi, pointer to destination Ipi
 * @param[in] A, pointer to Ipi (first factor)
 * @param[in] B, pointer to Ipi (second factor)
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enoguh memory to make operation
 */
static uint32_t ipi_add( Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *B )
{
    uint32_t retVal;

    bool diff_signs = check_if_signs_different(A, B);

    /* Do correct operation due to signs of input Ipis */
    if (diff_signs) {
        if (ipi_cmp_abs(A, B) != COMPARISON_RESULT_LOWER) {
            retVal = ipi_sub_abs(dest_ipi, A, B);
            dest_ipi->sign =  A->sign;
        }
        else {
            retVal = ipi_sub_abs(dest_ipi, B, A);
            dest_ipi->sign = B->sign;
        }
    } else {
        retVal = ipi_add_abs(dest_ipi, A, B);
        dest_ipi->sign = A->sign;
    }

    return retVal;
}

/**
 * Signed subtraction: dest_ipi = A - B
 * @param[in] dest_ipi Destination IPI
 * @param[in] A, pointer to Ipi (first factor)
 * @param[in] B, pointer to Ipi (second factor)
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enoguh memory to make operation
 */
static uint32_t ipi_sub( Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *B )
{
    uint32_t retVal;

    bool pos_signs = check_if_signs_positive(A, B);

    /* Do correct operation due to signs of input Ipis */
    if (pos_signs) {
        if (ipi_cmp_abs(A, B) != COMPARISON_RESULT_LOWER) {
            retVal = ipi_sub_abs(dest_ipi, A, B);
            dest_ipi->sign =  IPI_POSITIVE_VAL;
        }
        else {
            retVal = ipi_sub_abs(dest_ipi, B, A);
            dest_ipi->sign = IPI_NEGATIVE_VAL;
        }
    } else {
        retVal = ipi_sub_abs(dest_ipi, A, B);
        dest_ipi->sign = A->sign;
    }

    return retVal;
}

/*
 * Return the total size in bytes
 * @param[in] src_ipi, pointer to Ipi
 * @return total number of used bytes by Ipi
 */
static uint32_t ipi_size(const Ipi_t *src_ipi)
{
    uint32_t totalBitNum = ipi_msb_bitsnum(src_ipi);
    uint32_t totalByteNum = totalBitNum / NUMBER_OF_BITS_IN_BYTE;

    if ((totalBitNum % NUMBER_OF_BITS_IN_BYTE) != 0U) {
        totalByteNum++;
    }

    return totalByteNum;
}

uint32_t ipi_rd_binary(Ipi_t *dest_ipi, const uint8_t* srcBuf, uint32_t bufLen)
{
    uint32_t retVal;
    uint32_t full_limbs;
    uint32_t number_of_bytes_in_part_limb;
    uint32_t i;
    uint32_t index;
    uint32_t number_of_limbs;
    uint32_t bytes_to_copy;

    /* Remove trailing 0s */
    for (i = 0U; i < bufLen; i++) {
        if (srcBuf[i] != 0U) {
            break;
        }
    }

    /* Number of bytes in source buffer without trailing 0s */
    bytes_to_copy = bufLen - i;

    number_of_limbs = chars_to_limbs(bytes_to_copy);

    if (number_of_limbs < CDN_IPI_MAX_LIMBS) {
        retVal = ipi_setup(dest_ipi, (uint16_t)number_of_limbs);
    } else {
        retVal = CDN_EINVAL;
    }

    if (retVal == CDN_EOK) {
        number_of_bytes_in_part_limb = bytes_to_copy % NUMBER_OF_BYTES_IN_UINT32T;
        index = bufLen;

        /* number of full limbs is (bytes_to_copy / NUMBER_OF_BYTES_IN_UINT32T) */
        for (i = 0U; i < (bytes_to_copy / NUMBER_OF_BYTES_IN_UINT32T); i++) {
            index -= NUMBER_OF_BYTES_IN_UINT32T;
            dest_ipi->ptr[i] = getBe32(&srcBuf[index]);
        }

        /* Still remaining data to read */
        if (number_of_bytes_in_part_limb != 0U) {
            index -= number_of_bytes_in_part_limb;

            if (number_of_bytes_in_part_limb == 3U) {
                dest_ipi->ptr[i] = getBe24(&srcBuf[index]);
            } else if (number_of_bytes_in_part_limb == 2U) {
                dest_ipi->ptr[i] = (uint32_t)getBe16(&srcBuf[index]);
            } else {
                /* For index = 1U */
                dest_ipi->ptr[i] = (uint32_t)srcBuf[index];
            }
        }
    }

    return retVal;
}

/**
 * Sanity function for binary write operation
 * @param[in] src_ipi, pointer to Ipi
 * @param[in] destBuf, pointer to destination byte buffer
 * @return CDN_EOK if success,
 *         CDN_EINVAL if input parameters are invalid
 */
static uint32_t ipi_wr_binary_SF(const Ipi_t* src_ipi, const uint8_t* destBuf)
{
    uint32_t retVal = CDN_EOK;

    /* Check source Ipi */
    if ((src_ipi == NULL) || (src_ipi->ptr == NULL)) {
        retVal = CDN_EINVAL;
    }

    if (destBuf == NULL) {
        retVal = CDN_EINVAL;
    }

    return retVal;
}


/**
 * Helper function used to write rest of data (limb which are not full used)
 * @param[in] src, pointer to rest of data
 * @param[in] dest, pointer to buffer where rest of data should be written
 * @param[in] partialLimbs, number of pending limbs
 */
static inline void write_binary_partial(const uint32_t* src, uint8_t* dest, uint32_t partialLimbs)
{
    if (partialLimbs == 3U) {
        setBe24(*src, dest);
    } else if (partialLimbs == 2U) {
        setBe16((uint16_t)*src, dest);
    } else {
        /* For partialLimbs equals 1U */
        *dest = (uint8_t)*src;
    }
}

uint32_t ipi_wr_binary (const Ipi_t* src_ipi, uint8_t* destBuf, uint32_t bufLen)
{
    uint32_t ipiSize;
    uint32_t retVal;
    uint32_t fullLimbs;
    uint32_t partialLimbs;
    uint32_t index;
    uint32_t i;
    uint32_t* data;

    retVal = ipi_wr_binary_SF(src_ipi, destBuf);

    if (retVal == CDN_EOK) {

        ipiSize = ipi_size(src_ipi);

        if (bufLen < ipiSize) {
            retVal = CDN_EINVAL;
        }
    }

    if (retVal == CDN_EOK) {

        for (i = 0U; i < bufLen; i++) {
            destBuf[i] = 0U;
        }

        fullLimbs = ipiSize / CHARS_PER_LIMB;
        partialLimbs = ipiSize % CHARS_PER_LIMB;
        index = bufLen;

        data = src_ipi->ptr;

        for (i = 0U; i < fullLimbs; i++) {
            index -= CHARS_PER_LIMB;
            setBe32(data[i], &destBuf[index]);
        }

        /* Still data remaining to write */
        if (partialLimbs != 0U) {
            index -= partialLimbs;
            write_binary_partial(&data[i], &destBuf[index], partialLimbs);
        }
    }

    return retVal;
}

/*
 * Fast Montgomery initialization (thanks to Tom St Denis)
 * @param[out] mm, pointer to Montgomery specific value
 * @param[in] N, pointer to modulus Ipi
 */
static void ipi_montg_init(uint32_t* mm, const Ipi_t* N)
{
    uint32_t x;
    uint32_t m0 = N->ptr[0];

    x  = m0;
    x += safe_shift32l(((m0 + 2U) & MONTGOMERY_INIT_MASK), 1U);

    /* Bits per limb is const, so 3 mul is needed */
    x *= ( 2U - ( m0 * x ) );
    x *= ( 2U - ( m0 * x ) );
    x *= ( 2U - ( m0 * x ) );

    *mm = ~x + 1U;
}

/**
 * Used to convert double word to array with words
 * @param[in] multiplicant, double word value
 * @param[out] multiplicand_as_words, pointer to word array
 */
static inline void mulladdc_init(uint32_t* multiplicand_as_words, uint32_t multiplicand)
{
    multiplicand_as_words[0] = GetWord0(multiplicand);
    multiplicand_as_words[1] = GetWord1(multiplicand);
}

/**
 * Helper fucntion used to update r_matrix during multiplication
 * @param[in, out] r_matrix, pointer to multiplication helper matrix
 * @param[in] val, calculated value, based on which update is made
 * */
static inline void calc_r_matrix_hlp(uint32_t r_matrix[2][2], uint32_t val)
{
    r_matrix[0][0] += val;

    if (r_matrix[0][0] < val) {
        r_matrix[1][1]++;
    }

}

/**
 * Calculates Hadamard matrix multiplication
 * @param[in] A, left-hand Ipi
 * @param[in] B, right-hand Ipi
 * @param[in] result, result of multiplication
 */
/* parasoft-begin-suppress METRICS-39-3, "Too high value of VOCF metric", DRV-5191 */
static inline void mul_matrix_by_coordinates(const uint32_t A[MUL_MATRIX_DIMENSION_SIZE],
                                             const uint32_t B[MUL_MATRIX_DIMENSION_SIZE],
                                             uint32_t result[MUL_MATRIX_DIMENSION_SIZE][MUL_MATRIX_DIMENSION_SIZE]) {
    /* Helper matrix dimensions: 2x2 */
    result[0][0] = A[0] * B[0];
    result[0][1] = A[0] * B[1];
    result[1][0] = A[1] * B[0];
    result[1][1] = A[1] * B[1];
}
/* parasoft-end-suppress METRICS-39-3 */

/**
 * Used to calcuate r_matrix. After one calc in r_matrix are needed results for one step
 * @param[out] r_matrix, calculated matrix,
 * @param[in] multiplicand, specified two words of left-hand Ipi
 * @param[in] multiplier, specified two words of right-hand Ipi
 * @param[out] carry, calculated calue of carriage
 * @param[in] val, pointer to calculated limb
 */
static inline void calc_r_matrix(uint32_t r_matrix[MUL_MATRIX_DIMENSION_SIZE][MUL_MATRIX_DIMENSION_SIZE],
                                 const uint32_t multiplicand[MUL_MATRIX_DIMENSION_SIZE],
                                 const uint32_t multiplier[MUL_MATRIX_DIMENSION_SIZE],
                                 uint32_t carry,
                                 uint32_t val) {
    /* Matrix [2][2]:
       -------
       |r0|rx|
       -------
       |ry|r1|
       ------- */

    mul_matrix_by_coordinates(multiplier, multiplicand, r_matrix);

    // r1 = s1 * b1 + ( rx >> BITS_PER_HALF_LIMB ) + ( ry >> BITS_PER_HALF_LIMB )
    r_matrix[1][1] += (uint32_t)GetWord1(r_matrix[0][1])
                   +  (uint32_t)GetWord1(r_matrix[1][0]);

    // r0 += rx r1 += (r0 < rx)
    calc_r_matrix_hlp(r_matrix, safe_shift32l(r_matrix[0][1],  BITS_PER_HALF_LIMB));

    // r0 += ry  r1 += (r0 < ry)
    calc_r_matrix_hlp(r_matrix, safe_shift32l(r_matrix[1][0],  BITS_PER_HALF_LIMB));

    // r0 +=  c  r1 += (r0 <  c)
    calc_r_matrix_hlp(r_matrix, carry);

    //r0 += *d; r1 += (r0 < *d)
    calc_r_matrix_hlp(r_matrix, val);

}

/**
 * Used to correct multiplication procedure
 * @param[in] result, double pointer to current result limb
 * @param[in] carry, value of carriage before finish
 */
static inline void mulladdc_finish(uint32_t** result, uint32_t carry)
{
    uint32_t l_carry = carry;

    do {
        **result += l_carry;
        l_carry = bool_to_uint(**result < l_carry);
        (*result)++;
    } while (l_carry != 0U);
}

/**
 * Core of multiplication procedure
 * @param[in] multiplier, double pointer to multiplier value
 * @param[in] multiplicand_as_words, value of used multiplicand limbs cut into words
 * @param[in] carry, pointer to carraige
 * @param[in, out] dest, double pointer to result buffer
 * @param[in] index, number of iterations in one step
 * */
static void mulladdc_core(uint32_t** multiplier, const uint32_t multiplicand_as_words[2], uint32_t* carry, uint32_t** dest, uint32_t index)
{
    /* Function is called many times, static to avoid allocate/deallocate each time */
    static uint32_t multiplier_as_words[2];
    static uint32_t r_help_matrix[2][2];
    uint32_t i;

    for (i = 0U; i < index; i++) {
        // s0 = ( *s << BITS_PER_HALF_LIMB ) >> BITS_PER_HALF_LIMB
        multiplier_as_words[0] = GetWord0(**multiplier);
        // s1 = ( *s >> BITS_PER_HALF_LIMB )
        multiplier_as_words[1] = GetWord1(**multiplier);

        calc_r_matrix(r_help_matrix, multiplicand_as_words, multiplier_as_words, *carry, **dest);

        // c = r1
        *carry = r_help_matrix[1][1];
        // *(d++) = r0;
        **dest = r_help_matrix[0][0];

        (*dest)++;
        (*multiplier)++;
    }
}

/**
 * Helper function used during multiplication
 * @param[in] i, number of iterations to do
 * @param[in] multiplier, pointer right-hand Ipi buffer
 * @param[out] result, pointer to buffer with result
 * @param[in] multiplicand, pointer to buffer with left-hand Ipi buffer
 */
static void ipi_mul_hlp(uint32_t i, uint32_t* multiplier /* *s*/, uint32_t* result /* *d */, uint32_t multiplicand /* b */)
{
    uint32_t multiplicand_as_words[2];
    uint32_t carry = 0U;
    uint32_t index = i;

    /* Start multiplication */
    mulladdc_init(multiplicand_as_words, multiplicand);

    /* Make iterations as long as i will be 0 */
    for( ; index >= 16U; index -= 16U ) {
        mulladdc_core(&multiplier, multiplicand_as_words, &carry, &result, 16U);
    }

    for( ; index >= 8U; index -= 8U ) {
        mulladdc_core(&multiplier, multiplicand_as_words, &carry, &result, 8U);
    }

    for( ; index > 0U; index-- ) {
        mulladdc_core(&multiplier, multiplicand_as_words, &carry, &result, 1U);
    }

    /* Finish multiplication */
    mulladdc_finish(&result, carry);
}


/**
 * Baseline multiplication: dest_ipi = A * B
 * @param dest_ipi, pointer to destination IPI
 * @param A, pointer to left-hand IPI
 * @param B, pointer to right-hand IPI
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if not enough memory to make operation
 */
static uint32_t ipi_mul( Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *B )
{
    uint32_t retVal = CDN_EOK;
    uint16_t a_used_limbs, b_used_limbs;
    bool same_signs;
    Ipi_t m1;
    Ipi_t m2;

    ipi_init(&m1);
    ipi_init(&m2);

    /* Make copy of input data to avoid overwritten if input is also output */
    retVal  = ipi_copy(&m1, A);
    retVal |= ipi_copy(&m2, B);

    if (retVal == CDN_EOK) {

        a_used_limbs = get_num_of_used_limbs(&m1);
        b_used_limbs = get_num_of_used_limbs(&m2);

        retVal = ipi_setup(dest_ipi, (a_used_limbs + b_used_limbs));
    }

    if (retVal == CDN_EOK) {

        /* Iterate through all B limbs */
        while (b_used_limbs > 0U) {
            ipi_mul_hlp(a_used_limbs, m1.ptr, &dest_ipi->ptr[b_used_limbs - 1U], m2.ptr[b_used_limbs - 1U] );
            b_used_limbs--;
        }

        same_signs = check_if_signs_same(&m1, &m2);

        if (same_signs) {
            dest_ipi->sign = IPI_POSITIVE_VAL;
        } else {
            dest_ipi->sign = IPI_NEGATIVE_VAL;
        }
    }

    /* Free memory reserved for input Ipi copy */
    ipi_free(&m1);
    ipi_free(&m2);

    return retVal;
}

/**
 * Multiplication of Ipi and integer: dest_ipi = A * b
 * @param dest_ipi, pointer to destination IPI
 * @param A, pointer to left-hand IPI
 * @param b, integer value (right-hand)
 * @return CDN_EOK if successful,
 *         CDN_ENOMEM if not enough memory to make operation
 */
static uint32_t ipi_mul_uint(Ipi_t* dest_ipi, const Ipi_t* A, uint32_t b)
{
    uint32_t retVal;

    Ipi_t uintAsIpi;

    ipi_init(&uintAsIpi);

    retVal = ipi_grow(&uintAsIpi, 1);

    if (retVal == CDN_EOK) {

        uintAsIpi.ptr[0] = b;
        retVal = ipi_mul(dest_ipi, A, &uintAsIpi);
    }

    return retVal;
}

/**
 * Cleanup divide helper structure. Call only after end of procedure
 */
static void cleanup_ipi_div_helper(void)
{
    ipi_free(&ipiDivHlp.X);
    ipi_free(&ipiDivHlp.Y);
    ipi_free(&ipiDivHlp.T1);
    ipi_free(&ipiDivHlp.T2);
    ipi_free(&ipiDivHlp.Q);

    ipiDivHlp.is_initialized = false;
}

/**
 * Calc align and shiftu input data. Used during division process
 * @param[in] X, pointer to left-side Ipi
 * @param[in] Y, pointer to right-side Ipi
 * @param[out] align, calculated alignment
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enough memory to do operation
 */
static uint32_t get_align_with_shift(Ipi_t* X, Ipi_t* Y, uint8_t* align)
{
    uint32_t retVal = CDN_EOK;

    /* Can be casted - only 6 LSB used */
    *align = (uint8_t)ipi_msb_bitsnum(Y) % BITS_PER_LIMB;

    if (*align < (BITS_PER_LIMB - 1U)) {
        *align = BITS_PER_LIMB - 1U - *align;

        retVal  = ipi_shift_l(X, *align);
        retVal |= ipi_shift_l(Y, *align);

    } else {
        *align = 0U;
    }

    return retVal;
}

/**
 * Used to initialize divide helper strucutre.
 * @param[in] A, pointer to left-side Ipi
 * @param[in] B, pointer to right-side Ipi
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enoguh memory to store data
 */
static uint32_t init_ipi_div_helper(const Ipi_t* A, const Ipi_t* B)
{
    uint32_t retVal;
    uint8_t* align;

    Ipi_t* X  = &ipiDivHlp.X;
    Ipi_t* Y  = &ipiDivHlp.Y;
    Ipi_t* Q  = &ipiDivHlp.Q;

    /* Free also initializes ipis */
    cleanup_ipi_div_helper();

    retVal  = ipi_copy(X, A);
    retVal |= ipi_copy(Y, B);


    if (retVal == CDN_EOK) {
        retVal  = ipi_grow(Q, X->num_limbs + 2U);
        retVal |= ipi_grow(&ipiDivHlp.T1, 2);
        retVal |= ipi_grow(&ipiDivHlp.T2, 3);
    }

    if (retVal == CDN_EOK) {
        retVal = ipi_lset(Q, 0);
    }

    if (retVal == CDN_EOK) {
        retVal = get_align_with_shift(X, Y, &ipiDivHlp.align);
    }

    ipiDivHlp.n = X->num_limbs - 1U;
    ipiDivHlp.t = Y->num_limbs - 1U;
    ipiDivHlp.i = ipiDivHlp.n;

    /* Go to first state */
    lib_handler.divCalcCb = &make_base_q;

    return retVal;
}

static uint32_t make_base_q(void)
{
    /* Difference between number of limbs in X and Y */
    uint16_t limbs_diff;
    uint32_t shift_val;
    uint32_t retVal;
    ComparisonResult_t cmp_res;

    Ipi_t* X = &ipiDivHlp.X;
    Ipi_t* Y = &ipiDivHlp.Y;

    limbs_diff = X->num_limbs - Y->num_limbs;
    shift_val = BITS_PER_LIMB * (uint32_t)limbs_diff;
    /* Calculate y * b^(n-t) */
    retVal = ipi_shift_l(Y, shift_val);

    cmp_res = ipi_cmp(X, Y);

    /* Do while X is greater or equal Y */
    while ((cmp_res != COMPARISON_RESULT_LOWER) && (retVal == CDN_EOK)) {
        ipiDivHlp.Q.ptr[limbs_diff]++;
        retVal = ipi_sub_abs(X, X, Y);
        cmp_res = ipi_cmp(X, Y);
    }

    if (retVal == CDN_EOK) {
        /* Restore Y */
        retVal = ipi_shift_r(Y, shift_val);
    }

    lib_handler.divCalcCb = &do_divide_iteration;

    return retVal;
}

/** Realizes point 14.20.3.1 from Handbook of Applied Cryptography*/
static void calculate_q(void)
{
    const Ipi_t* X = &ipiDivHlp.X;
    const Ipi_t* Y = &ipiDivHlp.Y;
    Ipi_t* Q = &ipiDivHlp.Q;

    uint64_t temp;
    const uint32_t i = ipiDivHlp.i;
    const uint32_t t = ipiDivHlp.t;


    uint32_t current_q_index = i - t - 1U;


    if (X->ptr[i] >= Y->ptr[t]) {
        Q->ptr[current_q_index] = LIMB_MAX;
    } else {
        temp  = safe_shift64l((uint64_t)X->ptr[i], BITS_PER_LIMB) | (uint64_t)X->ptr[i - 1U];
        temp /= Y->ptr[t];

        if (temp > LIMB_MAX) {
            temp = LIMB_MAX;
        }

        Q->ptr[current_q_index] = (uint32_t)temp;
    }
}

/**
 * Returns three MSL (most-significant-limbs). Start at index value
 * @param[out] T, pointer to temporary Ipi (limbs will be stored there)
 * @param[in] X, pointer to Ipi
 * @param[in] index, start index
 */
static inline void get_division_x(Ipi_t* T, const Ipi_t* X, uint16_t index)
{
    ipi_buffer_cleanup(T->ptr, (uint32_t)T->num_limbs * CHARS_PER_LIMB);

    T->ptr[2] = X->ptr[index];

    if (index >= 1U) {
        T->ptr[1] = X->ptr[index - 1U];
    }

    if (index >= 2U) {
        T->ptr[0] = X->ptr[index - 2U];
    }
}

/**
 * Returns two MSL (most-significant-limbs). Start at index value
 * @param[out] T, pointer to temporary Ipi (limbs will be stored there)
 * @param[in] Y, pointer to Ipi
 * @param[in] index, start index
 */
static inline void get_division_y(Ipi_t* T, const Ipi_t* Y, uint16_t index)
{
    ipi_buffer_cleanup(T->ptr, (uint32_t)T->num_limbs * CHARS_PER_LIMB);

    T->ptr[1] = Y->ptr[index];

    if (index >= 1U) {
        T->ptr[0] = Y->ptr[index - 1U];
    }
}

/** Realizes point 14.20.3.2 from Handbook of Applied Cryptography*/
static uint32_t correct_q(void)
{
    const Ipi_t* Q = &ipiDivHlp.Q;
    Ipi_t* T1 = &ipiDivHlp.T1;
    Ipi_t* T2 = &ipiDivHlp.T2;

    const uint16_t i = ipiDivHlp.i;
    const uint16_t t = ipiDivHlp.t;
    const uint16_t current_q_index = i - t - 1U;
    uint32_t retVal;

    uint32_t* current_q = &Q->ptr[current_q_index];

    /* Get helper X value */
    get_division_x(T2, &ipiDivHlp.X, i);

    (*current_q)++;

    do {

        /* Get Y helper value */
        get_division_y(T1, &ipiDivHlp.Y, t);

        (*current_q)--;
        /* Q always greater or equal 0 */
        retVal = ipi_mul_uint(T1, T1, *current_q);

    } while ((retVal == CDN_EOK) && (ipi_cmp(T1, T2) == COMPARISON_RESULT_GREATER));

    return retVal;

}

/** Realizes point 14.20.3.3 from Handbook of Applied Cryptography */
static uint32_t calculate_x(void)
{
    Ipi_t* T1 = &ipiDivHlp.T1;
    Ipi_t* X = &ipiDivHlp.X;
    uint32_t shift_val;
    uint16_t current_q_index = ipiDivHlp.i - ipiDivHlp.t - 1U;

    // Q always greater or equal 0
    uint32_t retVal = ipi_mul_uint(T1, &ipiDivHlp.Y, ipiDivHlp.Q.ptr[current_q_index]);

    if (retVal == CDN_EOK) {
        shift_val  = (uint32_t)current_q_index * BITS_PER_LIMB;
        retVal = ipi_shift_l(T1, shift_val);
    }

    if (retVal == CDN_EOK) {
        retVal = ipi_sub(X, X, T1);
    }

    return retVal;
}

/** Used to correct calculated value duting division process*/
static uint32_t correct_result(void)
{
    uint32_t retVal = CDN_EOK;
    Ipi_t* X = &ipiDivHlp.X;
    Ipi_t* T1 = &ipiDivHlp.T1;
    uint16_t current_q_index;
    uint32_t shift_val;

    ComparisonResult_t cmp_res = ipi_cmp_int(X, 0);

    /* Correct when X is lower than 0 */
    if (cmp_res == COMPARISON_RESULT_LOWER) {

        current_q_index = ipiDivHlp.i - ipiDivHlp.t - 1U;
        ipiDivHlp.Q.ptr[current_q_index]--;

        retVal = ipi_copy(T1, &ipiDivHlp.Y);

        if (retVal == CDN_EOK) {
            shift_val = BITS_PER_LIMB * (uint32_t)current_q_index;
            retVal = ipi_shift_l(T1, shift_val);
        }

        if (retVal == CDN_EOK) {
            retVal = ipi_add(X, X, T1);
        }
    }

    return retVal;
}

/** Function realizes one iteration of division process */
static uint32_t do_divide_iteration(void)
{
    uint32_t retVal;

    calculate_q();

    retVal = correct_q();

    if (retVal == CDN_EOK) {
        retVal = calculate_x();
    }

    if (retVal == CDN_EOK) {
        retVal = correct_result();
    }

    /* Decrement loop index */
    ipiDivHlp.i--;

    if (ipiDivHlp.i == ipiDivHlp.t) {
        /* Finish calculation */
        lib_handler.divCalcCb = NULL;
    }

    return retVal;

}

/**
 * Used to check if whole procedure of division are needed
 * @param[oit] Q, total part of result
 * @param[out] R, rest part of result
 * @param[in] A, left-side Ipi
 * @param[in] B, right-side Ipi
 * @return CDN_EOK if success (no operation needed),
 *         CDN_EINVAL if B equals 0,
 *         CDN_EINPROGRESS if extra division operation are needed
 */
static inline uint32_t check_divide_conditions(Ipi_t* Q, Ipi_t* R, const Ipi_t* A, const Ipi_t* B)
{
    uint32_t retVal = CDN_EOK;

    if (ipi_cmp_int(B, 0) == COMPARISON_RESULT_EQUAL) {
        retVal = CDN_EINVAL;
    } else if (ipi_cmp_abs(A, B) == COMPARISON_RESULT_LOWER) {

        if (Q != NULL) {
            retVal |= ipi_lset(Q, 0);
        }

        if (R != NULL) {
            retVal |= ipi_copy(R, A);
        }

    } else {
        lib_handler.divCalcCb = &make_base_q;
        retVal = CDN_EINPROGRESS;
    }

    return retVal;
}

static inline uint32_t get_divide_result(Ipi_t* Q, Ipi_t* R)
{
    uint32_t retVal = CDN_EOK;
    bool same_signs;

    if (Q != NULL) {
        retVal = ipi_copy(Q, &ipiDivHlp.Q);
        same_signs = check_if_signs_same(&ipiDivHlp.X, &ipiDivHlp.Y);

        if (same_signs) {
            Q->sign = IPI_POSITIVE_VAL;
        } else {
            Q->sign = IPI_NEGATIVE_VAL;
        }
    }

    if (R != NULL) {

        retVal |= ipi_copy(R, &ipiDivHlp.X);
        retVal |= ipi_shift_r(R, ipiDivHlp.align);

        if( ipi_cmp_int( R, 0 ) == COMPARISON_RESULT_EQUAL) {
            R->sign = IPI_POSITIVE_VAL;
        } else {
            R->sign = ipiDivHlp.X.sign;
        }
    }

    return retVal;
}


static uint32_t ipi_div_ipi_splited_mode( Ipi_t *Q, Ipi_t *R, const Ipi_t *A, const Ipi_t *B )
{
    /* Logic of code ensure that will be initialized before first use.
     * Assignment only needed to avoid MISRA */
    uint32_t retVal = CDN_EINVAL;

    CalcCb_t* calcCb = &lib_handler.divCalcCb;

    if (*calcCb == NULL) {

        ipiDivHlp.is_initialized = false;
        retVal = check_divide_conditions(Q, R, A, B);


        if (retVal == CDN_EINPROGRESS) {
            retVal = init_ipi_div_helper(A, B);

            /* Initialized flag need to be set there, because during init process
             * helper is cleaned up */
            ipiDivHlp.is_initialized = true;
        }

    } else {
        retVal = (*calcCb)();
    }

    if (retVal == CDN_EOK) {
        if (*calcCb == NULL) {
            /* Task is finished*/
            if (ipiDivHlp.is_initialized) {
                /* Data was not stored in Q,R before */
                retVal = get_divide_result(Q, R);
            }

            cleanup_ipi_div_helper();
        } else {
            retVal = CDN_EINPROGRESS;
        }

    } else {
            /* Error ocurred during calculation */
            cleanup_ipi_div_helper();
    }

    return retVal;
}

/**
 * Sanity function of modulo operation
 * @param[in] A, pointer to left-hand Ipi
 * @param[in] B, pointer to right-hand Ipi
 * @return CDN_EOK if success,
 *         CDN_EINVAL if parameters are invalid
 */
static uint32_t ipi_mod_splitted_mode_SF(const Ipi_t* A, const Ipi_t* B) {

    uint32_t retVal;

    if ((A == NULL) || (A->ptr == NULL)) {
        retVal = CDN_EINVAL;
    } else if ((B == NULL) || (B->ptr == NULL)) {
        retVal = CDN_EINVAL;
    } else {
        retVal = CDN_EOK;
    }

    return retVal;
}

/**
 * Modulo: R = A mod B
 * @param[out] R, pointer to destination Ipi for the rest value
 * @param[in] A, pointer to left-hand IPI
 * @param[in] B, pointer to right-hand IPI
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enoguh memory,
 *         CDN_EINVAL if parameters are invalid
 */
static uint32_t ipi_mod_splited_mode( Ipi_t *R, const Ipi_t *A, const Ipi_t *B )
{
    uint32_t retVal = CDN_EOK;

    if (lib_handler.divCalcCb == NULL) {

        retVal = ipi_mod_splitted_mode_SF(A,B);

        if (retVal == CDN_EOK) {
            if( ipi_cmp_int( B, 0 ) == COMPARISON_RESULT_LOWER) {
                retVal = CDN_EINVAL;
            }
        }
    }

    /* Do division operation */
    if (retVal == CDN_EOK) {
        retVal = ipi_div_ipi_splited_mode(NULL, R, A, B );
    }

    if (retVal == CDN_EOK) {

        /* Correct result */
        while ((ipi_cmp_int( R, 0 ) == COMPARISON_RESULT_LOWER) && (retVal == CDN_EOK)) {
            retVal = ipi_add( R, R, B );
        }

        while ((ipi_cmp( R, B ) != COMPARISON_RESULT_LOWER) && (retVal == CDN_EOK)) {
            retVal = ipi_sub( R, R, B );
        }
    }

    return retVal;
}

/*
 * Montgomery multiplication helper function
 * @param[in] A, left-hand Ipi
 * @param[in] B, reight-hand Ipi
 * @param[in] N, modulus value
 * @param[in] mm, montgomery specified value
 * @param[out] T, temporary buffer to store Ipi data
 */
static void ipi_montmul_hlp(const Ipi_t *A, const Ipi_t *B, const Ipi_t *N, uint32_t mm, const Ipi_t *T)
{
    uint16_t n_limbs = N->num_limbs;
    uint32_t m = (B->num_limbs < n_limbs) ? B->num_limbs : n_limbs;
    uint32_t* temp_buffer;
    uint32_t temp_val;
    uint32_t a_val;
    uint16_t i;

    /* Iterate through all limbs */
    for( i = 0U; i < n_limbs; i++ ) {

        temp_buffer = &T->ptr[i];
        a_val = A->ptr[i];
        temp_val = (*temp_buffer + (a_val * B->ptr[0] )) * mm;

        ipi_mul_hlp( m, B->ptr, temp_buffer, a_val);
        ipi_mul_hlp( n_limbs, N->ptr, temp_buffer, temp_val);

        *temp_buffer = a_val;
        temp_buffer[n_limbs + 2U] = 0U;
    }
}

/*
 * Montgomery multiplication: A = A * B * R^-1 mod N  (HAC 14.36)
 * @param[in] A, left-hand Ipi
 * @param[in] B, reight-hand Ipi
 * @param[in] N, modulus value
 * @param[in] mm, montgomery specified value
 * @param[out] T, temporary buffer to store Ipi data
 */
static void ipi_montmul( Ipi_t *A, const Ipi_t *B, const Ipi_t *N, uint32_t mm, const Ipi_t *T )
{
    uint16_t n_limbs;
    uint16_t m;
    uint16_t i;

    ipi_buffer_cleanup(T->ptr, ((uint32_t)T->num_limbs * CHARS_PER_LIMB));

    ipi_montmul_hlp(A, B, N, mm, T);

    n_limbs = N->num_limbs;

    /* Copy results into buffer */
    CPS_BufferCopy((volatile uint8_t*)A->ptr,
                   (volatile uint8_t*)&T->ptr[n_limbs],
                   (((uint32_t)n_limbs + 1U) * CHARS_PER_LIMB));

    /* Correct results */
    if( ipi_cmp_abs( A, N ) != COMPARISON_RESULT_LOWER) {
        ipi_sub_hlp( n_limbs, N->ptr, A->ptr );
    } else {
        /* Prevent timing attacks */
        ipi_sub_hlp( n_limbs, A->ptr, T->ptr );
    }
}

/*
 * Montgomery reduction: A = A * R^-1 mod N
 * @param[in] A, left-hand Ipi
 * @param[in] B, reight-hand Ipi
 * @param[in] N, modulus value
 * @param[in] mm, montgomery specified value
 * @param[out] T, temporary buffer to store Ipi data
 */
static void ipi_montred( Ipi_t *A, const Ipi_t *N, uint32_t mm, const Ipi_t *T )
{
    uint32_t z = 1U;
    Ipi_t U = {
            .sign = IPI_POSITIVE_VAL,
            .num_limbs = 1U,
            .ptr = &z
    };

    ipi_montmul( A, &U, N, mm, T );
}

/**
 * Used to initialize iterator
 * @param[out] iterator, pointer to used iterator
 * @param[in] ipiPtr, pointer to Ipi through iteration will be done
 */
static void init_bit_iterator(Limb2BitIterator_t* iterator, const Ipi_t* ipiPtr)
{
    uint32_t index;

    iterator->limbs_number = ipiPtr->num_limbs - 1U;

    /* Get number of bits in last limb (if limb is not full)*/
    index = (ipi_msb_bitsnum(ipiPtr) % BITS_PER_LIMB);
    if (index == 0U) {
        iterator->bit_number = NUMBER_OF_BITS_IN_UINT8_T - 1U;
    } else {
        iterator->bit_number = (uint8_t)index;
    }

    iterator->ipiPtr = ipiPtr->ptr;
    iterator->isDone = false;
}

/**
 * Returns if iteration is done
 * @param[in] iterator, pointer to checked iterator
 * @return 'true' if is done, or 'false'
 */
static inline bool is_iteration_done(const Limb2BitIterator_t* iterator)
{
    return iterator->isDone;
}

/**
 * Get next bit from iterator
 * @param[in] iterator, pointer to used iterator
 * @returns next used bit
 */
static uint8_t get_next_bit(Limb2BitIterator_t* iterator)
{
    uint8_t bit = UINT8_MAX;

    if (!iterator->isDone) {
        iterator->bit_number--;

        bit = (uint8_t)safe_shift32r(iterator->ipiPtr[iterator->limbs_number], iterator->bit_number);

        bit &= LIMB_TO_BIT_ITERATOR_MASK;
    }

    /* Get next limb */
    if (iterator->bit_number == 0U) {
        iterator->bit_number = NUMBER_OF_BITS_IN_UINT8_T - 1U;

        if (iterator->limbs_number != 0U) {
            iterator->limbs_number--;
        } else {
            iterator->isDone = true;
        }
    }

    return bit;
}


static uint32_t exp_prereq_calc_r2_handler(void)
{
    uint32_t retVal;
    uint8_t i = 0U;
    Ipi_t* RR = &expModHlp.Z;

    retVal  = ipi_lset(RR, 1);

    /* Shift in 'while' to avoid overflow numbers of shifting */
    while ((i < (BITS_PER_LIMB * 2U)) && (retVal == CDN_EOK)) {
        retVal |= ipi_shift_l(RR, (uint32_t)expModHlp.N->num_limbs);
        i++;
    }

    lib_handler.expModCalcCb = &exp_prereq_calc_mod_handler;

    return retVal;
}

static uint32_t exp_prereq_calc_mod_handler(void)
{
    uint32_t retVal;
    /* Use Z as temporary buffer */
    Ipi_t* localIpi = &expModHlp.Z;

    retVal = ipi_mod_splited_mode(localIpi, localIpi, expModHlp.N);

    if (retVal != CDN_EINPROGRESS) {
        lib_handler.expModCalcCb = &exp_prereq_calc_x_dash_handler;
    }

    return retVal;
}

static uint32_t exp_prereq_calc_x_dash_handler(void)
{
    uint32_t retVal;
    /* Use A as temporary buffer - data is copied there at the end of prerequesities procedure*/
    Ipi_t* localIpi = &expModHlp.Z;
    Ipi_t* x_dash = &expModHlp.W[1];
    const Ipi_t* m = expModHlp.N;

    // TODO: how remove checking each time ?
    if (ipi_cmp(&expModHlp.A, m) != COMPARISON_RESULT_LOWER) {
        retVal = ipi_mod_splited_mode(x_dash, &expModHlp.A, m);
    } else {
        retVal = ipi_copy(x_dash, &expModHlp.A);
    }

    if (retVal == CDN_EOK) {
        ipi_montmul(x_dash, localIpi, m, expModHlp.mm, &expModHlp.T);
        lib_handler.expModCalcCb = &exp_prereq_calc_a_handler;
    }

    return retVal;
}

static uint32_t exp_prereq_calc_a_handler(void)
{
    /* Use A as temporary buffer - data is copied there at the end of prerequesities procedure*/
    Ipi_t* localIpi = &expModHlp.Z;

    if( expModHlp.window_size > 1U ) {
            lib_handler.expModCalcCb = &calculate_g;
    } else {
            lib_handler.expModCalcCb = &do_sliding_window_exp;
    }

    /* X = R^2 * R^-1 mod N = R mod N */
    ipi_montred(localIpi, expModHlp.N, expModHlp.mm, &expModHlp.T);
    return ipi_copy(expModHlp.ipiPtr, localIpi);
}

/** Used to calculate array of used multiplicands */
static uint32_t calculate_g(void)
{
    uint8_t i;
    uint32_t retVal;
    const uint8_t last_window_bit_index = expModHlp.window_size - 1U;
    const Ipi_t* N = expModHlp.N;
    const uint32_t last_window_bit_mask = safe_shift32l(1U, last_window_bit_index);
    const uint16_t limb_size = N->num_limbs + 1U;

    Ipi_t* current_w = &expModHlp.W[last_window_bit_mask];

    retVal  = ipi_grow(current_w, limb_size);
    retVal |= ipi_copy(current_w, &expModHlp.W[1]);

    if (retVal == CDN_EOK) {

        /* Calculate base value of multiplicand */
        for (i = 0U; i < last_window_bit_index; i++) {
            ipi_montmul(current_w, current_w, N, expModHlp.mm, &expModHlp.T );
        }

        i++;

        /* Iterate over rest of items */
        for ( ; i < (uint8_t)safe_shift32l(1U, expModHlp.window_size); i++) {

            current_w = &expModHlp.W[i];

            retVal  = ipi_grow(current_w, limb_size);
            retVal |= ipi_copy(current_w, &expModHlp.W[i - 1U]);

            if (retVal == CDN_EOK) {
                ipi_montmul(current_w, current_w, N, expModHlp.mm, &expModHlp.T);
            } else {
                break;
            }
        }
    }

    return retVal;
}

/**
 * Return size of required window
 * @param[in] ipi, pointer to Ipi on which window will be constructed
 * @returns window size
 */
static inline uint8_t get_window_size(const Ipi_t* ipi)
{
    uint32_t i = ipi_msb_bitsnum(ipi);
    uint8_t windowSize;

    if (i > 671U) {
        windowSize = 6U;
    } else if (i > 239U) {
        windowSize = 5U;
    } else if (i > 79U) {
        windowSize = 4U;
    } else if (i > 23U) {
        windowSize = 3U;
    } else {
        windowSize = 1U;
    }

    return windowSize;

}
/** Helper structur to save current state of window */
typedef struct {
    uint8_t window_bits;
    uint8_t number_of_bits;
    bool is_window_full;
    uint8_t window_size;
} Window_t;

/**
 * Used to update widnow after bit iteration
 * @param[in] window_ptr, pointer to used window
 * @param[in] ei, iterated bit value
 */
static void update_window(Window_t* window_ptr, uint8_t ei)
{
    uint8_t curr_window_bit_pos;
    window_ptr->number_of_bits++;

    curr_window_bit_pos = window_ptr->window_size - window_ptr->number_of_bits;

    window_ptr->window_bits |= (uint8_t)safe_shift32l(ei, curr_window_bit_pos);

    window_ptr->is_window_full = (window_ptr->number_of_bits == window_ptr->window_size);
}

/**
 * Used to cleanup current window
 * @param-in] window_ptr, pointer to window
 */
static void cleanup_window(Window_t* window_ptr)
{
    window_ptr->number_of_bits = 0U;
    window_ptr->window_bits = 0U;
    window_ptr->is_window_full = false;
}

/**
 * Finish sliding window exponentation step
 * @param[in] window_ptr, pointer to window
 */
static void finish_sliding_window_exp(Window_t* window_ptr)
{
    uint32_t mask = safe_shift32l(1U, window_ptr->window_size);
    uint8_t i;

    const Ipi_t* N = expModHlp.N;
    Ipi_t* result = expModHlp.ipiPtr;

    /* Process the remaining bits */
    for (i = 0U; i < window_ptr->number_of_bits; i++) {

        ipi_montmul(result, result, N, expModHlp.mm, &expModHlp.T);

        window_ptr->window_bits = (uint8_t)safe_shift32l(window_ptr->window_bits, 1U);

        if ((window_ptr->window_bits & mask) != 0U) {
            ipi_montmul(result, &expModHlp.W[1], N, expModHlp.mm, &expModHlp.T);
        }
    }

    lib_handler.expModCalcCb = &correct_exp_result;
    cleanup_window(window_ptr);
}

/**
 * Used to make slinding window exponetation phase. Each call of functions mean one iteration
 * @return CDN_EOK if success,
 *         CDN_EINPROGRESS if operation is not done yet
 */
static uint32_t do_sliding_window_exp(void)
{
    uint8_t ei;
    Ipi_t* dest_ipi = expModHlp.ipiPtr;
    const Ipi_t* N = expModHlp.N;
    uint32_t retVal;
    static Window_t window = {0U, 0U, false, 0U};
    uint8_t i;
    uint32_t mm = expModHlp.mm;
    Ipi_t* temp_ipi = &expModHlp.T;
    Limb2BitIterator_t* limb_to_bit_iterator = &expModHlp.e_iterator;

    ei = get_next_bit(limb_to_bit_iterator);

    if((ei == 0U) && (window.number_of_bits == 0U)) {
        /* Out of window, square dest_ipi */
        ipi_montmul( dest_ipi, dest_ipi, N, mm, temp_ipi);
    } else {
        /* Add ei to current window */

        window.window_size = expModHlp.window_size;
        update_window(&window, ei);

        if (window.is_window_full) {

            /* Take any 6b sequence. We need do (i-l+1) squaring of A if ei = 1,
             * and (6-i+l-1) for padding zeros. So for any 6b sequence we need do
             * (i-l+1+6-i+l-1) = 6 squaring operations */
            for (i = 0U; i < window.window_size; i++) {
                ipi_montmul(dest_ipi, dest_ipi, N, mm, temp_ipi );
            }

            /* dest_ipi = dest_ipi * W[current_window] R^-1 mod N */
            ipi_montmul(dest_ipi, &expModHlp.W[window.window_bits], N, mm, temp_ipi);

            /* Cleanup window data */
            cleanup_window(&window);
        }
    }

    if (is_iteration_done(limb_to_bit_iterator)) {

        finish_sliding_window_exp(&window);
        retVal = CDN_EOK;

    } else {
        retVal = CDN_EINPROGRESS;
    }

    return retVal;

}

/**
 * Helper sanity function to check modulus N
 * @param[in] N, pointer to modulus N Ipi
 * @return CDN_EOK if success,
 *         CDN_EINVAL if invalid paramter
 */
static inline uint32_t exp_mod_n_SF(const Ipi_t* N) {

    uint32_t retVal = CDN_EOK;

    if ((N == NULL) || (N->ptr == NULL) || ((N->ptr[0] & 1U) == 0U) || (ipi_cmp_int( N, 0 ) == COMPARISON_RESULT_LOWER)) {
        retVal = CDN_EINVAL;
    }

    return retVal;
}

/**
 * Sanity function to exponentation parameters
 * @param[in] dest_ipi, pointer to destination Ipi
 * @param[in] A, left-hand Ipi
 * @param[in] B, right-hand Ipi
 * @param[in] E, pointer to exponent Ipi
 * @param[in] N, pointer to modulus Ipi
 * @return CDN_EOK if success,
 *         CDN_EINVAL if invalid paramters
 */
static uint32_t ipi_exp_mod_SF(const Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *E, const Ipi_t *N)
{
    uint32_t retVal = CDN_EOK;

    if (dest_ipi == NULL) {
        retVal = CDN_EINVAL;
    } else if ((A == NULL) || (A->ptr == NULL)) {
        retVal = CDN_EINVAL;
    } else if ((E == NULL) || (E->ptr == NULL) || (ipi_cmp_int( E, 0 ) == COMPARISON_RESULT_LOWER)) {
        retVal = CDN_EINVAL;
    } else {
        retVal = exp_mod_n_SF(N);
    }

    return retVal;
}

/**
 * Used to intialize exponent helper structure
 * @param[in] ipiPtr, pointer to destination Ipi
 * @param[in] left-hand Ipi
 * @param[in] N, pointer to modulus Ipi
 * @param[in] E, pointer to exponent Ipi
 * @return CDN_EOK if success,
 *         CDN_ENOMEM if not enough memory
 */
static uint32_t init_exp_mod_helper(Ipi_t* ipiPtr, const Ipi_t* A, const Ipi_t* N, const Ipi_t* E)
{
    uint32_t i;
    uint32_t retVal;
    uint16_t size = N->num_limbs + 1U;

    /* Initialize temporary ipi */
    ipi_init(&expModHlp.T);
    retVal = ipi_grow(&expModHlp.T, (size * 2U));

    /* Initialize mm */
    ipi_montg_init(&expModHlp.mm, N);

    /* Initialize W */
    for (i = 0; i < 64U; i++) {
        ipi_init(&expModHlp.W[i]);
    }
    retVal |= ipi_grow(&expModHlp.W[1],  size);

    /* Initialize A */
    ipi_init(&expModHlp.A);
    retVal |= ipi_copy(&expModHlp.A, A);

    expModHlp.isNeg = (A->sign == IPI_NEGATIVE_VAL );
    if (expModHlp.isNeg) {
        expModHlp.A.sign = IPI_POSITIVE_VAL;
    }

    /* Get window size */
    expModHlp.window_size = get_window_size(E);

    /* Initialize destination ipi */
    expModHlp.ipiPtr = ipiPtr;
    retVal |= ipi_grow(expModHlp.ipiPtr, size);

    /* Initialize temporary buffer Z */
    ipi_init(&expModHlp.Z);

    /* Initialize iterator of E */
    init_bit_iterator(&expModHlp.e_iterator, E);

    expModHlp.N = N;

    lib_handler.expModCalcCb = &exp_prereq_calc_r2_handler;


    return retVal;
}

/** Used to correct calculated value in exponent process */
static uint32_t correct_exp_result(void)
{
    uint32_t retVal = CDN_EOK;
    Ipi_t* dest_ipi = expModHlp.ipiPtr;
    const Ipi_t* N = expModHlp.N;

    /* dest_ipi = A^E * R * R^-1 mod N = A^E mod N */
    ipi_montred(dest_ipi, N, expModHlp.mm, &expModHlp.T );

    if (expModHlp.isNeg) {
        dest_ipi->sign = IPI_NEGATIVE_VAL;
        retVal = ipi_add(dest_ipi, N, dest_ipi );
    }

    lib_handler.expModCalcCb = NULL;

    return retVal;
}

/** Used to clean-up exponent helper structure */
static void cleanup_exp_mod_helper(void)
{
    uint32_t i;
    for (i = 0U; i < safe_shift32l(1U, expModHlp.window_size); i++) {
        ipi_free(&expModHlp.W[i]);
    }

    ipi_free(&expModHlp.W[1]);
    ipi_free(&expModHlp.T);
    ipi_free(&expModHlp.A);

    lib_handler.expModCalcCb = NULL;

}

uint32_t ipi_exp_mod(Ipi_t *dest_ipi, const Ipi_t *A, const Ipi_t *E, const Ipi_t *N)
{
    /* Logic of code ensure that will be initialized before first use.
     * Assignment only needed to avoid MISRA */
    uint32_t retVal = CDN_EINVAL;

    /* Need to be a pointer - modified during calculation */
    CalcCb_t* calcCb = &lib_handler.expModCalcCb;

    /* If null - function is not initialized */
    if (*calcCb == NULL) {

        retVal = ipi_exp_mod_SF(dest_ipi, A, E, N);

        if (retVal == CDN_EOK) {
            retVal = init_exp_mod_helper(dest_ipi, A, N, E);
        }

    } else {
        retVal = (*calcCb)();
    }

    if (retVal == CDN_EOK) {
        if (*calcCb == NULL) {
            /* Task is finished*/
            cleanup_exp_mod_helper();
        } else {
            retVal = CDN_EINPROGRESS;
        }

    } else {
        if (retVal != CDN_EINPROGRESS) {
            /* Error ocurred during calculation */
            cleanup_exp_mod_helper();
        }
    }

    return retVal;
}
/* parasoft-end-suppress METRICS-36-3 */
/* parasoft-end-suppress METRICS-41-3 */

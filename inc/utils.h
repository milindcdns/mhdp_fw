// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 ******************************************************************************
 *
 * utils.h
 *
 ******************************************************************************
 */

// parasoft-begin-suppress METRICS-36-3 "function called from more than 5 different functions, DRV-3823"

#ifndef UTILS_H
#define UTILS_H

#include "string.h"
#include <stdio.h>
#include "cdn_stdint.h"
#include "cdn_stdtypes.h"

#define NUMBER_OF_BITS_IN_UINT64_T  64U
#define NUMBER_OF_BYTES_IN_UINT32T	4U
#define NUMBER_OF_BITS_IN_BYTE		8U
/**
 * \addtogroup INFRASTRUCTURES
 * \{
 */

 /**
 *  \file utils.h
 *  \brief general functions like random fill
 */

/* Typedef of pointer to functions called for states */
typedef void (*StateCallback_t)(void);

/** Convert bool value into 32-bit value */
static inline uint32_t bool_to_uint(bool val)
{
    return val ? 1U : 0U;
}

/** Return bits 7:0 from a 32-bit value */
static inline uint8_t GetByte0(uint32_t val)
{
    return (uint8_t)(val >> 0);
}

/** Return bits 15:8 from a 32-bit value */
static inline uint8_t GetByte1(uint32_t val)
{
    return (uint8_t)(val >> 8);
}

/** Return bits 23:16 from a 32-bit value */
static inline uint8_t GetByte2(uint32_t val)
{
    return (uint8_t)(val >> 16);
}

/** Return bits 31:24 from a 32-bit value */
static inline uint8_t GetByte3(uint32_t val)
{
    return (uint8_t)(val >> 24);
}

/** Return bits 15:0 from a 32-bit value */
static inline uint16_t GetWord0(uint32_t val)
{
    return (uint16_t)(val >> 0);
}

/** Return bits 31:16 from a 32-bit value */
static inline uint16_t GetWord1(uint32_t val)
{
    return (uint16_t)(val >> 16);
}

/* Return bits 31:0 from a 64-bit value */
static inline uint32_t GetDword0(uint64_t val)
{
    return (uint32_t)(val >> 0);
}

/** Return bits 63:32 from a 64-bit value */
static inline uint32_t GetDword1(uint64_t val)
{
    return (uint32_t)(val >> 32);
}

/** Get 32-bit big-endian value from 4 consecutive bytes starting from buffer[0] */
static inline uint32_t getBe32(const uint8_t* buffer)
{
    return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16 ) | ((uint32_t)buffer[2] <<  8) | (uint32_t)buffer[3];
}

/** Get 24-bit big-endian value from 3 consecutive bytes starting from buffer[0] */
static inline uint32_t getBe24(const uint8_t* buffer)
{
    return ((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | (uint32_t)buffer[2];
}

/** Get 16-bit big-endian value from 2 consecutive bytes starting from buffer[0] */
static inline uint16_t getBe16(const uint8_t* buffer)
{
    return ((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1];
}

/** Set 32-bit value to 4 consecutive bytes in buffer (big endian) */
static inline void setBe32(uint32_t value, uint8_t* buffer)
{
    buffer[0] = (uint8_t) (value >> 24);
    buffer[1] = (uint8_t) (value >> 16);
    buffer[2] = (uint8_t) (value >> 8);
    buffer[3] = (uint8_t) (value >> 0);
}

/** Set 24-bit value to 2 consecutive bytes in buffer (big endian) */
static inline void setBe24(uint32_t value, uint8_t* buffer)
{
    buffer[0] = (uint8_t) (value >> 16);
    buffer[1] = (uint8_t) (value >> 8);
    buffer[2] = (uint8_t) (value >> 0);
}

/** Set 16-bit value to 2 consecutive bytes in buffer (big endian) */
static inline void setBe16(uint16_t value, uint8_t* buffer)
{
    buffer[0] = (uint8_t) (value >> 8);
    buffer[1] = (uint8_t) (value >> 0);
}

/** Get 16-bit value out of 2 consecutive bytes in little endian buffer. */
static inline uint16_t getLe16(const uint8_t* input)
{
    return ((uint16_t)input[1] << 8) | (uint16_t)input[0];
}

/** Get 32-bit value out of 4 consecutive bytes in little endian buffer. */
static inline uint32_t getLe32(const uint8_t* input)
{
    return ((uint32_t)input[3] << 24) |((uint32_t)input[2] << 16) | ((uint32_t)input[1] << 8) | (uint32_t)input[0];
}

/** Set 32-bit value to 4 consecutive bytes in little endian buffer */
static inline void setLe32(uint32_t value, uint8_t* buffer)
{
    buffer[0] = (uint8_t) (value >> 0);
    buffer[1] = (uint8_t) (value >> 8);
    buffer[2] = (uint8_t) (value >> 16);
    buffer[3] = (uint8_t) (value >> 24);
}

/** Get the smaller value. */
static inline uint8_t get_minimum(uint8_t a, uint8_t b)
{
    return (a > b) ? a : b;
}

/**
 * Swap order of items in buff.
 * @param[in,out] buff, pointer to buffer with items to swap.
 * @param[in] size [in], number of elements in buffer.
 */
void convertEndianness(uint8_t *buff, size_t size);

bool if_buffers_equal(const uint8_t* const a, const uint8_t* const b, uint32_t size);

#define LEFT true
#define RIGHT false
#define NUMBER_OF_BITS_IN_UINT32_T 32U
#define NUMBER_OF_BITS_IN_UINT8_T 8U

/** Shift uint32_t value. */
static inline uint32_t safe_shift32(bool left, uint32_t val, uint8_t shift) {
    uint32_t ret = 0;
    if(shift < NUMBER_OF_BITS_IN_UINT32_T) {
        ret = left ? (val << shift) : (val >> shift);
    } else {
        ret = 0;
    }
    return ret;
}

/**
 * Shift left uint32_t value
 * @param[in] val, number to shift
 * @param[in] shift, number of bits to shift
 * @return shifted value
 */
static inline uint32_t safe_shift32l(uint32_t val, uint8_t shift) {
    uint32_t ret = 0;
    if(shift < NUMBER_OF_BITS_IN_UINT32_T) {
        ret = (val << shift);
    } else {
        ret = 0;
    }
    return ret;
}

/**
 * Shift right uint32_t value
 * @param[in] val, number to shift
 * @param[in] shift, number of bits to shift
 * @return shifted value
 */
static inline uint32_t safe_shift32r(uint32_t val, uint8_t shift) {
    uint32_t ret = 0;
    if(shift < NUMBER_OF_BITS_IN_UINT32_T) {
        ret = (val >> shift);
    } else {
        ret = 0;
    }
    return ret;
}

/**
 * Shift left uint32_t value
 * @param[in] val, number to shift
 * @param[in] shift, number of bits to shift
 * @return shifted value
 */
static inline uint64_t safe_shift64l(uint64_t val, uint8_t shift) {
    uint64_t ret = 0;
    if(shift < NUMBER_OF_BITS_IN_UINT64_T) {
        ret = (val << shift);
    } else {
        ret = 0;
    }
    return ret;
}

/** Cast 32-bit integer address to pointer to uint32_t */
static inline uint32_t *uintToPointer(uint32_t addr) {
    // parasoft-begin-suppress MISRA2012-RULE-11_4 "A conversion should not be performed between a pointer to object and an integer type, DRV-4620"
    return (uint32_t *)addr;
    // parasoft-end-suppress MISRA2012-RULE-11_4
}

/** Allocate memory using static_alloc.h */
uint32_t* MEM_malloc(uint16_t size);

/** Free memory using static_alloc.h */
void MEM_free(const uint32_t *ptr);

/**
 * Fill the buffer with random numbers. Numbers are generated by Pseudo Random Number Generator.
 * @param[in] len_bytes, number of bytes to write (with 4 bytes granularity)
 * @param[in,out] buff, buffer that needs to be filled
 */
void UTIL_FillRandomNumber(uint8_t * buff, uint8_t lenBytes);

void UTIL_PRNG_SetSeed(const uint32_t* seedVal);

#endif //__UTILS__

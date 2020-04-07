// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 ***********************************************************************
 * cps_v3.h
 * Interface for Cadence Platform Services (CPS), version 3
 *
 * This is the "hardware abstraction layer" upon which all drivers are built.
 * It must be implemented for each platform.
 ***********************************************************************/
#ifndef CPS_V3_H
#define CPS_V3_H

#include "cdn_stdtypes.h"

/* parasoft-begin-suppress MISRA2012-RULE-8_6-2 "An identifier with external linkage shall have exactly one external definition, DRV-4757" */
/* parasoft-begin-suppress METRICS-36-3 "A function should not be called from more than 5 different functions, DRV-3823" */

/****************************************************************************
 * Prototypes
 ***************************************************************************/

/**
 * Read a (32-bit) word
 * @param[in] address the address
 * @return the word at the given address
 */
extern uint32_t CPS_ReadReg32(volatile const uint32_t* address);

/**
 * Write a (32-bit) word to memory
 * @param[in] address the address
 * @param[in] value the word to write
 */
extern void CPS_WriteReg32(volatile uint32_t* address, uint32_t value);

/**
 * Read a byte, bypassing the cache
 * @param[in] address the address
 * @return the byte at the given address
 */
extern uint8_t CPS_UncachedRead8(volatile const uint8_t* address);

/**
 * Read a short, bypassing the cache
 * @param[in] address the address
 * @return the short at the given address
 */
extern uint16_t CPS_UncachedRead16(volatile const uint16_t* address);

/**
 * Read a (32-bit) word, bypassing the cache
 * @param[in] address the address
 * @return the word at the given address
 */
extern uint32_t CPS_UncachedRead32(volatile const uint32_t* address);

/**
 * Write a byte to memory, bypassing the cache
 * @param[in] address the address
 * @param[in] value the byte to write
 */
extern void CPS_UncachedWrite8(volatile uint8_t* address, uint8_t value);

/**
 * Write a short to memory, bypassing the cache
 * @param[in] address the address
 * @param[in] value the short to write
 */
extern void CPS_UncachedWrite16(volatile uint16_t* address, uint16_t value);

/**
 * Write a (32-bit) word to memory, bypassing the cache
 * @param[in] address the address
 * @param[in] value the word to write
 */
extern void CPS_UncachedWrite32(volatile uint32_t* address, uint32_t value);

/**
 * Write a (32-bit) address value to memory, bypassing the cache.
 * This function is for writing an address value, i.e. something that
 * will be treated as an address by hardware, and therefore might need
 * to be translated to a physical bus address.
 * @param[in] location the (CPU) location where to write the address value
 * @param[in] addrValue the address value to write
 */
extern void CPS_WritePhysAddress32(volatile uint32_t* location, uint32_t addrValue);

/**
 * Hardware specific memcpy.
 * @param[in] src  src address
 * @param[in] dst  destination address
 * @param[in] size size of the copy
 */
extern void CPS_BufferCopy(volatile uint8_t *dst, volatile const uint8_t *src, uint32_t size);

/**
 * Delay software execution by a number of nanoseconds
 * @param[in] ns number of nanoseconds to delay software execution
 */
extern void CPS_DelayNs(uint32_t ns);

#endif /* multiple inclusion protection */

/* parasoft-end-suppress METRICS-36-3 */
/* parasoft-end-suppress MISRA2012-RULE-8_6-2 */

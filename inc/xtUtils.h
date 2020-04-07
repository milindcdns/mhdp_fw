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
 * xtUtils.h
 *
 ******************************************************************************
 */

#ifndef XT_UTILS_H
#define XT_UTILS_H

#include <cdn_stdint.h>

typedef enum {
    /* Inject error into IRAM */
    ECC_ERROR_MEM_TYPE_INSTRUCTION_RAM = 1U,
    /* Inject error into DRAM */
    ECC_ERROR_MEM_TYPE_DATA_RAM = 2U
} EccErrorMemoryType;

typedef enum {
    /* Inject error into check bits */
    ECC_ERROR_TYPE_CHECK = 0U,
    /* Inject error into data bits */
    ECC_ERROR_TYPE_DATA  = 1U
} EccErrorType;

/**
 * Clean first word (4 bytes) of UserExceptionVector and DebugExceptionVector
 * and call illegal instruction exception to force fatal error
 */
void xtExecFatalInstr(void);

/**
 * Force execution after error injection to check if ASF interrupt was
 * catched properly.
 * @param[in] memType, type of memory where error was injected
 */
void xtMemepExtortError(uint8_t memType);

/**
 * Set Error Checking and Correction (ECC) enabled/disabled
 * @param[in] enable, set 1U if ECC will be enabled, otherwise 0U
 */
void xtSetEccEnable(uint8_t enable);

/**
 *  Inject errors into instruction and/or data RAMs
 *	@param[in] memType, type of memory where error will be injected
 *	@param[in] errorType, type of error to inject
 *	@param[in] mask, is the bit flip mask for check bits and databits
 *                   for both IRAM and DRAM, the valid values for
 *                   correctable errors for check bits are:
 *                   0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40;
 *                   for databits, the pattern is 1 bit set for a given bit
 *                   position
 */
void xtMemepInjectError(uint8_t memType, uint8_t errorType, uint32_t mask);

#endif

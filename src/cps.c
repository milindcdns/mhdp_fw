// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 ***********************************************************************
 * cps_bm.c
 *
 * Sample implementation of Cadence Platform Services for a bare-metal
 * system
 ***********************************************************************/

#include "cps.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* see cps.h */
uint32_t CPS_ReadReg32(volatile const uint32_t *address) {
    return *address;
}

/* see cps.h */
void CPS_WriteReg32(volatile uint32_t *address, uint32_t value) {
    *address = value;
}

/* see cps.h */
uint8_t CPS_UncachedRead8(volatile const uint8_t *address) {
    return *address;
}

/* see cps.h */
uint16_t CPS_UncachedRead16(volatile const uint16_t *address) {
    return *address;
}

/* see cps.h */
uint32_t CPS_UncachedRead32(volatile const uint32_t *address) {
    return *address;
}

/* see cps.h */
void CPS_UncachedWrite8(volatile uint8_t *address, uint8_t value) {
    *address = value;
}

/* see cps.h */
void CPS_UncachedWrite16(volatile uint16_t *address, uint16_t value) {
    *address = value;
}

/* see cps.h */
void CPS_UncachedWrite32(volatile uint32_t *address, uint32_t value) {
    *address = value;
}

/* see cps.h */
void CPS_WritePhysAddress32(volatile uint32_t *location, uint32_t addrValue) {
    *location = addrValue;
}

/* see cps.h */
void CPS_BufferCopy(volatile uint8_t *dst, volatile const uint8_t *src, uint32_t size) {
    // Do simple byte-after-byte copying
    uint32_t size_copy = size;
    while (size_copy > 0U) {
        size_copy--;
        dst[size_copy] = src[size_copy];
    }
}

/* Since this is a bare-metal system, with no MMU in place, we expect that there
 * will be no cache enabled, so no need to have CPS_CacheInvalidate,
 * CPS_CacheFlush */

// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 * __LICENCE_BODY_COPYRIGHT__
 * __LICENCE_BODY_CADENCE__
 *
 ******************************************************************************
 *
 * __FILENAME_BODY__
 *
 ******************************************************************************
 */

#ifndef MODE_H
#define MODE_H

typedef enum {
    DISPLAYPORT_FIRMWARE_ACTIVE,
    DISPLAYPORT_FIRMWARE_STANDBY
} DpMode_t;

extern DpMode_t dpMode;

/** Returns true if firmware is in active state */
static inline bool isActiveMode(void) {
    return dpMode == DISPLAYPORT_FIRMWARE_ACTIVE;
}

#endif /* MODE_H */

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
 * watchdog.h
 *
 ******************************************************************************
 */


#ifndef WATCHDOG_H
# define WATCHDOG_H

# include "cdn_stdtypes.h"

# ifndef WATCHDOG_MIN_VALUE
/*
 * Watchdog is cleared for nevery FW Scheduler loop (<2ms).
 * Measured min time of one execution of loop is 1us.
 * We take 400ns as minumum time which is 100 cycles
 * for 250MHz clock frequency
 */
#  define WATCHDOG_MIN_VALUE 100
# endif

# ifndef WATCHDOG_MAX_VALUE
/*
 * Watchdog is cleared for nevery FW Scheduler loop (<2ms).
 * Measured max time of one execution of loop is 260us.
 * We take 3ms as maximum time which is 750000 cycles for
 * For clock 250MHz frequency
 */
#  define WATCHDOG_MAX_VALUE 750000
# endif

void WatchdogSetEnable(bool enable);

void WatchdogClear(void);

void WatchdogSetConfig(uint32_t min, uint32_t max);

void WatchdogSetup(uint32_t min, uint32_t max);

#endif


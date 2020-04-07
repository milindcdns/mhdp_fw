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
 * watchdog.c
 *
 ******************************************************************************
 */

#include "watchdog.h"
#include "reg.h"

#define WATCHDOG_CLEAR_VALUE 0xA5A55A5AU

void WatchdogSetEnable(bool enable)
{
    // Read current watchdog state
    uint32_t reg = CPS_REG_READ(&mhdpRegBase->mhdp_apb_regs.WATCHDOG_EN_p);

    // Enable/disable watchdog
    if (enable) {
        reg |= CPS_FLD_WRITE(MHDP__MHDP_APB_REGS__WATCHDOG_EN_P, WATCHDOG_EN, 0, 1);
    } else {
        reg &= ~CPS_FLD_WRITE(MHDP__MHDP_APB_REGS__WATCHDOG_EN_P, WATCHDOG_EN, 0, 1);
    }

    CPS_REG_WRITE(&mhdpRegBase->mhdp_apb_regs.WATCHDOG_EN_p, reg);
}

void WatchdogClear(void)
{
    // Cleanup watchog
    CPS_REG_WRITE(&mhdpRegBase->mhdp_apb_regs.WATCHDOG_CLR_p, WATCHDOG_CLEAR_VALUE);
}

void WatchdogSetConfig(uint32_t min, uint32_t max)
{
    // Set watchog configuration
    CPS_REG_WRITE(&mhdpRegBase->mhdp_apb_regs.WATCHDOG_MIN_p, min);
    CPS_REG_WRITE(&mhdpRegBase->mhdp_apb_regs.WATCHDOG_MAX_p, max);
}

void WatchdogSetup(uint32_t min, uint32_t max)
{
    // Clear, set config and enable watchdog
    WatchdogClear();
    WatchdogSetConfig(min, max);
    WatchdogSetEnable(true);
}


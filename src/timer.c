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
 * timer.c
 *
 ******************************************************************************
 */

#include "timer.h"
#include "reg.h"
#include "mode.h"
#include "cdn_stdint.h"

#include <xtensa/hal.h>

uint32_t CPU_CLOCK_MEGA;
static uint32_t timers[TIMERS_NUMBER]= {0U};

/**
 * Convert number of cycles into microseconds
 * @param[in] cycles, number of cycles
 * @return number of microseconds
 */
static inline uint32_t cyclesToMicroseconds(uint32_t cycles)
{
    return cycles / CPU_CLOCK_MEGA;
}

/**
 * Convert number of cycles into milliseconds
 * @param[in] cycles, number of cycles
 * @return number of milliseconds
 */
static inline uint32_t cyclesToMiliseconds(uint32_t cycles)
{
    return cyclesToMicroseconds(cycles) / 1000U;
}

/**
 * Calculate difference between values as cycles
 * @param[in] cyclesBefore, number of cycles
 * @param[in] cyclesAfter, number of cycles
 * @return difference between arguments is number of cycles
 */
static uint32_t calculateDiffrence(uint32_t cyclesBefore, uint32_t cyclesAfter)
{
    uint32_t diff = cyclesAfter - cyclesBefore;
    return diff;
}


void updateClkFreq(void)
{
    bool isActive = isActiveMode();

    /* Update Clock only if FW/IP are in stand-by mode */
    if (!isActive) {
        CPU_CLOCK_MEGA = RegRead(SW_CLK_H);
    }
}

void startTimer(Timer_t timerNum)
{
    uint32_t* timer;

    /* Checker to avoid out-of-range error */
    if (timerNum < TIMERS_NUMBER) {
        timer = &timers[(uint8_t) timerNum];
        *timer = xthal_get_ccount();
    }
}

/* parasoft-begin-suppress METRICS-36-3 "Function is called from more than 5 different functions, DRV-3823 */

/**
 * Return difference between timer value and actual in number of cycles
 * @param[in] timerNum, type of timer
 * @param[in] update, 1U if update timer value, 0U if not
 * @return difference in cycles number
 */
static uint32_t getTimerDiff(Timer_t timerNum, uint8_t update)
{
    uint32_t currCycles;
    uint32_t diffCycles = 0U;
    uint32_t* timer;

    /* Checker to avoid out-of-range error */
    if (timerNum < TIMERS_NUMBER) {
        timer = &(timers[timerNum]);
        currCycles = xthal_get_ccount();
        diffCycles = calculateDiffrence(*timer, currCycles);

        if (update == 1U) {
            *timer = currCycles;
        }
    }

    return diffCycles;
}
/* parasoft-end-suppress METRICS-36-3 */

uint32_t getTimerMsWithoutUpdate(Timer_t timerNum)
{
    uint32_t diffCycles = getTimerDiff(timerNum, 0U);
    return cyclesToMiliseconds(diffCycles);
}

uint32_t getTimerUsWithoutUpdate(Timer_t timerNum)
{   /* Get current timer value without update reference */
    uint32_t diffCycles = getTimerDiff(timerNum, 0U);
    return cyclesToMicroseconds(diffCycles);
}

uint32_t getTimerUsWithUpdate(Timer_t timerNum)
{   /* Get current timer value and update reference */
    uint32_t diffCycles = getTimerDiff(timerNum, 1U);
    return cyclesToMicroseconds(diffCycles);
}

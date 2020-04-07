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
 * timer.h
 *
 ******************************************************************************
 */

// parasoft-begin-suppress METRICS-36-3 "A function should not be called from more than 5 different functions" DRV-3823

#ifndef TIMER_H
#define TIMER_H

#include "cdn_stdint.h"

extern uint32_t CPU_CLOCK_MEGA;

/* Enum describes timers used in firmware */
typedef enum {
    /* System timer used by modRunner (user should not use this timer) */
    MODRUNNER_SYS_TIMER,
    /* Timer used to calculate latency between start and end of sending command transaction */
    DP_AUX_TRANSACTION_TIMER,
    /* Timer used to calculate latency of link response */
    MAILBOX_LINK_LATENCY_TIMER,
    /* Timer used to calculate latency of HDCP2X response */
    HDCP2_RESPONSE_LATENCY_TIMER,
    /* Number of used timers */
    TIMERS_NUMBER
} Timer_t;

/**
 * Update core clock input frequency (MHz)
 */
void updateClkFreq(void);

/**
 * Save actual value of timer
 * @param[in] timerNum, type of timer
 */
void startTimer(Timer_t timerNum);

/**
 * Return difference between timer value and actual in milliseconds
 * without updating timer value
 * @param[in] timerNum, type of timer
 * @return difference in milliseconds
 */
uint32_t getTimerMsWithoutUpdate(Timer_t timerNum);

/**
 * Return difference between timer value and actual in microseconds
 * without updating timer value
 * @param[in] timerNum, type of timer
 * @return difference in microseconds
 */
uint32_t getTimerUsWithoutUpdate(Timer_t timerNum);

/**
 * Return difference between timer value and actual in microseconds
 * with updating timer value
 * @param[in] timerNum, type of timer
 * @return difference in microseconds
 */
uint32_t getTimerUsWithUpdate(Timer_t timerNum);

/** Convert miliseconds to microseconds */
inline static uint32_t milliToMicro(uint32_t milli) {
    return milli * 1000U;
}

#endif /* TIMER_H */

// parasoft-end-suppress METRICS-36-3

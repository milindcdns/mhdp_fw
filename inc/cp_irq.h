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
 * cp_irq.h
 *
 ******************************************************************************
 */

/* parasoft-begin-suppress METRICS-36-3 "A function should not be called from more than 5 different functions, DRV-3823" */

#ifndef CP_IRQ_H
#define CP_IRQ_H

#include "utils.h"
#include "cdn_stdtypes.h"

#define CP_IRQ_NO_TIMEOUT 0U

#define CP_IRQ_LATENCY_TIME_MS 5U

void setCpIrqEvent(uint8_t evMask, uint32_t timeoutMs, bool cpIrq);
bool isCpIrqRoutineFinished(void);
void callCpIrqRoutine(void);
void initCpIrqRoutine(void);
#endif /* CP_IRQ_H */

/* parasoft-end-suppress METRICS-36-3 */

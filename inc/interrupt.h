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
 * interrupt.h
 *
 ******************************************************************************
 */

#ifndef INTERRUPT_H
#define INTERRUPT_H

void interruptInit(void);
static void HpdEventDetectedIsr(void);

#endif /* INTERRUPT_H */

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
 * controlChannelM.h
 *
 ******************************************************************************
 */
/* parasoft-begin-suppress METRICS-36-3 "Function should not be called from more than 5 different functions", DRV-3823 */
#ifndef CONTROL_CHANNEL_M_H
#define CONTROL_CHANNEL_M_H

#include "cdn_stdtypes.h"

/**
 *  Check if error occurred during last transaction
 *  @return  1U if error occurred during last transaction or 0U
 */
bool CHANNEL_MASTER_isErrorOccurred(void);

/**
 *  Check if the channel is free to send new data
 *  @return 'true' if channel is free or 'false'
 */
bool CHANNEL_MASTER_isFree(void);

/**
 * Generate write transaction, ending with STOP bit
 */
void CHANNEL_MASTER_write(uint16_t sizeOut, uint32_t offset, uint8_t* buff);

/**
 * Generate read transaction
 */
void CHANNEL_MASTER_read(uint16_t sizeOut, uint32_t offset, uint8_t* buff);

/**
 *  Write transaction over, should be called also in case of error
 */
void CHANNEL_MASTER_transactionOver(void);

/**
 * Initialize channel
 */
void CHANNEL_MASTER_init(void);

#endif /* CONTROL_CHANNEL_M_H */

/* parasoft-end-suppress METRICS-36-3 */

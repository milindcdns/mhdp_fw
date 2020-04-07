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
 * dp_tx_mail_handler.h
 *
 ******************************************************************************
 */

#ifndef DP_TX_MAIL_HANDLER_H_
#define DP_TX_MAIL_HANDLER_H_

#include "modRunner.h"
#include "cdn_stdtypes.h"

/* Masks of event codes sent to the host */
#define DP_TX_EVENT_CODE_HPD_HIGH           0x01U
#define DP_TX_EVENT_CODE_HPD_LOW            0x02U
#define DP_TX_EVENT_CODE_HPD_PULSE          0x04U
#define DP_TX_EVENT_CODE_HPD_STATE_HIGH     0x08U

extern uint8_t hpdState;

/**
 * Function used to insert DP_TX_MAIL_HANDLER module into context
 */
void DP_TX_MAIL_HANDLER_InsertModule(void);

/**
 * Initialize HPD events (set all)
 */
void DP_TX_MAIL_HANDLER_initOnReset(void);

/**
 * Send to host notification about HPD event.
 * @param[in] eventCode, code of event
 */
void DP_TX_MAIL_HANDLER_notifyHpdEv(uint8_t eventCode);

#endif /* DP_TX_MAIL_HANDLER_H */

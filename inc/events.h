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

#ifndef EVENTS_H
#define EVENTS_H

/**
 * Event ID sent to the host via SW_EVENT register
 * ID is same as bit-mask for register
 */
typedef enum {
    EVENT_ID_DPTX_HPD = 0x01U,
    EVENT_ID_DPTX_TRAINING = 0x02U,
    EVENT_ID_RESERVE0 = 0x04U,
    EVENT_ID_RESERVE1 = 0x08U,
    EVENT_ID_HDCPTX_STATUS = 0x10U,
    EVENT_ID_HDCPTX_IS_KM_STORED = 0x20U,
    EVENT_ID_HDCPTX_STORE_KM = 0x40U,
    EVENT_ID_HDCPTX_IS_RECEIVER_ID_VALID = 0x80U
} EventId_t;

#endif /* EVENTS_H */

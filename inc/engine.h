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
 * engine.h
 *
 ******************************************************************************
 */

#ifndef ENGINE_H
#define ENGINE_H

#include "cdn_stdint.h"

typedef enum {
    DEV_NON_HDCP_CAPABLE,
    DEV_HDCP_RECEIVER,
    DEV_HDCP_REPEATER
} HdcpDevType_t;

/* Used to inform Host about used HDCP version (HDCP_DP_CONFIG register) */
typedef enum {
    HDCP_VERSION_2X = 1U,
    HDCP_VERSION_1X = 2U
} HdcpVer_t;

#endif /* ENGINE_H */

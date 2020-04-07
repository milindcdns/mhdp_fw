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
 * general_handler.h
 *
 ******************************************************************************
 */

#ifndef GENERAL_HANDLER_H
#define GENERAL_HANDLER_H

#include <stdint.h>

#include "modRunner.h"

/**
 *  \brief opcode defines host->controller
 */
#define GENERAL_TEST_ACCESS             0x04

typedef enum
{
    GEN_MAINCTRL_SET_ACTIVE_BIT_MASK       = 1U << 0,
    GEN_MAINCTRL_SET_FAST_HDCP_DELAYS_MASK = 1U << 2,
    GEN_MAINCTRL_SET_ECC_ENABLE_MASK       = 1U << 3,
} GENERAL_MAIN_CONTROL_BIT_MASK;

typedef enum
{
    GEN_INJ_ECC_ERR_MEM_TYPE_IRAM = 1U,
    GEN_INJ_ECC_ERR_MEM_TYPE_DRAM = 2U,
} GEN_INJ_ECC_ERR_MEM_TYPE;

#define GEN_INJ_ECC_ERR_TYPE_DATA            1
#define GEN_INJ_ECC_ERR_TYPE_CHECK           2

/**
 *  \brief opcode defines controller->host
 */

#define GEN_MAINCTRL_RESP                0x01
#define GENERAL_TEST_ECHO_RESP           0x02

#define GENERAL_READ_REGISTER_RESP       0x07
#define GENERAL_WAIT_RESP                0x08

typedef struct
{
    uint32_t delay;
} S_GENERAL_HANDLER_DATA;

/**
 * General and DPTX command/response IDs
 */
typedef enum
{
    GENERAL_MAIN_CONTROL        = 0x01,
    GENERAL_TEST_ECHO           = 0x02,
    GENERAL_WRITE_REGISTER      = 0x05,
    GENERAL_WRITE_FIELD         = 0x06,
    GENERAL_READ_REGISTER       = 0x07,
    GENERAL_GET_HPD_STATE       = 0x11,
    GENERAL_WAIT                = 0x08,
    GENERAL_SET_WATCHDOG_CFG    = 0x09,
    GENERAL_INJECT_ECC_ERROR    = 0x0A,
    GENERAL_FORCE_FATAL_ERROR   = 0x0B
} GENERAL_MAILBOX_MSG_ID;

# define EVENTS_HDCPTX_CNT 4

/**
 * Function used to insert GENERAL_HANDLER module into context
 */
void GENERAL_Handler_InsertModule(void);

#endif /* GENERAL_HANDLER_H */


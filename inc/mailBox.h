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
 * mailBox.h
 *
 ******************************************************************************
 */

#ifndef MAIL_BOX_H
#define MAIL_BOX_H

#include "cdn_stdint.h"
#include "cdn_stdtypes.h"

#define MAIL_BOX_MAX_SIZE 1024
#define MAIL_BOX_MAX_TX_SIZE 1024

 /**
 *  \file mailBox.h
 *  \brief Implementation mail box communication channel between IP and external host
 */

typedef enum
{
    MB_MODULE_ID_DP = 0x01U,
    MB_MODULE_ID_HDCP = 0x07U,
    MB_MODULE_ID_HDCP_GENERAL = 0x09U,
    MB_MODULE_ID_GENERAL = 0x0AU,
} MB_MODULE_ID;


typedef enum
{
    MB_TYPE_REGULAR,
    MB_TYPE_SECURE,
    MB_TYPE_COUNT,
} MB_TYPE;

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
typedef enum
{
    MB_SUCCESS,
    MB_BUSY,
    MB_NO_MEMORY
} MB_RET;

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
typedef enum
{
    MB_TO_HOST,
    MB_TO_CONTROLLER,
} MB_IDX;

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
typedef enum
{
    MB_STATE_EMPTY,
    MB_STATE_WAIT_MODULE_ID,
    MB_STATE_WAIT_SIZE_MSB,
    MB_STATE_WAIT_SIZE_LSB,
    MB_STATE_READ_DATA,
    MB_STATE_MSG_READY,
} MB_RX_STATE;


/** Field offsets/indexes of mailbox tx and rx buffers */
typedef enum
{
    MB_TXRXBUFF_OPCODE_IDX = 0U,
    MB_TXRXBUFF_MODULE_ID_IDX = 1U,
    MB_TXRXBUFF_SIZE_MSB_IDX = 2U,
    MB_TXRXBUFF_SIZE_LSB_IDX = 3U,
    MB_TXRXBUFF_DATA_IDX = 4U,
} MB_TX_RX_BUFF_IDX;

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

typedef struct
{
    bool portTxBusy;
    MB_RX_STATE rxState;
    uint32_t  rx_data_idx;
    uint32_t  rx_final_msgSize;
    uint8_t rxBuff[MAIL_BOX_MAX_SIZE];
    uint8_t txBuff[MAIL_BOX_MAX_TX_SIZE];
    uint32_t txTotal;
    uint32_t txCur;
} S_MAIL_BOX_DATA;

/* Structure used to store message informations */
typedef struct {
    /* Code of operation (byte 0) */
    uint8_t opCode;
    /* Length of message in bytes (bytes 1 -2 ) */
    uint16_t length;
    /* Pointer to message buffer */
    uint8_t* message;
} MailboxData_t;

uint8_t* MB_GetTxBuff(MB_TYPE type);
bool MB_IsTxReady(MB_TYPE type);
void MB_SendMsg(MB_TYPE type, uint32_t len, uint8_t opCode, MB_MODULE_ID moduleId);
bool MB_isWaitingModuleMessage(MB_TYPE type, MB_MODULE_ID moduleId);
void MB_getCurMessage(MB_TYPE type, uint8_t **message, uint8_t *opCode, uint16_t *msgLen);
void MB_FinishReadMsg(MB_TYPE type);

/**
 * Function used to insert MAIL_BOX module into context
 */
void MB_InsertModule(void);

/**
 * Function used to insert SECURE_MAIL_BOX module into context
 */
void MB_Secure_InsertModule(void);

#endif //MAIL_BOX_H

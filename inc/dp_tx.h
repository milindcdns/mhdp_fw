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

#ifndef DP_TX_H
#define DP_TX_H

#include "modRunner.h"
#include "cdn_stdtypes.h"

/* Maximum length of data */
#define DP_MAX_DATA_LEN 16U

/**
 * Bitfields in DP requests
 */
typedef enum {
    /* Write transaction */
    DP_REQUEST_WRITE = 0x00U,
    /* Read transaction */
    DP_REQUEST_READ =  0x01U,
    /* Write status and update request transaction, available only for I2C */
    DP_REQUEST_WRITE_UPDATE = 0x02U,
} DpRequest_t;

typedef enum {
    /* I2C-native-AUX transaction */
    DP_REQUEST_TYPE_I2C = 0x00U,
    /* AUX transaction */
    DP_REQUEST_TYPE_AUX = 0x08U
} DpRequestType_t;

#define DP_REQUEST_MASK          0x03U
#define DP_REQUEST_I2C_MOT_MASK  0x04U
#define DP_REQUEST_TYPE_MASK     0x08U

/**
 * Transaction reply commands
 */
typedef enum {
    DP_REPLY_ACK   = 0x00U,
    DP_REPLY_NACK  = 0x01U,
    DP_REPLY_DEFER = 0x02U,
} DpReply_t;

#define DP_REPLY_MASK 0x03
#define DP_REPLY_I2C_OFFSET 2U

/* Defines used to identify extra error codes */
#define DP_AUX_REPLY_SINK_ERROR 0x03U
#define DP_AUX_REPLY_BUS_ERROR  0x04U

/* Due to specification, AUX response need 2 bits.
 * When AUX transaction is realized rest of bits is unused,
 * so, extra errors can be caught */
#define DP_AUX_REPLY_MASK       0x07U
/**
 * Request structure, for communication between policy and link layer
 */
typedef struct
{
    /* Command to execute (RD/WR, Native AUX,I2C) - use defines above */
    uint8_t command;
    /* Address to write in AUX/I2C request */
    uint32_t address;
    /* Number of bytes to read/write (may be more than 16) */
    uint32_t length;
    /* Bytes read/written by sink, needs to be checked by  policy*/
    uint32_t bytes_reply;
    /* Finish transaction (MOT=0) before the callback*/
    bool endTransaction;
    /* Bytes to write/buffer for read bytes, must be allocated by policy and have [bytes] length */
    uint8_t* buffer;
} DpTxRequestData_t;

/**
 *  Callback function given by policy and called after processing the request
 */
typedef void (*ResponseCallback_t)(const DpTxRequestData_t* reply);

/**
 *  Checks if another request can be added
 *  @return 'true' if can or 'false' if busy
 */
bool DP_TX_isAvailable(void);

/**
 * Attach module to system
 */
void DP_TX_InsertModule(void);

/**
 *  Set idle state, notify policy about connected sink - should
 *  be called by HPD interrupt handler
 */
void DP_TX_connect(void);

/**
 *  Set unplugged state, notify policy about disconnected sink - should
 *  be called by HPD interrupt handler
 */
void DP_TX_disconnect(void);

/**
 *  Set interrupt state, notify policy about HPD interrupt signal sink - should
 *  be called by HPD interrupt handler
 */
void DP_TX_interrupt(void);

/**
 *  Indicate that the data was sent from the Tx mailbox to the sink
 */
void DP_TX_setTxFlag(void);

/**
 *  Indicate that the data from the sink is waiting in the Rx mailbox
 */
void DP_TX_setRxFlag(void);

/**
 *  Add new request if possible, should be called by policy to read/write data
 */
void DP_TX_addRequest(DpTxRequestData_t* request, ResponseCallback_t callback);

/**
 * Stop execute of current request and cleanup state of DP TX module
 */
void DP_TX_removeRequest(DpTxRequestData_t* request, ResponseCallback_t callback);

/**
 * Initialize Hot-Plug detection
 */
void DP_TX_hdpInit(void);

#endif /*DP_TX_H*/

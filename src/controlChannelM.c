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
 * controlChannelM.c
 *
 ******************************************************************************
 */

/* parasoft-begin-suppress METRICS-36 "Function is called from more than 5 functions, DRV-3823" */

#include "controlChannelM.h"
#include "hdcp_tran.h"
#include "dp_tx.h"

/**
 * States of master control channel
 */
typedef enum {
    /* Channel is available to transmit */
    CONTROL_CHANNEL_MASTER_FREE = 0U,
    /* Channel do write transaction */
    CONTROL_CHANNEL_MASTER_TX_OFFSET = 1U,
    /* Channel do read transaction */
    CONTROL_CHANNEL_MASTER_RX_OFFSET = 2U
} ControlChannelMasterState;

/**
 * State of master control channel last transaction
 */
static volatile struct {
    /* If error occurred during transaction */
    uint8_t errorOccurred;
    /* Size of transaction */
    uint16_t totalSize;
    /* Actual state of channel */
    ControlChannelMasterState state;
} controlChannelMaster;

/**
 *  Data of AUX MAIL HANDLER
 */
static DpTxRequestData_t dpTxRequest;

/**
 *  Set 'errorOccurred' flag in case of transaction fail
 */
static inline void setTransactionError(void)
{
   controlChannelMaster.errorOccurred = 1U;
}

/**
 * Set channel state as free
 */
static inline void setTransactionOver(void)
{
    controlChannelMaster.state = CONTROL_CHANNEL_MASTER_FREE;
}

/**********************************************************************************************
 * Callbacks
 * Arguments of callback functions need to be structure to be compatible with rest of callbacks
 **********************************************************************************************
 */

/**
 * Callback for DPCD read operation
 * @param[in] reply, pointer to request structure
 */
static void readFromDpcdCb(const DpTxRequestData_t* reply)
{
    if (reply->bytes_reply != (uint32_t)controlChannelMaster.totalSize) {
        /* read error */
        setTransactionError();
    }

    /* end transaction */
    setTransactionOver();
}

/**
 * Callback for DPCD write operation
 * @param[in] reply, pointer to request structure
 */
static void writeToDpcdCb(const DpTxRequestData_t* reply)
{
    if (reply->bytes_reply != ((uint32_t)controlChannelMaster.totalSize - 1U)) {
        /* write error */
        setTransactionError();
    }

    /* end transaction */
    setTransactionOver();
}

/******************************************************************************************
 * Declarations of public functions
 ******************************************************************************************
 */

void CHANNEL_MASTER_transactionOver(void)
{
   setTransactionOver();
}

void CHANNEL_MASTER_init(void)
{
    /* Same operation as transactionOver - another function used to
       distinguish logic purpose */
    controlChannelMaster.state = CONTROL_CHANNEL_MASTER_FREE;
}

void CHANNEL_MASTER_write(uint16_t sizeOut, uint32_t offset, uint8_t* buff)
{
    if (sizeOut > (uint16_t)HDCP_TRANSACTION_BUFFER_SIZE) {
        /* Size of data is too large */
        setTransactionError();
    } else {
        /* Actualize transaction status */
        controlChannelMaster.state = CONTROL_CHANNEL_MASTER_TX_OFFSET;
        controlChannelMaster.errorOccurred = 0U;
        controlChannelMaster.totalSize = sizeOut + 1U;

        /* Create Tx request */
        dpTxRequest.address = offset;
        dpTxRequest.command = (uint8_t)DP_REQUEST_TYPE_AUX | (uint8_t)DP_REQUEST_WRITE;
        dpTxRequest.length = sizeOut;
        dpTxRequest.buffer = buff;

        /* Add request to queue */
        DP_TX_addRequest(&dpTxRequest, &writeToDpcdCb);
    }
}

bool CHANNEL_MASTER_isErrorOccurred(void)
{
    bool error = (controlChannelMaster.errorOccurred == 1U) ;
    controlChannelMaster.errorOccurred = 0U;
    return error;
}

void CHANNEL_MASTER_read(uint16_t sizeOut, uint32_t offset, uint8_t* buff)
{
    if (sizeOut > (uint16_t)HDCP_TRANSACTION_BUFFER_SIZE) {
        /* Size of data is too large */
        setTransactionError();
    } else {
        /* Actualize transaction status */
        controlChannelMaster.errorOccurred = 0U;
        controlChannelMaster.totalSize = sizeOut;
        controlChannelMaster.state = CONTROL_CHANNEL_MASTER_RX_OFFSET;

        /* Create Tx request */
        dpTxRequest.address = offset;
        dpTxRequest.command = (uint8_t)DP_REQUEST_TYPE_AUX | (uint8_t)DP_REQUEST_READ;
        dpTxRequest.length  = sizeOut;
        dpTxRequest.buffer  = buff;

        /* Add request to queue */
        DP_TX_addRequest(&dpTxRequest, &readFromDpcdCb);
    }
}

bool CHANNEL_MASTER_isFree(void)
{
    return  ((controlChannelMaster.state == CONTROL_CHANNEL_MASTER_FREE) &&  (DP_TX_isAvailable()));
}

/* parasoft-end-suppress METRICS-36 */

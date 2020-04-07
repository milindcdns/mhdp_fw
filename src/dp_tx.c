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

#include "dp_tx.h"
#include "dp_tx_mail_handler.h"
#include "timer.h"
#include "utils.h"
#include "reg.h"
#include "cdn_stdtypes.h"

/**
 * Configuration of DP_TX module
 */
/* Maximum number of tries when module reply is DP_REPLY_DEFER */
#define DP_MAX_DEFER_TRIES 7U
/* Time in microseconds for which module is slept after DP_REPLY_DEFER reply */
#define DP_DEFER_TIMEOUT_US 400U
/* Number transaction restarts if timeout was reached*/
#define DP_MAX_REPLY_TRIES 5U
/* Time in microsecond for which transaction can occupy physical lane */
#define DP_AUX_TRANSACTION_TIMEOUT_US 500U
/* Mask of AUX frame start bit */
#define DP_TX_FRAME_START 0x100U
/* Mask of AUX frame end bit */
#define DP_TX_FRAME_END 0x200U
/* Offset of command in first part of message */
#define DP_TX_COMMAND_OFFSET 4U
/* Mask of command in transaction byte */
#define DP_TX_COMMAND_MASK 0xF0U
/* Mask of data in transaction word (used to cut SYNC from data) */
#define DP_TX_DATA_MASK 0xFFU
/* Hot-Plug pulse minimal length in microseconds */
#define DP_TX_HPD_PULSE_MIN_LENGTH 500U
/* Hot-Plug pulse maximum length in microseconds */
#define DP_TX_HPD_PULSE_MAX_LENGTH 1000U
/* The maximum legal frequency receiving from the AUX line.
   Correspond to value 1.25 MHz*/
#define DP_TX_MAX_AUX_FREQ_KHZ 1250U
/* The minimum legal frequency receiving from AUX line.
   Corrsepond to value 0.83 MHz */
#define DP_TX_MIN_AUX_FREQ_KHZ 830U
/* Marigin (in percents) used to calculate max/min legal frequency for AUX channel */
#define DP_TX_AUX_FREQ_MARGIN 15U

/**
 * Structure used to call correct reply handlers
 */
typedef struct {
    /* Handler for ACK reply */
    void (*ackHandler)(void);
    /* Handler for NACK reply */
    void (*nackHandler)(void);
    /* Handler for DEFER reply */
    void (*deferHandler)(void);
} DpReplyHandlers_t;

/**
 *  Transaction structure for communication between Source and Sink
 */
typedef struct
{
    /* Command to execute (RD/WR, Native AUX,I2C) - use defines above/ response from sink */
    uint8_t command;
    /* Address to write in AUX/I2C request */
    uint32_t address;
    /* Number of bytes to read/write (from 0 to 15 (1-16 bytes)) */
    uint8_t length;
    /* Buffer of [DP_MAX_DATA_LEN] length */
    uint8_t buffer[DP_MAX_DATA_LEN];

} DpTxTransactionData_t;

typedef struct
{
	/* Current state */
    StateCallback_t stateCb;
    /* Data given by policy and returned to it */
    DpTxRequestData_t* requestData;
    /* Response from Sink / Request sent from Source in single transaction */
    DpTxTransactionData_t transactionData;
    /* Number of timeouts (give up transaction after 3 timeouts - DP doc) */
    uint8_t timeoutCounter;
    /* Number of defer responses (give up transaction after 7 defer - DP doc) */
    uint8_t deferCounter;
    /* Number of bytes to read/write in current transaction */
    uint8_t transaction_bytes;
    /* Number of remaining bytes for request from policy */
    uint32_t dataCounter;
    /* Current number of bytes received/written during transaction */
    uint8_t transactionDataCounter;
    /* I2C middle of transaction (MOT) flag*/
    bool motState;
    /* I2C repeated start to be performed during read flag */
    bool repeatedStart;
    /* Callback function given by policy */
    ResponseCallback_t policyCallback;
    /* Data transmission finished flag*/
    uint8_t txDoneIrqFlag;
    /* Data receive finished flag */
    uint8_t rxDoneIrqFlag;
    /* Unplug event flag */
    uint8_t unpluggedIrqFlag;
    /* Plug event flag */
    uint8_t pluggedIrqFlag;
    /* Plug-in flag */
    bool plugged;
} DpTxData_t;

static DpTxData_t dpTxData;

/******************************************
 * Handlers to DPTX module states actions.
 ******************************************/

/**
 * Thread action corresponding to TX transaction.
 * Checks if data were send to RX (TX_DONE interrupt occured)
 * or if timeout for transaction was reached.
 */
static void sendingHandler(void);

/**
 * Thread action corresponding to RX transaction.
 * Checks if data were send to TX (RX_DONE interrupt occured)
 * or if timeout was reached (if transmission was not started in DP_AUX_REPLY_TIMEOUT
 * or transmission was started but not finished in DP_AUX_TRANSACTION_TIMEOUT)
 */
static void waitForResponseHandler(void);

/**
 * Thread action responsible for request renewal if data exchange failed
 */
static void resendHandler(void);

/**
 * Thread action responsible for timeout service. Renew request or finish
 * if renewed DP_MAX_REPLY_TRIES before
 */
static void timeoutHandler(void);

/**
 * Function used to notify module about plugIn event
 */
static inline void checkSynchronization(void)
{
	/* Read HPD event register */
    uint32_t regVal = RegRead(HPD_EVENT_DET);

    /* Check if 'SYNC' bit was set */
    if (RegFieldRead(HPD_EVENT_DET, HPD_IN_SYNC, regVal) == 1U) {
        DP_TX_connect();
    }
}

/**
 * Used to check if next transaction is address-only. Called during
 * transaction header folding
 * @param[in] requestType, if transaction use AUX or I2C-native-AUX
 * @param[in] requestCode, if transaction is WRITE, READ or WRITE-UPDATE
 * @param[in] dataLen, expected number of bytes to READ/WRITE
 * @return true, if transaction is address-only
 * @return false, if transaction is not address-only (data length required)
 */
static inline bool isAddressOnlyTransaction(uint8_t requestType, uint8_t requestCode, uint8_t dataLen) {
    bool addressOnlyTran = ((requestType == (uint8_t)DP_REQUEST_TYPE_I2C) &&
                            (requestCode == (uint8_t)DP_REQUEST_WRITE_UPDATE)) ||
                            (dataLen == 0U);

    return addressOnlyTran;
}

/**
 * Used to check if next-transaction command should be enriched with
 * Middle-Of-Transaction (MOT) bit. Called during transaction header folding
 * @param[in] requestType, if transaction use AUX or I2C-native-AUX
 * @param[in] motState, Middle-Of-Transaction flag, set during adding request
 * @return true, if MOT bit should be set for transaction
 * @return false, if MOT bit is not required
 */
static inline bool isMiddleOfTransaction(uint8_t requestType, bool motState) {
    bool isMot = (requestType == (uint8_t)DP_REQUEST_TYPE_I2C) && motState;
    return isMot;
}

/**
 * Function used to send AUX header (3 or 4 bytes) into sink.
 * Header structure: |   [9:8]  |   [7:4]   |      [3:0]     |
 *                   0: | SYNC  |   command | address[20:17] |
 *                   1: | SYNC  |         address[16:8]      |
 *                   2: | SYNC  |         address[7:0]       |
 *                   3: | SYNC  |         dataLength         |
 */
static void sendRequestHeader(void)
{
    const uint32_t* address = &(dpTxData.transactionData.address);
    uint8_t command = dpTxData.transactionData.command;
    uint8_t requestType = command & (uint8_t)DP_REQUEST_TYPE_MASK;
    uint8_t requestCode = command & (uint8_t)DP_REQUEST_MASK;
    uint32_t dataWord;

    /* If DP_REQUEST_WRITE_UPDATE or dataLength (dpTxData.transaction_bytes) == 0U -> address-only-transaction */
    bool addressOnlyTransaction = isAddressOnlyTransaction(requestType, requestCode, dpTxData.transaction_bytes);

    if (isMiddleOfTransaction(requestType, dpTxData.motState)) {
        command |= DP_REQUEST_I2C_MOT_MASK;
    }

    dataWord = (uint32_t)DP_TX_FRAME_START
             | ((uint32_t)command << DP_TX_COMMAND_OFFSET)
             | (uint32_t)GetByte2(*address);

    RegWrite(DP_AUX_TX_DATA, dataWord);
    RegWrite(DP_AUX_TX_DATA, GetByte1(*address));

    if (addressOnlyTransaction) {
        /* Add DP_TX_FRAME_END sync */
        dataWord = (uint32_t)DP_TX_FRAME_END | (uint32_t)GetByte0(*address);
        RegWrite(DP_AUX_TX_DATA, dataWord);
    } else {
        RegWrite(DP_AUX_TX_DATA, GetByte0(*address));
        dataWord = (uint32_t)dpTxData.transaction_bytes - 1U;
        /* If transaction code is 'read', add DP_TX_FRAME_END flag into message and send number of bytes to read
           If transaction code is 'write', send number of bytes to write (look 2.8.7.1.5, p. 347)*/
        if (requestCode == (uint8_t)DP_REQUEST_READ) {
            /* Add DP_TX_FRAME_END sync */
            dataWord |= (uint32_t)DP_TX_FRAME_END;
        }

        RegWrite(DP_AUX_TX_DATA, dataWord);
    }
}

/**
 * Send requested data into a sink. Should be called only
 * for DP_REQUEST_WRITE transactions
 */
static inline void sendRequestData(void)
{
    uint32_t i;
    uint32_t dataLength = (uint32_t)dpTxData.transaction_bytes + dpTxData.requestData->bytes_reply;

    for (i = dpTxData.requestData->bytes_reply; i < (dataLength - 1U); i++) {
        RegWrite(DP_AUX_TX_DATA, dpTxData.requestData->buffer[i]);
    }

    /* Send last byte with end-frame flag */
    RegWrite(DP_AUX_TX_DATA, ((uint32_t)DP_TX_FRAME_END | (uint32_t)dpTxData.requestData->buffer[i]));
}

/**
 * Send transaction header and data, if transaction type is DP_REQUEST_WRITE
 */
static void sendRequest(void)
{
    /* Set request data length (same for RD and WR) */
    if (dpTxData.dataCounter > (uint32_t)DP_MAX_DATA_LEN) {
        dpTxData.transaction_bytes = (uint8_t)DP_MAX_DATA_LEN;
    } else {
        dpTxData.transaction_bytes = (uint8_t)dpTxData.dataCounter;
    }

    sendRequestHeader();

    if ((dpTxData.requestData->command & (uint8_t)DP_REQUEST_MASK) == (uint8_t)DP_REQUEST_WRITE) {
        /* If request type is DP_REQUEST_WRITE - send data */
        sendRequestData();
    }

    /* Start time measure and go to DP_TX_SENDING state */
    startTimer(DP_AUX_TRANSACTION_TIMER);
    dpTxData.stateCb = &sendingHandler;
}

/**
 * Read data from reply
 */
static inline void readData(void)
{
    uint8_t i;

    for (i = 0U; i < dpTxData.transactionData.length; i++) {
        dpTxData.requestData->buffer[dpTxData.requestData->bytes_reply] = dpTxData.transactionData.buffer[i];
        dpTxData.requestData->bytes_reply++;
    }
}

/**
 * Maximum length of transaction (both of AUX and I2C) is 16B.
 * This function allows to start next transaction if declared size of data
 * was greater than 16B by sending new request.
 */
static void startNextTransaction(DpRequestType_t reqType)
{
    /* For AUX transaction update address */
    if (reqType == DP_REQUEST_TYPE_AUX) {
        dpTxData.transactionData.address += dpTxData.transaction_bytes;
    }

    /* Clear counters */
    dpTxData.timeoutCounter = 0U;
    dpTxData.deferCounter = 0U;

    /* Prepare same command as previous transaction */
    dpTxData.transactionData.command = dpTxData.requestData->command;

    sendRequest();
}


/**
 * Clear registers responsible for response (RX->TX) transaction status
 */
static inline void resetRx(void) {
    /* Read register before write to avoid overwrite redundant bits */
    uint32_t regVal = RegRead(DP_AUX_CLEAR_RX);
    regVal = RegFieldSet(DP_AUX_CLEAR_RX, AUX_HOST_CLEAR_RX, regVal);
    RegWrite(DP_AUX_CLEAR_RX, regVal);
}

/**
 * Clear registers responsible for request (TX->RX) transaction status
 */
static inline void resetTx(void) {
    /* Read register before write to avoid overwrite redundant bits */
    uint32_t regVal = RegRead(DP_AUX_CLEAR_TX);
    regVal = RegFieldSet(DP_AUX_CLEAR_TX, AUX_HOST_CLEAR_TX, regVal);
    RegWrite(DP_AUX_CLEAR_TX, regVal);
}
/**
 * Clear registers responsible for informing device about AUX transactions
 */
static inline void resetAux(void) {
    /* Reset status registers for both directions (TX and RX) */
    resetTx();
    resetRx();
}

/**
 * Send DP_REQUEST_WRITE_UPDATE command. Function should be called when replier return information,
 * that number of written data is lower than required
 */
static void updateStatusI2C(void)
{
    uint8_t bytesDiff = dpTxData.transactionData.buffer[0] - dpTxData.transactionDataCounter;
    resetAux();

    /* Bytes replied is disparity of expected and received data */
    dpTxData.requestData->bytes_reply += (uint32_t)bytesDiff;
    dpTxData.transactionDataCounter = dpTxData.transactionData.buffer[0];

    /* Prepare DP_REQUEST_WRITE_UPDATE command */
    dpTxData.transactionData.command = (uint8_t)DP_REQUEST_TYPE_I2C | (uint8_t)DP_REQUEST_WRITE_UPDATE;

    /* Clear counters */
    dpTxData.timeoutCounter = 0U;
    dpTxData.deferCounter = 0U;

    /* Go to state corresponding with RX response*/
    dpTxData.stateCb = &waitForResponseHandler;

    /* [DP_TX]>>>WRITE I2C ACK [%d bytes written, try again with CMD [0x%x]] */
}

/**
 * Handler to DP_TX callback. Should be called always when request (or sequence of requests)
 * was finished.
 */
/* parasoft-begin-suppress METRICS-36 "Function is called from more than 5 functions, DRV-3823" */
static void finishRequest(void)
{
    if (dpTxData.policyCallback != NULL) {
        /* If callback is not NULL, call itto finish request */
        dpTxData.requestData->command = dpTxData.transactionData.command;
        dpTxData.policyCallback(dpTxData.requestData);
        dpTxData.policyCallback = NULL;
    }

    /* Cleanup interrupt flags to be sure that no previous interrupts will be used */
    dpTxData.rxDoneIrqFlag = 0U;
    dpTxData.txDoneIrqFlag = 0U;

    /* Clear transaction registers */
    resetAux();

    /* Go to DP_TX_IDLE state */
    dpTxData.stateCb = NULL;
}
/* parasoft-end-suppress METRICS-36 */

/**
 * Check if response word have DP_TX_FRAME_END indicator
 * @param[in] responseData, analyzed response word
 * @return 'true' if contain or 'false' if not
 */
static inline bool isEndOfFrame(uint32_t responseData)
{
    return (responseData & DP_TX_FRAME_END) != 0U;
}

/**
 * Function read response and put data into dpTxData structure
 */
static void getResponse(void)
{
    uint32_t responseData = RegRead(DP_AUX_RX_DATA);
    uint32_t regVal;

    /* Cast responseData to uint8_t to extract only command [7:4] */
    dpTxData.transactionData.command = ((uint8_t)responseData & (uint8_t)DP_TX_COMMAND_MASK) >> DP_TX_COMMAND_OFFSET;

    dpTxData.transactionData.length = 0U;

    while ((dpTxData.transactionData.length < (uint8_t)DP_MAX_DATA_LEN) && (!isEndOfFrame(responseData)))
    {
        responseData = RegRead(DP_AUX_RX_DATA);
        dpTxData.transactionData.buffer[dpTxData.transactionData.length] = ((uint8_t)responseData & (uint8_t)DP_TX_DATA_MASK);
        dpTxData.transactionData.length++;
    }

    /* Set sink error when end sync was not received */
    if (!isEndOfFrame(responseData)) {
    	dpTxData.transactionData.command = (uint8_t)DP_AUX_REPLY_SINK_ERROR;
    }

    /* Cleanup RX transaction status */
    resetRx();
}

/**
 * Calculate ratio between sys_clk and 2MHz.
 */
static inline uint32_t calculateClockRatio(void)
{
    /* Look for description of DP_AUX_DIVIDE_2M register */
    return (CPU_CLOCK_MEGA / 2U) - 1U;
}

/**
 * Calculate maximum number of cycles per AUX transaction
 * Look for description of DP_AUX_FREQUENCY_1M_MAX
 * @return maximum number of cycles
 */
static inline uint32_t calcMaxFreqRate(void) {
    /*  Margin is extended by 1000 (instead 115% -> 1.15 used 1150) to compensate diffrences in freqs units (MHz to KHz) */
    uint32_t margin = (100U + DP_TX_AUX_FREQ_MARGIN) * 10U;
    uint32_t retVal = (margin * CPU_CLOCK_MEGA) / DP_TX_MIN_AUX_FREQ_KHZ;
    return retVal;
}

/**
 * Calculate minimum number of cycles per AUX transaction
 * Look for description of DP_AUX_FREQUENCY_1M_MIN
 * @return minimum number of cycles
 */
static inline uint32_t calcMinFreqRate(void) {
    /*  Margin is extended by 1000 (instead 85% -> 0.85 used 850) to compensate diffrences in freqs units (MHz to KHz) */
    uint32_t margin = (100U - DP_TX_AUX_FREQ_MARGIN) * 10U;
    uint32_t retVal = (margin * CPU_CLOCK_MEGA) / DP_TX_MAX_AUX_FREQ_KHZ;
    return retVal;
}

/**
 * Function used to set maximum and minimum frequency of receiving.
 * Maximum is 1.25 MHz and minimum is 0.83 MHz (by standard)
 */
static inline void setFrequencyRange(void)
{
    uint32_t regVal = calcMaxFreqRate();
    RegWrite(DP_AUX_FREQUENCY_1M_MAX, regVal);

    regVal = calcMinFreqRate();
    RegWrite(DP_AUX_FREQUENCY_1M_MIN, regVal);
}

/*************************************************************
 * Handlers of AUX channel response checkers
 *************************************************************
 */

/**
 * Function handler for AUX ACK reply.
 * For write: replier has received a request and complete transaction
 * For read: replier has received request and: complete transaction or replier is ready
 * only with some data (M+1 bytes) or reply with requested bytes
 */
static void processResponseAckAux(void)
{
    uint32_t i;
    bool dataIncompleteErr = false;

    /* For AUX DP_REQUEST_WRITE_UPDATE is not used */
    if ((dpTxData.requestData->command & (uint8_t)DP_REQUEST_MASK) == (uint8_t)DP_REQUEST_READ) {

        /* "[DP_TX]>>>READ AUX ACK [%d bytes received]" */
        /* Read received data */
        readData();

        /* Reply sent only part of data, so finish transaction */
        if (dpTxData.transaction_bytes != dpTxData.transactionData.length) {
            /* "[DP_TX]>>>READ AUX ACK [Data not complete, back to callback]" */
            dpTxData.dataCounter = 0U;
            finishRequest();
            dataIncompleteErr = true;
        }

    } else {
        /* "[DP_TX]>>>WRITE AUX ACK [all data written]"  */
        dpTxData.requestData->bytes_reply += dpTxData.transaction_bytes;
    }

    if (!dataIncompleteErr) {

        dpTxData.dataCounter -= (uint32_t)dpTxData.transaction_bytes;

        if (dpTxData.dataCounter > 0U) {
            /* Not all data was processed yet, so send next pack of data
               "[DP_TX]>>>AUX ACK [not all data processed, continue with CMD [0x%x] ADDR [0x%x]]" */
            startNextTransaction(DP_REQUEST_TYPE_AUX);
        } else {
            /* "[DP_TX]>>>AUX ACK [request complete, back to callback]" */
            finishRequest();
        }
    }
}

/**
 * Function handler for AUX NACK (Negative-Acknowledge) reply, used only for 'write' transactions
 * Replier has received a write request, but has not completed the write.
 * Buffer contain number of data written so far.
 */
static void processResponseNackAux(void)
{
    /* "[DP_TX]>>>AUX NACK [back to callback]" */
    dpTxData.requestData->bytes_reply += dpTxData.transactionData.buffer[0];
    finishRequest();
}

/**
 * Function handler for AUX DEFER reply.
 * Replier has received a request , but cannot complete a transaction
 */
static void processResponseDeferAux(void)
{
    /* Sink is not ready, try again up to DP_MAX_DEFER_TRIES times */
    if (dpTxData.deferCounter < DP_MAX_DEFER_TRIES) {
        /* After defer try again up to DP_MAX_DEFER_TRIES times in a row make some delay before another try
           "[DP_TX]>>>AUX DEFER [try again CMD [0x%x]]" */
        modRunnerSleep(DP_DEFER_TIMEOUT_US);
        dpTxData.deferCounter++;
        dpTxData.transactionData.command = dpTxData.requestData->command;
        dpTxData.stateCb = &resendHandler;
    } else {
        /* After DP_MAX_DEFER_TRIES defer replies, give up
           [DP_TX]>>>AUX DEFER [give up] */
        finishRequest();
    }
}

/*************************************************************
 * Handlers of I2C-over-AUX channel response checkers
 *************************************************************
 */

/**
 * Function handler for I2C ACK reply.
 * For write: DPRX write to its I2C slave M-bytes of data without NACK or all requested data if
 *               M-bytes are omitted
 * For read: I2C address is correct, data was received correctly or some M-bytes of data was received and
 *              rest of data is unavailable
 */
static void processResponseAckI2C(void)
{
    uint32_t i;
    bool statusUpdating = false;
    uint8_t byteDiff;

    uint8_t reqCode  = dpTxData.requestData->command & (uint8_t)DP_REQUEST_MASK;

    switch (reqCode) {
    case ((uint8_t)DP_REQUEST_READ):
        /* [DP_TX]>>>READ I2C ACK [%d bytes received]
           Sink replied with some bytes */
        readData();
        dpTxData.transaction_bytes = dpTxData.transactionData.length;
        break;
    case ((uint8_t)DP_REQUEST_WRITE):
        /* ACK response for write request */
        if (dpTxData.transactionData.length > 0U) {
            /* If reply contain something more than ACK, it means that not all data was written,
               need to send DP_REQUEST_WRITE_UPDATE command and wait */
            updateStatusI2C();
            statusUpdating = true;

        } else {
            /* All bytes was written - [DP_TX]>>>WRITE I2C ACK [%d bytes written] */
            byteDiff = dpTxData.transaction_bytes - dpTxData.transactionDataCounter;
            dpTxData.requestData->bytes_reply += (uint32_t)byteDiff;
            dpTxData.transactionDataCounter = 0U;
        }
        break;
    default:
        /* No operation for DP_REQUEST_WRITE_UPDATE */
        break;
    }

    if (!statusUpdating) {
        dpTxData.dataCounter -= (uint32_t)dpTxData.transaction_bytes;

        if (dpTxData.dataCounter > 0U) {
            /* Not all data was processed yet */
            startNextTransaction(DP_REQUEST_TYPE_I2C);
        } else {
            /* All data was successfully processed */
            if (dpTxData.repeatedStart) {
                dpTxData.repeatedStart = false;
                /* [DP_TX]>>>I2C ACK [all data processed, repeated start CMD [0x%x] LEN[%d]] */
                dpTxData.dataCounter = dpTxData.requestData->length;
                dpTxData.requestData->command = (uint8_t)DP_REQUEST_TYPE_I2C | (uint8_t)DP_REQUEST_READ;

            } else if (dpTxData.motState && dpTxData.requestData->endTransaction) {
                /* Send address only MOT = 0 request to stop I2C transaction
                   [DP_TX]>>>I2C ACK [all data processed, send MOT=0 address only request] */
                dpTxData.motState = false;
                dpTxData.transactionData.command = dpTxData.requestData->command;
                dpTxData.stateCb = &waitForResponseHandler;

            } else {
                /* [DP_TX]>>>I2C ACK [all data processed, back to callback] */
                finishRequest();
            }
        }
    }
}

static void processResponseNackI2C(void)
{
    /* [DP_TX]>>>I2C NACK [finish transaction with MOT=0, CMD [0x%x]] */

    uint8_t byteDiff;

    /* Callback function with error state, handle M byte */
    if (dpTxData.transactionData.length > 0U) {
        //M byte present indicationg number of written bytes
        byteDiff = dpTxData.transactionData.buffer[0] - dpTxData.transactionDataCounter;
        dpTxData.requestData->bytes_reply += (uint32_t)byteDiff;
        dpTxData.transactionDataCounter = 0U;
        dpTxData.dataCounter = 0U;
        dpTxData.motState = false;
        dpTxData.transactionData.command = dpTxData.requestData->command;
        dpTxData.stateCb = &waitForResponseHandler;

    } else {
        dpTxData.requestData->command = dpTxData.transactionData.command;
        finishRequest();
    }

    if (dpTxData.repeatedStart) {
        dpTxData.repeatedStart = false;
        dpTxData.requestData->bytes_reply = 0U;
    }
}

static void processResponseDeferI2C(void)
{
    /* Sink is not ready, try again up to DP_MAX_DEFER_TRIES times */
    if (dpTxData.deferCounter < DP_MAX_DEFER_TRIES) {
        /* After defer try again up to DP_MAX_DEFER_TRIES times in a row
           Make some delay before another try */
        modRunnerSleep(DP_DEFER_TIMEOUT_US);
        dpTxData.deferCounter++;
        if ((dpTxData.requestData->command & (uint8_t)DP_REQUEST_MASK) == (uint8_t)DP_REQUEST_WRITE) {
            /* In case of write send write update
               [DP_TX]>>>I2C DEFER [try again write update CMD [0x%x]] */
            dpTxData.transactionData.command = (uint8_t)DP_REQUEST_TYPE_I2C
                                              | (uint8_t)DP_REQUEST_WRITE_UPDATE;
        } else {
            /* In case of read repeat the same request
               [DP_TX]>>>I2C DEFER [try again read CMD [0x%x]] */
            dpTxData.transactionData.command = dpTxData.requestData->command;
        }

        dpTxData.stateCb = &resendHandler;

    } else {
        if (dpTxData.motState) {
            /* After DP_MAX_DEFER_TRIES defer replies, give up and send MOT = 0 to stop I2C transaction
               [DP_TX]>>>I2C DEFER [give up, send MOT=0 CMD [0x%x]] */
            dpTxData.dataCounter = 0U;
            dpTxData.transactionDataCounter = 0U;
            dpTxData.motState = false;
            dpTxData.transactionData.command = dpTxData.requestData->command;
            dpTxData.stateCb = &resendHandler;
        } else {
            finishRequest();
        }
    }
}

/**
 * Handler for incorrect response.
 */
static void incorrectResponseHandler(void)
{
    /* Handle incorrect response, clear flags, go to callback */
    dpTxData.dataCounter = 0U;
    dpTxData.transactionDataCounter = 0U;
    dpTxData.motState = false;
    finishRequest();
}

/**
 * Function used to handle replies
 * @param[in] replyCode, code of handle transaction (ACK, NACK, DEFER)
 * @param[in] replyCounters, pointer to structure with handlers to correct type of transaction (AUX or I2C)
 */
static void responseHandler(uint8_t replyCode, const DpReplyHandlers_t* replyHandlers)
{
    switch (replyCode)
    {
    case (uint8_t)DP_REPLY_ACK:
        replyHandlers->ackHandler();
        break;
    case (uint8_t)DP_REPLY_NACK:
        replyHandlers->nackHandler();
        break;
    case (uint8_t)DP_REPLY_DEFER:
        replyHandlers->deferHandler();
        break;
    default:
        /* [DP_TX]>>>{DP_REQUEST_TYPE} PROCESS [incorrect response [0x%x]] */
        incorrectResponseHandler();
        break;
    }
}

/**
 * Function used to call correct reply handler (ACK, NACK, DEFER for AUX or I2C)
 */
static void processHandler(void)
{
    /* Pack of function used to handle AUX replies */
    static const DpReplyHandlers_t auxHandlers = {
        .ackHandler   = processResponseAckAux,
        .nackHandler  = processResponseNackAux,
        .deferHandler = processResponseDeferAux
    };

    /* Pack of function used to handle I2C replies */
    static const DpReplyHandlers_t i2cHandlers = {
        .ackHandler   = processResponseAckI2C,
        .nackHandler  = processResponseNackI2C,
        .deferHandler = processResponseDeferI2C
    };

    /* AUX reply command */
    uint8_t auxResponse = dpTxData.transactionData.command & (uint8_t)DP_REPLY_MASK;
    /* I2C reply command */
    uint8_t i2cResponse = (dpTxData.transactionData.command >> (uint8_t)DP_REPLY_I2C_OFFSET) & (uint8_t)DP_REPLY_MASK;

    dpTxData.timeoutCounter = 0U;

    if ((dpTxData.requestData->command & (uint8_t)DP_REQUEST_TYPE_MASK) == (uint8_t)DP_REQUEST_TYPE_AUX) {
        /* Response to AUX request */
        responseHandler(auxResponse, &auxHandlers);
    } else {
        /* Response to I2C request. Check if AUX part is ACK */
        if (auxResponse == (uint8_t)DP_REPLY_ACK) {
            responseHandler(i2cResponse, &i2cHandlers);
        } else if (auxResponse == (uint8_t)DP_REPLY_DEFER) {
            processResponseDeferAux();
        } else {
            /* Handle incorrect response, clear flags, go to callback */
            incorrectResponseHandler();
        }
    }
}

/********************************************
 * Handlers to DPTX module states actions.
 * This functions should be not called from
 * another functions that DP_TX_thread
 ********************************************
 */

static void waitForResponseHandler(void) {

    uint32_t timerVal;

    static bool auxRxInProcess = false;

    /* If interrupt occured, data are ready to read */
    if (dpTxData.rxDoneIrqFlag == 1U) {
        dpTxData.rxDoneIrqFlag = 0U;
        auxRxInProcess = false;

        /* [DP_TX]>>>STATE PENDING [Response ready]
           Data available, pack it into response structure */
        getResponse();
        dpTxData.stateCb = &processHandler;
    } else {
        /* Calculate time since reply transaction start */
        timerVal = getTimerUsWithoutUpdate(DP_AUX_TRANSACTION_TIMER);
        /* Check if timeout was reached */
        if (timerVal >= DP_AUX_TRANSACTION_TIMEOUT_US) {
            auxRxInProcess = false;
            dpTxData.stateCb = &timeoutHandler;
        }
    }
}

static void timeoutHandler(void) {
  /* If number of replies was not exceeded, try again */
    if (dpTxData.timeoutCounter < DP_MAX_REPLY_TRIES) {
        /* [DP_TX]>>>STATE PENDING [Timeout counter [%d]] */

        /* Reset RX and TX status registers */
        resetAux();

        /* Cleanup interrupt flags to be sure that no previous interrupts will be used */
        dpTxData.rxDoneIrqFlag = 0U;
        dpTxData.txDoneIrqFlag = 0U;

        dpTxData.timeoutCounter++;
        dpTxData.stateCb = &resendHandler;
    } else {
        /* [DP_TX]>>>STATE PENDING [Timeout]
            timeout after DP_MAX_REPLY_TRIES send tries */
        finishRequest();
    }
}

static void sendingHandler(void)
{
    uint32_t regVal;

    if (dpTxData.txDoneIrqFlag == 1U) {

        startTimer(DP_AUX_TRANSACTION_TIMER);

        dpTxData.txDoneIrqFlag = 0U;

        /* Cleanup TX status registers */
        resetTx();

        /* Go to state corresponding with RX response*/
        dpTxData.stateCb = &waitForResponseHandler;
    }
    else {
        /* Check if timeout was reached */
        if (getTimerUsWithoutUpdate(DP_AUX_TRANSACTION_TIMER) > (uint32_t)DP_AUX_TRANSACTION_TIMEOUT_US) {
            finishRequest();
        }
    }
}

static void resendHandler(void) {
    /* Try again to send same request */
    sendRequest();
}

/**
 * Handler for plugged interrupt
 */
static void unplugHandler(void)
{
    /* Clear interrupt flag */
    dpTxData.unpluggedIrqFlag = 0U;

    if (dpTxData.plugged) {
        /* Finish existing requests */
        finishRequest();

        dpTxData.plugged = false;

        DP_TX_MAIL_HANDLER_notifyHpdEv(DP_TX_EVENT_CODE_HPD_LOW);
    }
}

/**
 * Handler for unplugged interrupt
 */
static void plugInHandler(void)
{
	uint8_t evCode;

    /* Clear interrupt flag */
    dpTxData.pluggedIrqFlag = 0U;

    if (!dpTxData.plugged) {
        /* In case of re-plug event check also the link stable event */
        finishRequest();

        dpTxData.plugged = true;

        evCode = (uint8_t)DP_TX_EVENT_CODE_HPD_STATE_HIGH | (uint8_t)DP_TX_EVENT_CODE_HPD_HIGH;
        DP_TX_MAIL_HANDLER_notifyHpdEv(evCode);
    }
}

/*
 **********************************************************************
 * Public functions
 **********************************************************************
 */

/**
 * Main thread of  DP_TX module. It is static, but is called from
 * another module by pointer to function.
 */
static void DP_TX_thread(void)
{
    /* Handle plugged/unplugged action interrupt */
    if (dpTxData.unpluggedIrqFlag != 0U) {
        unplugHandler();
    }

    if (dpTxData.pluggedIrqFlag != 0U) {
        plugInHandler();
    }

    /* Perform action for current state */
    if (dpTxData.stateCb != NULL) {
        (*dpTxData.stateCb)();
    }
}

/**
 * Function used to start DP_TX module. It is static, but is called from
 * another module by pointer to function.
 */
static void DP_TX_start(void)
{
    modRunnerWakeMe();
}

/**
 * Function used to initialize DP_TX module. It is static, but is called from
 * another module by pointer to function.
 */
static void DP_TX_init(void)
{
    uint32_t regVal;

    dpTxData.stateCb = NULL;
    dpTxData.plugged = false;
    dpTxData.txDoneIrqFlag = 0U;
    dpTxData.rxDoneIrqFlag = 0U;

    regVal = calculateClockRatio();
    RegWrite(DP_AUX_DIVIDE_2M, regVal);

    setFrequencyRange();

    regVal = RegRead(DP_AUX_HOST_CONTROL);
    regVal = RegFieldSet(DP_AUX_HOST_CONTROL, AUX_HOST_TRANSMIT_IMMEDIATE, regVal);
    RegWrite(DP_AUX_HOST_CONTROL, regVal);

    /* Clear RX and TX transactions registers */
    resetAux();

    /* Stop timer */
    regVal = RegFieldSet(DP_AUX_TIMER_STOP, AUX_HOST_STOP_TIMER, 0U);
    RegWrite(DP_AUX_TIMER_STOP, regVal);

    /* Check if synchronization was achieved */
    checkSynchronization();
}

bool DP_TX_isAvailable(void)
{
    /* Device if plugged and no operation is currently done */
    bool isAvail = (dpTxData.plugged) && (dpTxData.stateCb == NULL);
    return isAvail;
}

void DP_TX_connect(void)
{
    /* Set plugIn interrupt flag */
    dpTxData.pluggedIrqFlag = 1U;
}

void DP_TX_setTxFlag(void)
{
    /* Set sink Tx done flag */
    dpTxData.txDoneIrqFlag = 1U;
}

void DP_TX_setRxFlag(void) {
    /* Set sink Rx done flag */
    dpTxData.rxDoneIrqFlag = 1U;
}

void DP_TX_disconnect(void)
{
    /* Set unplugged interrupt flag */
    dpTxData.unpluggedIrqFlag = 1U;
}

void DP_TX_interrupt(void)
{
    /* Call Hot-Plug policy interrupt handler */
	uint8_t evCode = (uint8_t)DP_TX_EVENT_CODE_HPD_STATE_HIGH
			       | (uint8_t)DP_TX_EVENT_CODE_HPD_PULSE;

	DP_TX_MAIL_HANDLER_notifyHpdEv(evCode);
}

void DP_TX_removeRequest(DpTxRequestData_t* request, ResponseCallback_t callback)
{
    /* Save removed request data (to finish request) */
    dpTxData.requestData = request;
    dpTxData.transactionData.address = request->address;
    dpTxData.policyCallback = callback;

    /* Clear status of transaction */
    dpTxData.motState = false;
    dpTxData.requestData->bytes_reply = 0U;
    dpTxData.timeoutCounter = 0U;
    dpTxData.deferCounter = 0U;
    dpTxData.transactionDataCounter = 0U;
    dpTxData.dataCounter = 0U;

    /* End current transaction */
    finishRequest();
}

void DP_TX_addRequest(DpTxRequestData_t* request, ResponseCallback_t callback)
{
    dpTxData.requestData = request;
    dpTxData.transactionData.address = request->address;
    dpTxData.policyCallback = callback;
    dpTxData.transactionData.command = request->command;

    /* Clear counters */
    dpTxData.requestData->bytes_reply = 0U;
    dpTxData.timeoutCounter = 0U;
    dpTxData.deferCounter = 0U;
    dpTxData.transactionDataCounter = 0U;

    dpTxData.dataCounter = request->length;

    /* Set MOT (middle-of-transaction) state */
    if (((request->command & (uint8_t)DP_REQUEST_TYPE_MASK) == (uint8_t)DP_REQUEST_TYPE_I2C) && (request->length > 0U)) {
        /* [DP_TX]>>>ADD I2C REQUEST [CMD [0x%x] MOT [%d] REP_START [%d]] */
        dpTxData.motState = true;
    } else {
        /* [DP_TX]>>>ADD AUX REQUEST [CMD [0x%x]] */
        dpTxData.motState = false;
    }

    sendRequest();
}

void DP_TX_hdpInit(void)
{
    /* Set timer values as (xx * CPU_CLOCK_MEGA) where xx is time in microseconds
       Look for Table 3-3, page 584 */
    uint32_t regVal = DP_TX_HPD_PULSE_MIN_LENGTH * CPU_CLOCK_MEGA;
    RegWrite(HPD_IRQ_DET_MIN_TIMER, regVal);

    regVal =  DP_TX_HPD_PULSE_MAX_LENGTH * CPU_CLOCK_MEGA;
    RegWrite(HPD_IRQ_DET_MAX_TIMER, regVal);
}

void DP_TX_InsertModule(void)
{
    /* Have to be static to allow access from modRunner module */
    static Module_t dpTxModule;

    /* Assign thread functions into pointers */
    dpTxModule.initTask = &DP_TX_init;
    dpTxModule.startTask = &DP_TX_start;
    dpTxModule.thread = &DP_TX_thread;

    dpTxModule.moduleId = MODRUNNER_MODULE_DP_AUX_TX;

    /* Set priority of module */
    dpTxModule.pPriority = 0U;

    /* Attach module to system */
    modRunnerInsertModule(&dpTxModule);
}

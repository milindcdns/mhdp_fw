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
 * dp_tx_mail_handler.c
 *
 ******************************************************************************
 */

#include "dp_tx_mail_handler.h"
#include "dp_tx.h"
#include "utils.h"
#include "general_handler.h"
#include "timer.h"
#include "reg.h"
#include "cdn_stdtypes.h"
#include "mailBox.h"
#include "apbChecker.h"
#include "events.h"

/* Size of the EDID block */
#define EDID_LENGTH 128

/* Size of DP_TX_MAIL_HANDLER module buffer */
#define DP_TX_MAIL_HANDLER_BUFFER_LEN 1024

/* Minimum size of message for different types of operation
 * Message starts on byte [4] of transaction */
#define DP_TX_EDID_RESP_MSG_MIN_SIZE 2U
#define DP_TX_ADJUST_MSG_MIN_SIZE    3U
#define DP_TX_I2C_RESP_MSG_MIN_SIZE  3U
#define DP_TX_I2C_REQ_MSG_MIN_SIZE   4U
#define DP_TX_DPCD_MSG_MIN_SIZE      5U

/* Address of DPCD registers (chapter 2.9.3.2 of DP specification) */
#define DPCD_TRAINING_LANE0_SET_ADDR    0x00103U
#define DPCD_TRAINING_LANE0_STATUS_ADDR 0x00202U
#define DPCD_POWER_CONTROL_ADDR          0x00600U

/* Size of message header*/
#define DP_TX_HEADER_SIZE 4U

/* Range of I2C devices addresses */
#define DP_TX_I2C_ADDRESS_RANGE 127U

/* Timeout of sending */
#define DP_TX_MAIL_HANDLER_TIMEOUT_MS 2000U

/* These addresses does not include the direction bit.
   They correspond to addresses 0x60 / 0x61 and 0xA0 / 0xA1
   on I2C bus */
#define EDID_SEGMENT_SLAVE_ADDRESS 0x30U
#define EDID_SLAVE_ADDRESS         0x50U

/* Request codes (host->controller) received via mailbox*/
typedef enum {
    DPTX_SET_POWER_MNG       = 0x00U,
    DPTX_GET_EDID            = 0x02U,
    DPTX_READ_DPCD           = 0x03U,
    DPTX_WRITE_DPCD          = 0x04U,
    DPTX_ENABLE_EVENT        = 0x05U,
    DPTX_WRITE_REGISTER      = 0x06U,
    DPTX_WRITE_FIELD         = 0x08U,
    DPTX_READ_EVENT          = 0x0AU,
    DPTX_GET_LAST_AUX_STATUS = 0x0EU,
    DPTX_HPD_STATE           = 0x11U,
    DPTX_LT_ADJUST           = 0x12U,
    DPTX_I2C_READ            = 0x15U,
    DPTX_I2C_WRITE           = 0x16U,
    DPTX_GET_LAST_I2C_STATUS = 0x17U
} DpTxMailRequest_t;

#define NUMBER_OF_REQ_OPCODES     14U

/* Response codes (controller->host) received via mailbox */
typedef enum {
    DPTX_EDID_RESP          =  0x02U,
    DPTX_DPCD_READ_RESP     =  0x03U,
    DPTX_DPCD_WRITE_RESP    =  0x04U,
    DPTX_READ_EVENT_RESP    =  0x0AU,
    DPTX_I2C_READ_RESP      =  0x15U,
    DPTX_I2C_WRITE_RESP     =  0x16U
} DpTxResponse_t;

/* Pointer to mailbox requests handlers */
typedef void (*MessageHandler_t)(const MailboxData_t* mailboxData);

/* Structure used to connect opCodes with handlers */
typedef struct {
    /* Pointer to message handler*/
    MessageHandler_t msgHandler;
    /* OpCode connected with handler */
    DpTxMailRequest_t opCode;
} MsgAction_t;

/* Data of DP mail handler */
typedef struct {
    /* Mail handler state function*/
    StateCallback_t stateCb;
    /* Request structure for link layer communication */
    DpTxRequestData_t request;
    /* Request data buffer */
    uint8_t buffer[DP_TX_MAIL_HANDLER_BUFFER_LEN];
    /* Request callback function pointer */
    ResponseCallback_t callback;
    /* Response data length */
    uint32_t responseLength;
    /* Response opcode for the host */
    uint8_t responseOpcode;
    /* Number of the requested segment */
    uint8_t segmentNumber;
    /* Offset of the EDID data to read */
    uint8_t edidOffset;
    /* Status of current enabled events */
    uint8_t enabledEvFlags;
    /* Buffer with last event details for the host - sent upon request */
    uint8_t eventDetails;
    /* Last received message bus - APB or SAPB */
    MB_TYPE messageBus;
    /* Last error for aux - Sink response state (0 ACK, 1 NACK, 2 DEFER,
     * 3 sink error (when there is no stop condition), 4 bus error - SAPB used)*/
    uint8_t latestAuxError;
    /* Last error for I2C - Sink response state (0 ACK, 1 NACK, 2 DEFER) */
    uint8_t latestI2cError;
    /* Indicates, how long to wait (in us) before reading DPCD registers
     * and responding. Value != 0 also means, that current DPCD write/read
     * is related to Link Training. */
    uint16_t wait_time;
} DpTxMailHandlerData_t;

static DpTxMailHandlerData_t dpTxMailHandlerData;

uint8_t hpdState;

/* Handler for IDLE state */
static void idleHandler(void);

/* Handler for SENDING_MESSAGE state */
static void sendMessageHandler(void);

/* Handler for LT_DPCD_READ state */
static void readLinkTrainingResultHandler(void);

/* Handler for LT_WAIT state */
static void linkTrainingWaitHandler(void);

/* Handler for WAITING_LINK_RESPONSE state */
static void timeoutHandler(void);

/* Handler for PROCESSING_RX state */
static void rxProcessingHandler(void);

/**
 * Return length of data for I2C-native-AUX request.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint16_t getI2cDataLen(const uint8_t* message) {
    return getBe16(message);
}

/**
 * Return address of slave for I2C-native-AUX request.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getI2cAddress(const uint8_t* message) {
    return message[2];
}

/**
 * Return if requested for I2C-native-AUX transaction is MOT.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getI2cMot(const uint8_t* message) {
    return message[3];
}

/**
 * Return offset of EDID for I2C-native-AUX transaction.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getEdidOffset(const uint8_t* message) {
    return (message[1] == 0U) ? 0U : 128U;
}

/**
 * Return segment number of EDOD for I2C-native-AUX transaction.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getEdidSegmentNumber(const uint8_t* message) {
    return message[0];
}

/**
 * Return address of DPCD register for AUX transaction.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint32_t getDpcdAddress(const uint8_t* message) {
    return getBe24(&message[2]);
}

/**
 * Return length of DPCD data which should be write/read via AUX transaction.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint16_t getDpcdDataLen(const uint8_t* message) {
    return getBe16(message);
}

/**
 * Return HPD events to set.
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getEvents(const uint8_t* message) {
    return message[0];
}

/**
 * Return lane number for Link Training
 * @param[in] message, pointer to message via mailbox
 */
static inline uint8_t getLaneCount(const uint8_t* message) {
    return message[0];
}

/**
 * Return delay for step of Link Training
 * @param[in] message, pointer to message via mailbox
 */
static inline uint16_t getTrainingStepDelay(const uint8_t* message) {
    return getBe16(&message[1]);
}

/**
 * Verify I2C-over-AUX request parameters
 * Request legend:
 * |           header [0-3]         |                  message [4-...]         |
 * |  req[0]  |   req[1] | req[2-3] | req[4-5] |  req[6] | req[7] | req[8-...] |
 * |  opCode  | moduleId |  dataLen |   size   | address |  mot   |     data   |
 */
static bool is_i2c_aux_request_valid(uint16_t length, const uint8_t* message, bool isWrite)
{
    bool valid = true;
    uint16_t dataLength = getI2cDataLen(message);

    if (isWrite) {
        /* For writing: minimum 4 bytes (request header) + data length */
        valid = (length >= ((uint16_t)DP_TX_I2C_REQ_MSG_MIN_SIZE + dataLength));
    } else {
        /* For reading: minimum 4 bytes (request header) */
        valid = (length >= (uint16_t)DP_TX_I2C_REQ_MSG_MIN_SIZE);
    }

    /* 9 bytes are required for mailbox response in worst case */
    if ((dataLength + (uint16_t)DP_TX_HEADER_SIZE + (uint16_t)DP_TX_DPCD_MSG_MIN_SIZE) > (uint16_t)MAIL_BOX_MAX_SIZE) {
        valid = false;
    }

    /* I2C address over range */
    if (getI2cAddress(message) > (uint8_t)DP_TX_I2C_ADDRESS_RANGE) {
        valid = false;
    }

    /* Only boolean (0U or 1U) MoT value allowed */
    if (getI2cMot(message) > 1U) {
        valid = false;
    }

    return valid;
}

/** Check if is there any message to read and update type of bus if is
 *  @return 'true' if is any message to read or 'false' if not
 */
static bool isWaitingMessage(void)
{
    bool isWaitingMsg;

    if(MB_isWaitingModuleMessage(MB_TYPE_REGULAR, MB_MODULE_ID_DP)) {
        isWaitingMsg = true;
        dpTxMailHandlerData.messageBus = MB_TYPE_REGULAR;
    } else if(MB_isWaitingModuleMessage(MB_TYPE_SECURE, MB_MODULE_ID_DP)) {
        isWaitingMsg = true;
        dpTxMailHandlerData.messageBus = MB_TYPE_SECURE;
    } else {
        isWaitingMsg = false;
    }

    return isWaitingMsg;
}

/****************************************************************
 * Functions used as callback to requests transferred via mailbox.
 * This functions are called after finish of transaction.
 ****************************************************************
 */

/**
 * Callback for DPTX_READ_DPCD request
 * @param[in] reply, pointer to request data struct
 */
static void readDpcdCb(const DpTxRequestData_t* reply)
{
    /* Set transaction status */
    dpTxMailHandlerData.responseLength = reply->bytes_reply + (uint32_t)DP_TX_DPCD_MSG_MIN_SIZE;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_DPCD_READ_RESP;
    dpTxMailHandlerData.latestAuxError = reply->command & (uint8_t)DP_AUX_REPLY_MASK;

    /* Get data from reply */
    dpTxMailHandlerData.buffer[0] = GetByte1(reply->bytes_reply);
    dpTxMailHandlerData.buffer[1] = GetByte0(reply->bytes_reply);
    dpTxMailHandlerData.buffer[2] = GetByte2(reply->address);
    dpTxMailHandlerData.buffer[3] = GetByte1(reply->address);
    dpTxMailHandlerData.buffer[4] = GetByte0(reply->address);

    /* Save handler of next state */
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Callback for DPTX_WRITE_DPCD request
 * @param[in] reply, pointer to request data struct
 */
static void writeDpcdCb(const DpTxRequestData_t* reply)
{

    dpTxMailHandlerData.latestAuxError = reply->command & (uint8_t)DP_AUX_REPLY_MASK;

    if (dpTxMailHandlerData.wait_time == 0U) {
        /* Regular DPCD write: send response. */
        dpTxMailHandlerData.responseLength = (uint32_t)DP_TX_DPCD_MSG_MIN_SIZE;
        dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_DPCD_WRITE_RESP;

        /* Get data from reply */
        dpTxMailHandlerData.buffer[0] = GetByte1(reply->bytes_reply);
        dpTxMailHandlerData.buffer[1] = GetByte0(reply->bytes_reply);
        dpTxMailHandlerData.buffer[2] = GetByte2(reply->address);
        dpTxMailHandlerData.buffer[3] = GetByte1(reply->address);
        dpTxMailHandlerData.buffer[4] = GetByte0(reply->address);

        dpTxMailHandlerData.stateCb = sendMessageHandler;
    } else {
        /* DPCD write related to Link Training. Do not send response. */
        dpTxMailHandlerData.stateCb = linkTrainingWaitHandler;
    }
}

/**
 * Callback for DPTX_I2C_READ request
 * @param[in] reply, pointer to request data struct
 */
static void i2cReadCb(const DpTxRequestData_t* reply)
{
    /* Get transaction status */
    dpTxMailHandlerData.latestAuxError = reply->command & (uint8_t)DP_REPLY_MASK;
    dpTxMailHandlerData.latestI2cError = (reply->command >> (uint8_t)DP_REPLY_I2C_OFFSET) & (uint8_t)DP_REPLY_MASK;
    dpTxMailHandlerData.responseLength = reply->bytes_reply + (uint32_t)DP_TX_I2C_RESP_MSG_MIN_SIZE;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_I2C_READ_RESP;

    /* Get data from reply */
    dpTxMailHandlerData.buffer[0] = GetByte1(reply->bytes_reply);
    dpTxMailHandlerData.buffer[1] = GetByte0(reply->bytes_reply);
    dpTxMailHandlerData.buffer[2] = (uint8_t)reply->address;

    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Callback for DPTX_I2C_WRITE request
 * @param[in] reply, pointer to request data struct
 */
static void i2cWriteCb(const DpTxRequestData_t* reply)
{
    /* Get transaction status */
    dpTxMailHandlerData.latestAuxError = reply->command & (uint8_t)DP_REPLY_MASK;
    dpTxMailHandlerData.latestI2cError = (reply->command >> DP_REPLY_I2C_OFFSET) & (uint8_t)DP_REPLY_MASK ;
    dpTxMailHandlerData.responseLength = (uint32_t)DP_TX_I2C_RESP_MSG_MIN_SIZE;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_I2C_WRITE_RESP;

    /* Get data from reply */
    dpTxMailHandlerData.buffer[0] = GetByte1(reply->bytes_reply);
    dpTxMailHandlerData.buffer[1] = GetByte0(reply->bytes_reply);
    dpTxMailHandlerData.buffer[2] = (uint8_t)reply->address;

    /* Save handler of next state */
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Callback for DPTX_GET_EDID request
 * @param[in] reply, pointer to request data struct
 */
static void readEdidCb(const DpTxRequestData_t* reply)
{
    /* Get transaction status */
    dpTxMailHandlerData.responseLength = reply->bytes_reply + 2U;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_EDID_RESP;

    /* Cannot be greater than 128B */
    dpTxMailHandlerData.buffer[0] = (uint8_t)reply->bytes_reply;

    dpTxMailHandlerData.buffer[1] = dpTxMailHandlerData.segmentNumber; //TODO set block number
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/* parasoft-begin-suppress MISRA2012-RULE-2_7-4, "Parameter 'reply' unused in function, DRV-4576" */

/**
 * Callback for DPTX_SET_POWER_MNG request
 * @param[in] reply, pointer to request data struct
 */
static void powerManageCb(const DpTxRequestData_t* reply)
{
    dpTxMailHandlerData.stateCb = idleHandler;
}

/**
 * Auxiliary callback for DPTX_GET_EDID request
 * (segment number is ok)
 * @param[in] reply, pointer to request data struct
 */
static void writeEdidOffsetCb(const DpTxRequestData_t* reply)
{
    DpTxRequestData_t* request = &dpTxMailHandlerData.request;

    /* Offset has been written, read 128 bytes EDID
     * and finish transaction. */
    request->address = (uint32_t)EDID_SLAVE_ADDRESS;
    request->command = (uint8_t)DP_REQUEST_TYPE_I2C
                     | (uint8_t)DP_REQUEST_READ;
    request->length = (uint8_t)EDID_LENGTH;
    request->endTransaction = true;
    request->buffer = &dpTxMailHandlerData.buffer[2];

    dpTxMailHandlerData.callback = readEdidCb;
    dpTxMailHandlerData.stateCb = rxProcessingHandler;
}

/**
 * Auxiliary callback for DPTX_GET_EDID request
 * (segment error occured)
 * @param[in] reply, pointer to request data struct
 */
static void writeEdidSegmentErrorCb(const DpTxRequestData_t* reply)
{
    /* Prepare response */
    dpTxMailHandlerData.responseLength = (uint32_t)DP_TX_EDID_RESP_MSG_MIN_SIZE;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_EDID_RESP;
    dpTxMailHandlerData.buffer[0] = 0U;
    dpTxMailHandlerData.buffer[1] = dpTxMailHandlerData.segmentNumber;
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Auxiliary function used to generate request for writing
 * EDID offset
 */
static void writeEdidOffset(void)
{
	DpTxRequestData_t* request = &dpTxMailHandlerData.request;

	/* Prepare request data */
	request->address = (uint32_t)EDID_SLAVE_ADDRESS;
	request->command = (uint8_t)DP_REQUEST_TYPE_I2C
	                                    | (uint8_t)DP_REQUEST_WRITE;
	request->length = 1U;
	request->endTransaction = false;
	request->buffer = &dpTxMailHandlerData.buffer[1];
	request->buffer[0] = dpTxMailHandlerData.edidOffset;

	/* Set function to call after finish */
	dpTxMailHandlerData.callback = writeEdidOffsetCb;

	/* Save handler of next state */
	dpTxMailHandlerData.stateCb = rxProcessingHandler;
}

/**
 * Callback for DPTX_GET_EDID request
 * @param[in] reply, pointer to request data struct
 */
static void writeEdidSegmentCb(const DpTxRequestData_t* reply)
{
    if ((reply->command != (uint8_t)DP_REPLY_ACK) && (dpTxMailHandlerData.segmentNumber != 0U)) {
        /* Reply was not I2C_ACK|AUX_ACK (i.e. I2C NACK) for non-zero E-EDID segment.
         * Send address-only end-of-transaction request. */
        dpTxMailHandlerData.request.address = (uint32_t)EDID_SEGMENT_SLAVE_ADDRESS;
        dpTxMailHandlerData.request.command = (uint8_t)DP_REQUEST_TYPE_I2C
                                            | (uint8_t)DP_REQUEST_WRITE;
        dpTxMailHandlerData.request.length = 0U;
        dpTxMailHandlerData.request.endTransaction = true;

        dpTxMailHandlerData.callback = writeEdidSegmentErrorCb;

        /* Save handler of next state */
        dpTxMailHandlerData.stateCb = rxProcessingHandler;

    } else {
        /* Write EDID offset */
        writeEdidOffset();
    }
}
/* parasoft-end-suppress MISRA2012-RULE-2_7-4 */

/********************************************************************************
 * Pack of functions used to generate transaction data due to received message Id
 * Functions are called, when message was received.
 ********************************************************************************
 */

/**
 * Auxiliary function used to generate DPCD response if error occured
 * @param[in] isRegularBus, if used bus is APB
 * @param[in] opCode, code of operation
 * @param[in] address, address of DPCD register
 */
static void setInvalidDpcdTransactionResp(bool isRegularBus, DpTxResponse_t opCode, uint32_t address)
{
    uint8_t* buffer = dpTxMailHandlerData.buffer;

    dpTxMailHandlerData.responseLength = DP_TX_DPCD_MSG_MIN_SIZE;
    dpTxMailHandlerData.responseOpcode = (uint8_t)opCode;
    /* If SAPB is used - inform host about it */
    dpTxMailHandlerData.latestAuxError = isRegularBus ?
            (uint8_t)DP_REPLY_ACK : (uint8_t)DP_AUX_REPLY_BUS_ERROR;

    buffer[0] = 0U;
    buffer[1] = 0U;
    buffer[2] = GetByte2(address);
    buffer[3] = GetByte1(address);
    buffer[4] = GetByte0(address);

    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Auxiliary function used to DPCD read request
 * @param[in] address, DPCD address
 * @param[in] length, number of data to transmit
 */
static void setReadDpcdRequest(uint32_t address, uint16_t length)
{
    DpTxRequestData_t* request = &dpTxMailHandlerData.request;

    /* Create request */
    request->address = address;
    request->command = (uint8_t)DP_REQUEST_TYPE_AUX | (uint8_t)DP_REQUEST_READ;
    request->length = length;
    request->buffer = &dpTxMailHandlerData.buffer[DP_TX_DPCD_MSG_MIN_SIZE];

    dpTxMailHandlerData.callback = readDpcdCb;

    /* Save handler of next state */
    dpTxMailHandlerData.stateCb = rxProcessingHandler;

}

/**
 * Auxiliary function used to generate DPCD write request
 * @param[in] address, DPCD address
 * @param[in] length, number of data to transmit
 * @param[in] buffer, pointer to buffer with data
 */
static void setDpcdWriteRequest(uint32_t address, uint16_t length, const uint8_t* buffer)
{
    uint8_t index;
    DpTxRequestData_t* request = &dpTxMailHandlerData.request;

    /* Create request */
    request->address = address;
    request->length = length;
    request->command = (uint8_t)DP_REQUEST_TYPE_AUX
                     | (uint8_t)DP_REQUEST_WRITE;
    request->buffer = dpTxMailHandlerData.buffer;

    if (request->length > 0U) {
        /* Put data into buffer */
        for (index = 0U; index < request->length; index++) {
            request->buffer[index] = buffer[index];
        }

        dpTxMailHandlerData.callback = writeDpcdCb;

        /* Save handler of next state */
        dpTxMailHandlerData.stateCb = rxProcessingHandler;
    }
}

/**
 * Auxiliary function used to generate I2C-over-AUX request
 * @param[in] opCode, read or write
 * @param[in] length, number of bytes to transmit
 * @param[in] message, pointer to message received via mailbox
 */
static void setI2cRequest(DpTxMailRequest_t opCode, uint16_t length, const uint8_t* message)
{
    bool isWrite = (opCode == DPTX_I2C_WRITE);
    DpTxRequestData_t* request;
    uint8_t index;

     /* Verify request parameters. */
     if (!is_i2c_aux_request_valid(length, message, isWrite)) {

         /* Return error message */
         dpTxMailHandlerData.responseLength = (uint32_t)DP_TX_I2C_RESP_MSG_MIN_SIZE;
         dpTxMailHandlerData.buffer[0] = 0U;
         dpTxMailHandlerData.buffer[1] = 0U;
         dpTxMailHandlerData.buffer[2] = 0U;
         dpTxMailHandlerData.latestI2cError = 0U;
         dpTxMailHandlerData.latestAuxError = 0U;
         dpTxMailHandlerData.responseOpcode = (uint8_t)opCode;
         dpTxMailHandlerData.stateCb = sendMessageHandler;

     } else {

         request = &dpTxMailHandlerData.request;

         /* Calculate address and length of data for I2C-native-AUX transaction */
         request->address = getI2cAddress(message);
         request->length = getI2cDataLen(message);

         if (isWrite) {
             request->command = (uint8_t)DP_REQUEST_TYPE_I2C
                                | (uint8_t)DP_REQUEST_WRITE;
             request->buffer = &dpTxMailHandlerData.buffer[DP_TX_I2C_REQ_MSG_MIN_SIZE];
             dpTxMailHandlerData.callback = i2cWriteCb;

             /* Put data into buffer */
             for (index = 0U; index < dpTxMailHandlerData.request.length; index++) {
                  request->buffer[index] = message[DP_TX_I2C_REQ_MSG_MIN_SIZE + index];
             }

         } else {
             request->command = (uint8_t)DP_REQUEST_TYPE_I2C | (uint8_t)DP_REQUEST_READ;
             request->buffer = &dpTxMailHandlerData.buffer[3];
             dpTxMailHandlerData.callback = i2cReadCb;
         }

         dpTxMailHandlerData.request.endTransaction = (getI2cMot(message) == 0U);

         /* Save handler of next state */
         dpTxMailHandlerData.stateCb = rxProcessingHandler;
     }
}

/**
 * Handler for DPTX_SET_POWER_MNG request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void setPowerMngHandler(const MailboxData_t* mailboxData)
{
    DpTxRequestData_t* request = &dpTxMailHandlerData.request;

    /* Create request */
    request->address = (uint32_t)DPCD_POWER_CONTROL_ADDR;
    request->command = (uint8_t)DP_REQUEST_TYPE_AUX | (uint8_t)DP_REQUEST_WRITE;

    request->length = 1U;
    request->buffer = &dpTxMailHandlerData.buffer[0];
    request->buffer[0] = mailboxData->message[0];

    dpTxMailHandlerData.callback = powerManageCb;

    /* Save handler of next state */
    dpTxMailHandlerData.stateCb = rxProcessingHandler;
}

/**
 * Handler for DPTX_GET_EDID request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void getEdidHandler(const MailboxData_t* mailboxData)
{
    DpTxRequestData_t* request;

    /* Set EDID parameters */
    dpTxMailHandlerData.edidOffset = getEdidOffset(mailboxData->message);
    dpTxMailHandlerData.segmentNumber = getEdidSegmentNumber(mailboxData->message);

    if (dpTxMailHandlerData.segmentNumber != 0U) {

    	request = &dpTxMailHandlerData.request;

    	/* Set request data */
        request->address = (uint32_t)EDID_SEGMENT_SLAVE_ADDRESS;
        request->command = (uint8_t)DP_REQUEST_TYPE_I2C
                         | (uint8_t)DP_REQUEST_WRITE;
        request->length = 1U;
        request->endTransaction = false;

		/* Put segment number into request buffer */
		request->buffer = &dpTxMailHandlerData.buffer[0];
		request->buffer[0] = dpTxMailHandlerData.segmentNumber;

		dpTxMailHandlerData.callback = writeEdidSegmentCb;
		dpTxMailHandlerData.stateCb = rxProcessingHandler;
    } else {
    	/* Write EDID offset */
    	writeEdidOffset();
    }
}

/**
 * Handler for DPTX_READ_DPCD request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void readDpcdHandler(const MailboxData_t* mailboxData)
{
    uint16_t dpcdLen = getDpcdDataLen(mailboxData->message);
    uint32_t address = getDpcdAddress(mailboxData->message);
    bool isRegularBus = (dpTxMailHandlerData.messageBus == MB_TYPE_REGULAR);

    if ((isRegularBus) && (dpcdLen > 0U)) {
        setReadDpcdRequest(address, dpcdLen);
    } else {
        setInvalidDpcdTransactionResp(isRegularBus, DPTX_DPCD_READ_RESP, address);
    }
}

/**
 * Handler for DPTX_WRITE_DPCD request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void writeDpcdHandler(const MailboxData_t* mailboxData)
{
    bool isRegularBus = (dpTxMailHandlerData.messageBus == MB_TYPE_REGULAR);
    dpTxMailHandlerData.request.length = 0U;
    uint32_t address = getDpcdAddress(mailboxData->message);

    if (isRegularBus) {
        setDpcdWriteRequest(address,
                            getDpcdDataLen(mailboxData->message),
                            &(mailboxData->message[DP_TX_DPCD_MSG_MIN_SIZE]));
    }

    /* Cannot send message on requested bus */
    if ((!isRegularBus) || (dpTxMailHandlerData.request.length == 0U)){
        setInvalidDpcdTransactionResp(isRegularBus, DPTX_DPCD_WRITE_RESP, address);
    }
}

/**
 * Handler for DPTX_ENABLE_EVENT request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void enableEventHandler(const MailboxData_t* mailboxData)
{
    dpTxMailHandlerData.enabledEvFlags = getEvents(mailboxData->message);
}

/** Handler for DPTX_WRITE_REGISTER request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
 static void writeRegisterHandler(const MailboxData_t* mailboxData)
 {
    uint16_t address = getBe16(mailboxData->message);
    uint32_t* reg = uintToPointer((uint32_t)address);

    if (is_mb_access_permitted(reg, (dpTxMailHandlerData.messageBus != MB_TYPE_REGULAR))) {
        *reg = getBe32(&mailboxData->message[2]);
    }
 }

 /** Handler for DPTX_WRITE_FIELD request
  * @param[in] mailboxData, pointer to data received via mailbox
  */
 static void writeFieldHandler(const MailboxData_t* mailboxData)
 {
    uint8_t* message = mailboxData->message;
    uint16_t address = getBe16(message);
    uint32_t* reg = uintToPointer((uint32_t)address);
    uint32_t mask;

    bool access_permitted = is_mb_access_permitted(reg, (dpTxMailHandlerData.messageBus != MB_TYPE_REGULAR));

    if (access_permitted) {
        mask = safe_shift32(LEFT, 0xFFFFFFFFU, 32U - message[3]);
        // 2nd operation on mask - shift right
        mask = safe_shift32(RIGHT, mask, 32U - message[2] - message[3]);

        *reg &= ~mask;
        *reg |= getBe32(&message[4]) & mask;
    }
 }

/* parasoft-begin-suppress MISRA2012-RULE-2_7-4, "Parameter 'mailboxData unused in function, DRV-4576" */

/**
 * Handler for DPTX_READ_EVENT request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void readEventHandler(const MailboxData_t* mailboxData)
{
    uint8_t* eventDetails = &dpTxMailHandlerData.eventDetails;

    dpTxMailHandlerData.buffer[0] = *eventDetails;

    /* Clear events except HPD current value */
    *eventDetails &= (uint8_t)DP_TX_EVENT_CODE_HPD_STATE_HIGH;

    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_READ_EVENT_RESP;
    dpTxMailHandlerData.responseLength = 1U;
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Handler for DPTX_GET_LAST_STATUS request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void getLastAuxStatusHandler(const MailboxData_t* mailboxData)
{
    dpTxMailHandlerData.buffer[0] = dpTxMailHandlerData.latestAuxError;
    dpTxMailHandlerData.responseLength = 1U;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_GET_LAST_AUX_STATUS;
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Handler for DPTX_HPD_STATE request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void hpdStateHandler(const MailboxData_t* mailboxData)
{
    extern uint8_t g_hpd_state;
    dpTxMailHandlerData.buffer[0] = g_hpd_state & (uint8_t)DP_TX_EVENT_CODE_HPD_HIGH;
    dpTxMailHandlerData.responseLength = 1U;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_HPD_STATE;
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}

/**
 * Handler for DPTX_GET_LAST_I2C_STATUS request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void getLastI2cStatusHandler(const MailboxData_t* mailboxData)
{
    dpTxMailHandlerData.buffer[0] = dpTxMailHandlerData.latestI2cError;
    dpTxMailHandlerData.responseLength = 1U;
    dpTxMailHandlerData.responseOpcode = (uint8_t)DPTX_GET_LAST_I2C_STATUS;
    dpTxMailHandlerData.stateCb = sendMessageHandler;
}
/* parasoft-end-suppress MISRA2012-RULE-2_7-4*/

/**
 * Handler for DPTX_LT_ADJUST request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void ltAdjustHandler(const MailboxData_t* mailboxData)
{
    dpTxMailHandlerData.wait_time = getTrainingStepDelay(mailboxData->message);

    bool isRegularBus = (dpTxMailHandlerData.messageBus == MB_TYPE_REGULAR);

    uint8_t length = getLaneCount(mailboxData->message);

    if ((isRegularBus) && (length > 0U)) {
        setDpcdWriteRequest(DPCD_TRAINING_LANE0_SET_ADDR,
                            length,
                            &(mailboxData->message[DP_TX_ADJUST_MSG_MIN_SIZE]));
    } else {
        if (dpTxMailHandlerData.wait_time == 0U) {
            setInvalidDpcdTransactionResp(isRegularBus, DPTX_DPCD_WRITE_RESP, DPCD_TRAINING_LANE0_SET_ADDR);
        } else {
            /* Message and DPCD bus type mismatch during Link Training -
               skip to read state, which will detect mismatch as well and return "empty" response. */
            dpTxMailHandlerData.stateCb = readLinkTrainingResultHandler;
        }
    }
}

/**
 * Handler for DPTX_I2C_READ request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void i2cReadHandler(const MailboxData_t* mailboxData)
{
    setI2cRequest(DPTX_I2C_READ, mailboxData-> length, mailboxData->message);
}

/**
 * Handler for DPTX_I2C_WRITE request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void i2cWriteHandler(const MailboxData_t* mailboxData)
{
    setI2cRequest(DPTX_I2C_WRITE, mailboxData->length, mailboxData->message);
}

/**
 * Auxiliary function used to execute correct function mailbox request
 * @param[in] mailboxData, pointer to data received via mailbox
 */
static void mailboxExecutor(const MailboxData_t* mailboxData)
{
    uint8_t i;

    /* Array with pointers to functions and opCodes*/
    static const MsgAction_t msgAction[] = {
            {setPowerMngHandler, DPTX_SET_POWER_MNG},
            {getEdidHandler, DPTX_GET_EDID},
            {readDpcdHandler, DPTX_READ_DPCD},
            {writeDpcdHandler, DPTX_WRITE_DPCD},
            {writeRegisterHandler, DPTX_WRITE_REGISTER},
            {writeFieldHandler, DPTX_WRITE_FIELD},
            {enableEventHandler, DPTX_ENABLE_EVENT},
            {readEventHandler, DPTX_READ_EVENT},
            {getLastAuxStatusHandler, DPTX_GET_LAST_AUX_STATUS},
            {hpdStateHandler, DPTX_HPD_STATE},
            {ltAdjustHandler, DPTX_LT_ADJUST},
            {i2cReadHandler, DPTX_I2C_READ},
            {i2cWriteHandler, DPTX_I2C_WRITE},
            {getLastI2cStatusHandler, DPTX_GET_LAST_I2C_STATUS}
    };

    /* If invalid opCode was received, any action will be done */
    for (i = 0U; i < (uint8_t)NUMBER_OF_REQ_OPCODES; i++) {
        if (mailboxData->opCode == (uint8_t)msgAction[i].opCode) {
            (msgAction[i].msgHandler)(mailboxData);
            break;
        }
    }
}

/**********************************************************
 * Handlers to DP TX mailbox handler module states
 **********************************************************
 */

static void idleHandler(void)
{
    MailboxData_t mailboxData;

    /* Check if message was received */
    if (isWaitingMessage()) {

        /* Get current message */
        MB_getCurMessage(dpTxMailHandlerData.messageBus, &(mailboxData.message), &(mailboxData.opCode), &(mailboxData.length));

        /* Do action for message */
        mailboxExecutor(&mailboxData);

        /* Set message as read to free buffer */
        MB_FinishReadMsg(dpTxMailHandlerData.messageBus);
    }
}

static void rxProcessingHandler(void)
{
    /* Check if DP_TX module is not busy */
    if (DP_TX_isAvailable()) {
        /* Add request to DP_TX module */
        DP_TX_addRequest(&dpTxMailHandlerData.request, dpTxMailHandlerData.callback);
        /* Start timer to catch timeout if will appear */
        startTimer(MAILBOX_LINK_LATENCY_TIMER);
        /* Save handler of next state */
        dpTxMailHandlerData.stateCb = timeoutHandler;
    } else {
        /* Can't handle to command right now, return to callback with empty data */
        DP_TX_removeRequest(&dpTxMailHandlerData.request, dpTxMailHandlerData.callback);
    }
}

static void sendMessageHandler(void)
{
    uint8_t* responseBuff;
    uint8_t i;

    MB_TYPE busType = dpTxMailHandlerData.messageBus;

    /* Check if mailbox TX is ready */
    if (MB_IsTxReady(busType)) {
        responseBuff = MB_GetTxBuff(busType);
        /* Copy data into buffer */
        for (i = 0U; i < dpTxMailHandlerData.responseLength; i++) {
            responseBuff[i] = dpTxMailHandlerData.buffer[i];
        }

        /* Send message and go to next state */
        MB_SendMsg(busType, dpTxMailHandlerData.responseLength, dpTxMailHandlerData.responseOpcode, MB_MODULE_ID_DP);
        dpTxMailHandlerData.stateCb = idleHandler;
    }
}

static void timeoutHandler(void)
{
    /* Remove request if timeout was reached, no update timer status */
    if (getTimerMsWithoutUpdate(MAILBOX_LINK_LATENCY_TIMER) > (uint32_t)DP_TX_MAIL_HANDLER_TIMEOUT_MS) {
        DP_TX_removeRequest(&dpTxMailHandlerData.request, dpTxMailHandlerData.callback);
    }
}

static void linkTrainingWaitHandler(void)
{
    /* Put module to sleep */
    modRunnerSleep(dpTxMailHandlerData.wait_time);
    /* Save handler of next state */
    dpTxMailHandlerData.stateCb = readLinkTrainingResultHandler;
}

static void readLinkTrainingResultHandler(void)
{
    bool isRegularBus = (dpTxMailHandlerData.messageBus == MB_TYPE_REGULAR);

    dpTxMailHandlerData.wait_time = 0U;

    if (isRegularBus) {
        /* Read DPCD registers containing result of link training phase
         * and requested adjustments */
        setReadDpcdRequest(DPCD_TRAINING_LANE0_STATUS_ADDR, 6U);
    } else {
        setInvalidDpcdTransactionResp(isRegularBus,
                                      DPTX_DPCD_READ_RESP,
                                      DPCD_TRAINING_LANE0_STATUS_ADDR);
    }
}

/**
 * Main thread of DP_TX_MAIL_HANDLER module
 */
static void DP_TX_MAIL_HANDLER_thread(void)
{
	if (dpTxMailHandlerData.stateCb != NULL) {
		(dpTxMailHandlerData.stateCb)();
	}
}

/**
 * Initialization function of DP_TX_MAIL_HANDLER module
 */
static void DP_TX_MAIL_HANDLER_init(void)
{
    dpTxMailHandlerData.stateCb = idleHandler;
    dpTxMailHandlerData.wait_time = 0U;
    dpTxMailHandlerData.latestAuxError = 0U;
    dpTxMailHandlerData.latestI2cError = 0U;
}

/**
 * Starter of DP_TX_MAIL_HANDLER module
 */
static void DP_TX_MAIL_HANDLER_start(void)
{
    modRunnerWakeMe();
}

void DP_TX_MAIL_HANDLER_notifyHpdEv(uint8_t eventCode)
{
    uint32_t regVal;

    /* Update global register */
    hpdState |= eventCode;

    if ((dpTxMailHandlerData.enabledEvFlags & (uint8_t)DP_TX_EVENT_CODE_HPD_HIGH) != 0U) {
        /* Update host events */
        dpTxMailHandlerData.eventDetails = eventCode;
        RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_DPTX_HPD);
    }
}

void DP_TX_MAIL_HANDLER_initOnReset(void)
{
    /* Set up all events */
    dpTxMailHandlerData.enabledEvFlags = 0xFF;
}

void DP_TX_MAIL_HANDLER_InsertModule(void)
{
    static Module_t dpTxMailHandlerModule;

    /* Set module functions */
    dpTxMailHandlerModule.initTask = &DP_TX_MAIL_HANDLER_init;
    dpTxMailHandlerModule.startTask = &DP_TX_MAIL_HANDLER_start;
    dpTxMailHandlerModule.thread = &DP_TX_MAIL_HANDLER_thread;

    dpTxMailHandlerModule.moduleId = MODRUNNER_MODULE_DP_AUX_TX_MAIL_HANDLER;
    dpTxMailHandlerModule.pPriority = 0U;

    /* Insert module into context */
    modRunnerInsertModule(&dpTxMailHandlerModule);
}

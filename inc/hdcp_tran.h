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
 * hdcp_tran.h
 *
 ******************************************************************************
 */

/* parasoft-begin-suppress METRICS-36 "Function is called from more than 5 functions, DRV-3823" */

#ifndef HDCP_TRAN_H
#define HDCP_TRAN_H

#include "cdn_stdtypes.h"
#include "utils.h"
#include "engine.h"
#include "modRunner.h"

/* Mask for valid receiver message */
#define HDCP_MSG_IS_REC_ID_VALID_MASK 0x01U
/* Offset of devCount field in ReceiverId List */
#define HDCP_RID_LIST_DEV_COUNT_OFFSET 0U
/* Size of HDCP Receiver ID in bytes*/
#define HDCP_REC_ID_SIZE 5U
/* Offset of receiver data (Binfo (v1.x) or RxInfo (v2.x) in ReceiverId list */
#define HDCP_RID_LIST_ID_OFFSET 2U
/* Size of local ReceiverId list. Structure of list:
 * byte[0] - number of devices (max 127)
 * byte[1] - is/not repeater (unused)
 * bytes[2...636] - receiver IDs (5 bytes for each device)
 * bytes[637-638] - Binfo (v1.x) or Bcaps (v2.x) of last receiver */
#define HDCP_RID_LIST_SIZE 639U
/* Size of buffer used in HDCP transactions. Size specified on biggest transaction in FW
 * (reading receiverId list for HDCP v1.x) */
#define HDCP_TRANSACTION_BUFFER_SIZE 635U
/* Address of DPCD_REV register */
#define DPCD_DCPD_REV_ADDRESS 0x00000U
/* Address if device interrupt vector */
#define DEVICE_SERVICE_IRQ_VECTOR  0x00201U
/* Mask of 'Content Protection' interrupt */
#define DEVICE_SERVICE_CP_IRQ_MASK 0x04U
/* Address of MSTM_CAP register */
#define MSTM_CAP_ADDRESS 0x00021U
/* Mask of MST support bit */
#define MSTM_CAP_MST_CAP_MASK 0x01U

typedef enum {
    DPCD_RX_REV_1p0 = 0x10U,
    DPCD_RX_REV_1p1 = 0x11U,
    DPCD_RX_REV_1p2 = 0x12U,
    DPCD_RX_REV_1p3 = 0x13U,
    DPCD_RX_REV_1p4 = 0x14U
} DpcdRxRev_t;

/* Status
 * | 1d1 |  err  | streamMng | rxType | rec/rep | auth |
 * | [9] | [8:5] |    [4]    |  [3:2] |   [1]   |  [0] |
 */
/* Offset of 'authenticated' in status */
#define HDCP_STATUS_IS_AUTH_OFFSET 0U
/* Mask for 'authenticated' in status */
#define HDCP_STATUS_IS_AUTH_MASK   0x0001U
/* Offset of 'device_type' (receiver, receiver/repeater) in status */
#define HDCP_STATUS_DEVICE_TYPE_OFFSET  1U
/* Mask for 'device_type' (receiver, receiver/repeater) in status */
#define HDCP_STATUS_DEVICE_TYPE_MASK 0x0002U
/* Offset of 'HDCP Rx Type' (v1.x or v2.x) in status */
#define HDCP_STATUS_HDCP_TYPE_OFFSET 2U
/* Offset of 'Stream Management'  in status */
#define HDCP_STATUS_STREAM_MG_OFFSET 4U
/* Mask for 'Stream Management'  in status */
#define HDCP_STATUS_STREAM_MG_MASK 0x0010U
/* Offset of 'error_code'  in status */
#define HDCP_STATUS_ERROR_TYPE_OFFSET 5U
/* Mask of 'error code' in status */
#define HDCP_STATUS_ERROR_TYPE_MASK 0x01E0U
/* Mask of status info part */
#define HDCP_STATUS_INFO_BITS_MASK 0x021FU

/* When message to HDCP module was send via APB, FW needs to response with message filled with zeros
 * to avoid blocking of driver. Size was presented in bytes */
/* Size of HDCP_TRAN_STATUS_CHANGE message response */
#define HDCP_STATUS_CHANGE_RESP_SIZE 5U
/* Size of HDCP2X_TX_STORE_KM message response */
#define HDCP2X_STORE_KM_RESP_SIZE 53U
/* Size of HDCP_TRAN_IS_REC_ID_VALID message response */
#define HDCP_IS_REC_ID_VALID_RESP_SIZE 4U
/* Size of HDCP2X_TX_IS_KM_STORED message response */
#define HDCP2X_IS_KM_STORED_RESP_SIZE 5U

typedef enum {
    /* No error occurred */
    HDCP_ERR_NO_ERROR = 0x00U,
    /* HPD is down */
    HDCP_ERR_HPD_DOWN = 0x01U,
    /* SRM failure */
    HDCP_ERR_SRM_FAIL = 0x02U,
    /* Signature verification error */
    HDCP_ERR_SIGN_ERROR = 0x03U,
    /* Hash H', computed by receiver, differs from H computed
     * by transmitter (for HDCP 2.x) */
    HDCP_ERR_H_HASH_MISMATCH = 0x04U,
    /* Hash V', computed by receiver, differs from V computed
     * by transmitter (for HDCP 1.x)  */
    HDCP_ERR_V_HASH_MISMATCH = 0x05U,
    /*Locality check failed and could not be retried anymore (for HDCP 2.x) */
    HDCP_ERR_LOCALITY_CHECK_FAIL = 0x06U,
    /* DDC (AUX channel) error */
    HDCP_ERR_DDC_ERROR = 0x07U,
    /* Re-authentication is required */
    HDCP_ERR_REAUTH_REQ = 0x08U,
    /* Topology error. Exceeded max number of devices (MAX_DEVS_EXCEEDED) or
     * max repeater cascade depth (MAX_CASCADE_EXCEEDED), or seq_num_V is invalid. */
    HDCP_ERR_TOPOLOGY_ERROR = 0x09U,
    /* Not all reserved (Rsvd) bytes in HDCP port were read as zero, or receiver
     * is not HDCP-capable. */
    HDCP_ERR_RSVD_NOT_ZERO = 0x0BU,
    /* Link synchronization verification values RI differ. */
    HDCP_ERR_RI_MISMATCH = 0x0DU,
    /* Module watchdog expired. Some data was not available in assumed time */
    HDCP_ERR_WATCHDOG_EXPIRED = 0x0EU,
} HdcpTransactionError_t;

typedef enum {
    /* Use HDCP v2.x protocol */
    HDCP_2_SUPPORT = 0U,
    /* Use HDCP v1.x protocol */
    HDCP_1_SUPPORT = 1U,
    /* Use HDCP 2.x protocol (if capable) or HDCP v1.x */
    HDCP_BOTH_SUPPORT = 2U,
    /* Unused value */
    HDCP_RESERVED = 3U
} HdcpVerSupport_t;

typedef enum {
    /* May be transmitted by The HDCP Repeater to all HDCP Devices. */
    HDCP_CONTENT_TYPE_0 = 0x00U,
    /* Must not be transmitted by the HDCP Repeater to:
     * - HDCP 1.x-compliant Devices,
     * - HDCP 2.0-compliant Devices,
     * - HDCP 2.1-compliant Devices*/
    HDCP_CONTENT_TYPE_1 = 0x01U
} HdcpContentStreamType_t;

/**
 *  \file
 *  \brief general HDCP2X transmitter function and data structures
 */

typedef void (*HdcpFunc_t)(void);

typedef struct {
    /* Pointer to HDCP initialization function (for specified HDCP version) */
    HdcpFunc_t initCb;
    /* Pointer to HDCP thread function (for specified HDCP version) */
    HdcpFunc_t threadCb;
} HdcpHandler_t;

/* Used in HDCP status word */
typedef enum {
    /* HDCP receiver supports 1.x */
    HDCP_RX_TYPE_1X = 0x01U,
    /* HDCP receiver supports 2.x */
    HDCP_RX_TYPE_2X = 0x02U
} HdcpRxType_t;

typedef struct {
    /* Extra byte to store specific message data (example: length or value of bit). If more than
     * one byte of message is needed, write function in engine{1T,2T}.c and use it */
    uint8_t result;
    /* 'true' if message was read, otherwise 'false' */
    bool isReady;
} MailboxHdcp_t;

typedef struct {
    /* List with receiver IDs */
    uint8_t command[HDCP_RID_LIST_SIZE];
    /* Current receiver ID list size */
    uint16_t size;
} ReceiverId_t;

typedef struct {
    /* Determines if fast delays are used */
    bool fastDelays;
    /* List with receiver IDs */
    ReceiverId_t rid;
    /* Status word of HDCP module(Same for HDCP v1.x and v2.x) */
    uint16_t status;
    /* Structure used to inform specified HDCP modules about mailbox message
     * Used for specific mailbox commands */
    MailboxHdcp_t mailboxHdcpMsg;
    /* HDCP transaction buffer (Rx/Tx) */
    uint8_t hdcpBuffer[HDCP_TRANSACTION_BUFFER_SIZE];
    /* Indicates which HDCP version is supported (v1.x, v2.x or both) */
    HdcpVerSupport_t  supportedMode;
    /* Value assigned to the Content Stream to be transmitted to the HDCP receiver */
    HdcpContentStreamType_t contentType;
    /* Indicates if status was updated in current iteration */
    bool statusUpdate;
    /* Indicates if error part of status was updated in current iteration */
    bool errorUpdate;
    /* TODO: what is it ? */
    bool customKmEnc;
    /* If HPD pulse event occurred */
    bool hpdPulseIrq;
    /* Pointer to current SM callback */
    StateCallback_t stateCb;
    /* Structure with pointers to functions of HDCP specified version */
    HdcpHandler_t hdcpHandler;
    /* Inicates which HDCP version is used */
    HdcpVer_t usedHdcpVer;
    /* XXX: mbeberok, workaround, will be removed */
    bool isMst;
} HdcpGenTransData_t;

typedef enum {
    /* Set HDCP transmitter type and wake it up (or stop) */
    HDCP_TRAN_CONFIGURATION = 0U,
    /* Set public key for the HDCP2X.x transmitter */
    HDCP2X_TX_SET_PUBLIC_KEY_PARAMS = 1U,
    /* Enforce the random parameters (for debug only!) instead of
     * the random data generated by the embedded RNG. */
    HDCP2X_TX_SET_DEBUG_RANDOM_NUMBERS = 2U,
    /* Set TX pairing data */
    HDCP2X_TX_RESPOND_KM = 3U,
    /* Received keys needed for HDCP 1.x */
    HDCP1_TX_SEND_KEYS = 4U,
    /* Set received AN, use it for debug purpose */
    HDCP1_TX_SEND_RANDOM_AN = 5U,
    /* Will be called when status-change event will be occurred by cadence HDCP IP */
    HDCP_TRAN_STATUS_CHANGE = 6U,
    /* Controller check if KM is stored by host (HDCP2X.x)*/
    HDCP2X_TX_IS_KM_STORED = 7U,
    /* Controller response host with stored pairing data */
    HDCP2X_TX_STORE_KM = 8U,
    /* Controller response with receiver ID */
    HDCP_TRAN_IS_REC_ID_VALID = 9U,
    /* Received information from host about validation of receiver ID */
    HDCP_TRAN_RESPOND_RECEIVER_ID_VALID = 10U,
    /* Compare HDCP keys with facsimile key, not implemented in driver */
    HDCP_TRAN_TEST_KEYS = 11U,
    /* Used to load customer defined Key for Km-key encryption into the HDCP2X.x transmitter controller */
    HDCP2X_TX_SET_KM_KEY_PARAMS = 12U,
    /* Number of supported messages */
    HDCP_NUM_OF_SUPPORTED_MESSAGES
} HdcpMailboxMsgId_t;

typedef enum {
    HDCP_GENERAL_SET_LC_128 = 0,
    HDCP_SET_SEED,
} HdcpMailboxGeneralMsgId_t;

extern HdcpGenTransData_t hdcpGenData;

/**
 * Attach module to system
 */
void HDCP_TRAN_InsertModule(void);

/**
 * Update status of module (should be called by submodules)
 * Function don't change error part of status
 * @param[in] status, current status of module
 */
void HDCP_TRAN_setStatus(uint16_t status);

/**
 * Update 'error code' part of module status (should be called by submodules)
 * Function change only error part of status
 * @param[in] errorVal, value of 'errorCode' occurred in module
 */
void HDCP_TRAN_setError(HdcpTransactionError_t errorVal);

/**
 * Return pointer to HDCP Rx/Tx buffer
 */
uint8_t* HDCP_TRAN_getBuffer(void);

/**
 * Reset for specified HDCP data. Should be called only once, after start-up of FW
 */
void HDCP_TRAN_initOnReset(void);

/**
 * Enables or disables fast delays.
 * @param[in] enable, 'true' to set fast delays or 'false'
 */
void HDCP_TRAN_setFastDelays(bool enable);

/**
 * Puts HDCP module to sleep for us time
 * @param[in] us, time to sleep if fast delays are disabled
 * @param[in] us_fast, time to sleep if fast delays are enabled.
 */

void HDCP_TRAN_sleep(uint32_t us, uint32_t us_fast);

/**
 * Used to update receiver ID list
 * @param[in] list, pointer to received ID list
 * @param[in] devCount, number of devices in topology
 * @param[in] receiverInfo, RxInfo (v2.x) or Binfo (v1.x) register value
 * @param[in] hdcpVer, version of HDCP Rx (v2.x, v1.x)
 */
void HDCP_setReceiverIdList(uint8_t* list, uint8_t devCount, uint16_t receiverInfo, HdcpVer_t hdcpVer);

#endif /* HDCP_TRAN_H */

/* parasoft-end-suppress METRICS-36 */

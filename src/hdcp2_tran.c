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
 * hdcp2_tran.c
 *
 ******************************************************************************
 */

/* parasoft-begin-suppress METRICS-36 "Function is called from more than 5 functions, DRV-3823" */
#include "hdcp_tran.h"
#include "hdcp2_tran.h"
#include "engine2T.h"
#include "engine.h"
#include "libHandler.h"
#include "general_handler.h"
#include "cipher_handler.h"
#include "reg.h"
#include "hdcp2.h"
#include "utils.h"
#include "controlChannelM.h"
#include "cdn_stdtypes.h"
#include "cps_drv.h"
#include "cp_irq.h"
#include "cdn_errno.h"
#include "events.h"

/* Special structure used to workaround VIP bugs (invalid changes of SM when some
   transactions are done in one flow, ex. DRV-3292 */
typedef struct {
    /* Number of done transactions in current flow */
    uint8_t counter;
    /* Address offset of current flow */
    uint16_t offset;
} MsgHelper_t;

typedef struct {
    /* Data for RxStatus events (if needed) */
    StateCallback_t nextCb;
    /* Timeout for read message operation */
    uint16_t timeout;
    /* Message Id */
    Hdcp2MsgId_t msgId;
    /* Transaction helper */
    MsgHelper_t msgHelper;
} MessageData_t;

static MessageData_t msgData;

static Hdcp2TData_t hdcp2TData;

/* Clean HDCP v2.2 status and send "AKE_Init" message to receiver */
static void A1_exchangeKmCb(void);

/* Order "AKE_Send_Cert" message reading */
static void A1_SendCertCb(void);

/* Save "AKE_Send_Cert" message data */
static void A1_waitAkeSendCertCb(void);

/* Decide if stored or non-stored Km is used based on HDCP v2.2 mailbox message */
static void A1_waitPairingTestCb(void);

/* Check, if certification signature is valid for non-stored Km */
static void A1_noStoredCCSVCb(void);

/* Send "AKE_Stored_Km" message into receiver */
static void A1_sendAkeStoredKmCb(void);

/* Send "AKE_No_Stored_Km" message into receiver */
static void A1_sendAkeNoStoredKmCb(void);

/* Order "AKE_H_Prime" message reading */
static void A1_readHPrimeCb(void);

/* Check if H equals H' */
static void A1_waitAkeSendHPrimeCb(void);

/* Order "AKE_Send_Pairing_Info" message reading */
static void A1_readPairingInfoCb(void);

/* Save "AKE_Send_Pairing_Info" message data */
static void A1_waitPairingWaitingAuxCb(void);

/* Send "LC_Init" message to receiver */
static void A2_sendLcInitCb(void);

/* Order "LC_Send_L_prime" message reading */
static void A2_readLPrime(void);

/* Check if L is equal L' */
static void A2_checkReadCb(void);

/* Send "SKE_Send_Eks" message to receiver */
static void A3_exchangeKsCb(void);

/* Check if device is only receiver or receiver-repeater */
static void A4_testForRepeaterCb(void);

/* Send content type to receiver. */
static void A5_sendContentyTypeCb(void);

/* Function for A5_Authenticated state */
static void A5_authenticatedCb(void);

/* Extra state to force polling of authentication */
static void A5_checkRxStatusCb(void);

static void A6_WaitForReceiverIdListCb(void);

/* Order "RepeaterAuth_Send_ReceiverId_List" message reading */
static void A6_readReceiverIdList(void);

/* Verify received receiverIdList by checking topology and compare V and V' */
static void A7_verifyReceiverIdCb(void);

/* Wait for revocation list from HDCP v2.2 mailbox message */
static void A7_waitForRevocationListCb(void);

/* Write "RepeaterAuth_Stream_Manage" message to receiver and order "RepeaterAuth_Stream_Ready" message reading */
static void A9_contentStreamManagementCb(void);

/* Read "RepeaterAuth_Stream_Ready message */
static void A9_readAuthStreamReadyCb(void);

/* Verify received "RepeaterAuth_Stream_Ready" message */
static void A9_ackResponseCb(void);

/* Prepare receiverList for driver */
static void srmCheckCb(void);

/* Order "AKE_Send_H_prime" in case of HDCP v2.2 response (receiverId is valid) */
static void srmResultCb(void);

/* Create request for message reading */
static void readHdcpMsg(void);

/* Create request for message writing */
static void writeHdcpMsg(void);

/**
 * Auxiliary function used to set request for message reading
 * @param[in] msgId, identifier of message
 * @param[in] passCb, address of function called in case of reading success
 * @param[in] timeoutMs, timeout for reading message (in milliseconds)
 */
static void readHdcp22Message(Hdcp2MsgId_t msgId, StateCallback_t passCb, uint16_t timeoutMs)
{
    /* Save message specific information */
    msgData.msgId = msgId;
    msgData.nextCb = passCb;

    msgData.msgHelper.counter = 0U;
    msgData.msgHelper.offset = 0U;

    if (timeoutMs != HDCP2X_NO_TIMEOUT) {
        modRunnerSetTimeout(milliToMicro(timeoutMs));
    }

    hdcp2TData.cb = &readHdcpMsg;
}

/* Write HDCP 2.2 message */
static void writeHdcp22Message(Hdcp2MsgId_t msgId, StateCallback_t nextState)
{
    /* Set up global msgData structure */
    msgData.msgId = msgId;
    msgData.nextCb = nextState;

    msgData.msgHelper.counter = 0U;
    msgData.msgHelper.offset = 0U;

    /* Set callback to function respective to the action (write message) */
    hdcp2TData.cb = &writeHdcpMsg;
}

/**
 * Reset HDCP v2.2 state machine and inform driver about error
 * @param[in] errorCode, Id of occurred error
 */
static inline void resetSM(HdcpTransactionError_t errorCode)
{
    HDCP_TRAN_setError(errorCode);
    modRunnerTimeoutClear();
    hdcp2TData.cb = &A1_exchangeKmCb;
}

/**
 * Cleanup module data before authentication process
 */
static void cleanHdcpData(void)
{
    /* Cleanup sequential numbers */
    hdcp2TData.seqNum.M = 0U;
    hdcp2TData.seqNum.V = 0U;

    hdcp2TData.devType = DEV_HDCP_RECEIVER;

    hdcp2TData.receiverIdListVerif = true;
}

static void A1_exchangeKmCb(void)
{
    CIPHER_ClearAuthenticated();

    if (CHANNEL_MASTER_isFree()) {

        cleanHdcpData();

        /* Update status of HDCP */
        hdcp2TData.status = (uint16_t)safe_shift32(true, (uint32_t)HDCP_RX_TYPE_2X, HDCP_STATUS_HDCP_TYPE_OFFSET);
        HDCP_TRAN_setError(HDCP_ERR_NO_ERROR);

        /* AKE_Init size - 11B */
        ENG2T_set_AKE_Init(hdcp2TData.buffer);

        /* AKE_Init transaction */
        writeHdcp22Message(HDCP2X_CMD_AKE_INIT, &A1_SendCertCb);
    }
}

/* Order "AKE_Send_Cert" message reading */
static void A1_SendCertCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        readHdcp22Message(HDCP2X_CMD_AKE_SEND_CERT, &A1_waitAkeSendCertCb, HDCP2X_AKE_SEND_READ_TIMEOUT_MS);
    }
}

static void A1_waitAkeSendCertCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        modRunnerTimeoutClear();

        /* Save AKE_Send_Cert read data in HDCP engine*/
        ENG2T_get_AKE_Send_Cert(hdcp2TData.buffer);

        hdcp2TData.devType = ENG2T_getDeviceTypeFromRxCaps();

        /* Set 'isRepeater' bit in status */
        if (hdcp2TData.devType == DEV_HDCP_REPEATER) {
            hdcp2TData.status |= (uint16_t)HDCP_STATUS_DEVICE_TYPE_MASK;
        }

        RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_IS_KM_STORED);

        hdcp2TData.cb = &A1_waitPairingTestCb;
    }
}

static void A1_waitPairingTestCb(void)
{
    MailboxHdcp_t* mailboxMsg = &(hdcpGenData.mailboxHdcpMsg);

    if (mailboxMsg->isReady) {

        /* Check length of message */
        if (mailboxMsg->result == 0U) {
            hdcp2TData.isPairingNeeded = true;
            hdcp2TData.cb = &A1_noStoredCCSVCb;
        } else {
            /* Key was stored immediately after receive message (look on: hdcp_tran.c) */
            hdcp2TData.isPairingNeeded = false;
            hdcp2TData.cb = &A1_sendAkeStoredKmCb;
        }

        mailboxMsg->isReady = false;
    }
}

static void A1_noStoredCCSVCb(void)
{
    uint32_t result = ENG2T_valid_cert_signature();

    /* For CDN_EINPROGRESS do nothing */
    if (result != CDN_EINPROGRESS) {
        if (result == CDN_EINVAL) {
            resetSM(HDCP_ERR_SIGN_ERROR);
        } else {
            hdcp2TData.cb = &A1_sendAkeNoStoredKmCb;
        }
    }
}

static void A1_sendAkeNoStoredKmCb(void)
{
    uint32_t result;

    if (CHANNEL_MASTER_isFree()) {

        result = ENG2T_set_AKE_No_Stored_km(hdcp2TData.buffer);

        /* If EkPubKm generation was finished, check if HPD were down. If not, check if generation was valid.
         * If "CDN_EOK" but HPD were not down) try again, same as if CDN_EINPROGRESS */
         if (result == CDN_EOK) {
             /* Write AKE_Send_No_Stored_km message */
             writeHdcp22Message(HDCP2X_CMD_AKE_NO_STORED_KM, &srmCheckCb);
        }
    }
}

static void A1_sendAkeStoredKmCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* No need to prepare data (was saved in buffer during "Pairing Test"), look at: hdcp_tran.c */
        writeHdcp22Message(HDCP2X_CMD_AKE_STORED_KM, &srmCheckCb);
    }
}

/* Prepare receiverList for driver */
static void srmCheckCb(void)
{
    /* Get receiver ID from "sCertRx" */
    ENG2T_GetReceiverId(&hdcpGenData.rid.command[2]);

    hdcpGenData.rid.command[0] = 1U;
    hdcpGenData.rid.command[1] = 0U;
    hdcpGenData.rid.size = (uint16_t)2U + (uint16_t)HDCP_REC_ID_SIZE;

    RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_IS_RECEIVER_ID_VALID);

    /* Next State: Order "AKE_Send_H_prime" */
    hdcp2TData.cb = &srmResultCb;
}

static void srmResultCb(void)
{
    MailboxHdcp_t* mailboxMsg = &(hdcpGenData.mailboxHdcpMsg);
    uint16_t timeout;

    if (mailboxMsg->isReady) {
        /* Check if receiverId is valid */
        if (mailboxMsg->result != 0U) {
            /* Valid receiver ID */
            timeout = (hdcp2TData.isPairingNeeded) ?
                    HDCP2X_H_PRIME_AVAILABLE_NO_STORED_TIMEOUT_MS : HDCP2X_H_PRIME_AVAILABLE_STORED_TIMEOUT_MS;

            /* XXX: mbeberok, CP_IRQ not work properly for MST */
            setCpIrqEvent(HDCP2X_RXSTATUS_HAVAILABLE_MASK, timeout, !hdcpGenData.isMst);

            hdcp2TData.cb = &A1_readHPrimeCb;

        } else {
            /* Non valid */
            resetSM(HDCP_ERR_SRM_FAIL);
        }

        mailboxMsg->isReady = false;
    }
}

/* Order "AKE_H_Prime" message reading */
static void A1_readHPrimeCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        readHdcp22Message(HDCP2X_CMD_AKE_SEND_H_PRIME, &A1_waitAkeSendHPrimeCb, HDCP2X_H_PRIME_READ_TIMEOUT_MS);
    }
}

static void A1_waitAkeSendHPrimeCb(void)
{
    /* Check if AKE_Send_H_prime was read */
    if (CHANNEL_MASTER_isFree()) {

        /* Verify H' */
        if (ENG2T_valid_H(hdcp2TData.buffer)) {

            /* Check if we need pairing or not */
            if (hdcp2TData.isPairingNeeded) {
                /* Km is not stored, send pairing info in 200 milliseconds */
                setCpIrqEvent(HDCP2X_RXSTATUS_PAIRING_AV_MASK, HDCP2X_PAIRING_AVAILABLE_TIMEOUT_MS, !hdcpGenData.isMst);
                hdcp2TData.cb = &A1_readPairingInfoCb;

            } else {
                /* Km is stored, not needed pairing info, go to locality check */
                modRunnerTimeoutClear();
                hdcp2TData.cb = &A2_sendLcInitCb;
            }
        } else {

            /* H' is not equal H */
            resetSM(HDCP_ERR_H_HASH_MISMATCH);
        }
    }
}

/* Order "AKE_Send_Pairing_Info" message reading */
static void A1_readPairingInfoCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        readHdcp22Message(HDCP2X_CMD_AKE_SEND_PAIRING_INFO, &A1_waitPairingWaitingAuxCb, HDCP2X_PAIRING_READ_TIMEOUT_MS);
    }
}

/* Save "AKE_Send_Pairing_Info" message data */
static void A1_waitPairingWaitingAuxCb(void)
{
    /* Check if AKE_Send_Pairing_Info is finished */
    if (CHANNEL_MASTER_isFree()) {

        /* Get pairing info from buffer */
        ENG2T_AKE_Send_Pairing_Info(hdcp2TData.buffer, &hdcp2TData.pairingData);

        RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_STORE_KM);

        hdcp2TData.cb = &A2_sendLcInitCb;
    }
}

/* Send "LC_Init" message to receiver */
static void A2_sendLcInitCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Prepare data for LC_Init command */
        ENG2T_set_LC_Init(hdcp2TData.buffer);

        /* Write LC_Init message */
        writeHdcp22Message(HDCP2X_CMD_LC_INIT, &A2_readLPrime);
    }
}

/* Order "LC_Send_L_prime" message reading */
static void A2_readLPrime(void)
{
    if (CHANNEL_MASTER_isFree()) {
        readHdcp22Message(HDCP2X_CMD_LC_SEND_L_PRIME, &A2_checkReadCb, HDCP2X_LC_SEND_L_PRIME_READ_TIMEOUT_MS);
    }
}

static void A2_checkReadCb(void)
{
    static uint16_t localityCheckCounter = 0U;

    if (CHANNEL_MASTER_isFree()) {
        if (ENG_ENG2T_valid_L(hdcp2TData.buffer)) {
            /* Locality check success */
            hdcp2TData.cb = &A3_exchangeKsCb;
            localityCheckCounter = 0U;
        } else {
            if (localityCheckCounter < (uint16_t)HDCP2X_LOCALITY_CHECK_MAX_ATTEMPS) {
                /* Try another locality check */
                localityCheckCounter++;
                hdcp2TData.cb = &A2_sendLcInitCb;
            } else {
                /* Failure in locality check */
                localityCheckCounter = 0U;
                resetSM(HDCP_ERR_LOCALITY_CHECK_FAIL);
            }
        }

        modRunnerTimeoutClear();
    }
}

static void A3_exchangeKsCb(void)
{
    if (CHANNEL_MASTER_isFree())  {
        ENG2T_set_SKE_Send_Eks(hdcp2TData.buffer, hdcpGenData.contentType);

        /* Write "SKE_Send_Eks" message */
        writeHdcp22Message(HDCP2X_CMD_SKE_SEND_EKS, &A4_testForRepeaterCb);
    }
}

static void A4_testForRepeaterCb(void)
{
    if (hdcp2TData.devType == DEV_HDCP_RECEIVER) {
        /* Device is not a receiver-repeater */
        hdcp2TData.cb = &A5_sendContentyTypeCb;
    } else {
        /* Device is a receiver-repeater */
        hdcp2TData.cb = &A6_WaitForReceiverIdListCb;
    }
}

/* Send content type to receiver. */
static void A5_sendContentyTypeCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        hdcp2TData.buffer[0] = (uint8_t)hdcpGenData.contentType;

        CHANNEL_MASTER_write(HDCP2X_TYPE_SIZE, HDCP2X_TYPE_ADDRESS, hdcp2TData.buffer);


        HDCP_TRAN_sleep(milliToMicro(HDCP2X_RCV_ENCRYPTION_START_LATENCY_MS),
                        milliToMicro(HDCP2X_ENCRYPTION_START_LATENCY_FAST_MS));

        /* Next state: A5_Authenticated */
        hdcp2TData.cb = &A5_authenticatedCb;
    }
}

static void A5_authenticatedCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Authentication success */
        CIPHER_SetAuthenticated();

        /* Poll rx status every second */
        hdcp2TData.status |= (uint16_t)HDCP_STATUS_IS_AUTH_MASK;

        hdcp2TData.cb = &A5_checkRxStatusCb;
    }
}

/* A5_authenticatedCb_checkRxStatus_read_aux */
static void A5_checkRxStatusCb(void)
{
    /* XXX: workaround, to avoid reading previous CP_IRQ */
    if (!hdcpGenData.isMst) {
        setCpIrqEvent((HDCP2X_RXSTATUS_REAUTH_MASK | HDCP2X_RXSTATUS_LINK_AUTH_MASK), CP_IRQ_NO_TIMEOUT, true);
        hdcp2TData.cb = &A1_exchangeKmCb;
    }
}

static void A6_WaitForReceiverIdListCb(void)
{
    /* Poll RxStatus till Ready or max timeout of 3 seconds was achieved*/
    setCpIrqEvent(HDCP2X_RXSTATUS_READY_MASK, HDCP2X_READY_TIMEOUT, !hdcpGenData.isMst);
    modRunnerSleep(milliToMicro(CP_IRQ_LATENCY_TIME_MS));
    hdcp2TData.cb = &A6_readReceiverIdList;
}

/* Order "RepeaterAuth_Send_ReceiverId_List" message reading */
static void A6_readReceiverIdList(void)
{
    if (CHANNEL_MASTER_isFree()) {
        readHdcp22Message(HDCP2X_CMD_RPTR_AUTH_SEND_RECEIVER_ID_LIST, &A7_verifyReceiverIdCb, HDCP2X_NO_TIMEOUT);
    }
}

/**
 * Extra function used to validate topology received by "RepeaterAuth_Send_ReceiverId_List" message
 * @param[in] seqNumV, value received via message
 * @param[in] rxInfo, value received via message
 */
static bool validateTopology(uint32_t seqNumV, uint16_t rxInfo)
{
    bool error;

    /* Check if maximum number of devices or maximum depth was achieved */
    if (((rxInfo & RX_INFO_MAX_CASCADE_EXCEEDED_MASK) != 0U) || ((rxInfo & RX_INFO_MAX_DEVS_EXCEEDED_MASK) != 0U)) {
        error = true;
    } else {
        if (seqNumV != 0U) {
            /* Check if it is first verification */
            error = hdcp2TData.receiverIdListVerif;
        } else {
            error = (hdcp2TData.seqNum.V == 0xFFFFFFU);
        }
    }

    /* When error is 'false', topology is okay! */
    return !error;
}

static void A7_verifyReceiverIdCb(void)
{
    uint32_t seqNumV;
    uint16_t rxInfo;
    uint8_t* msg = hdcp2TData.buffer;
    uint8_t* ksv_list;
    uint32_t mask;
    uint8_t devCount;

    /* Get values from "RepeaterAuth_Send_ReceiverId_List" message */
    ENG2T_get_receiverIdList(msg, &rxInfo, &seqNumV, ksv_list);

    if (validateTopology(seqNumV, rxInfo)) {
        hdcp2TData.seqNum.V = seqNumV;
        hdcp2TData.receiverIdListVerif = false;

        if (ENG2T_verify_receiverIdList(msg, &msg[1])) {
            /* Update local receiverId list */
            mask = (uint32_t)rxInfo & (uint32_t)RX_INFO_DEVICE_COUNT_MASK;
            devCount = (uint8_t)safe_shift32(RIGHT, mask, (uint8_t)RX_INFO_DEVICE_COUNT_OFFSET);
            HDCP_setReceiverIdList(ksv_list, devCount, rxInfo, HDCP_VERSION_2X);

            RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_IS_RECEIVER_ID_VALID);

            hdcp2TData.cb = &A7_waitForRevocationListCb;
        } else {
            /* Fail - V is not equal V' */
            resetSM(HDCP_ERR_V_HASH_MISMATCH);
        }
    } else {
        /* Topology error */
        resetSM(HDCP_ERR_TOPOLOGY_ERROR);

    }
}

static void A7_waitForRevocationListCb(void)
{
    MailboxHdcp_t* mailboxMsg = &(hdcpGenData.mailboxHdcpMsg);

    if ((CHANNEL_MASTER_isFree()) && (mailboxMsg->isReady)) {

        /* Check if receiverId is valid */
        if (mailboxMsg->result != 0U)
        {
            /* Valid receiver ID. Data to send are already in buffer from last step */
            writeHdcp22Message(HDCP2X_CMD_RPTR_AUTH_SEND_ACK, &A9_contentStreamManagementCb);
            HDCP_TRAN_sleep(milliToMicro(10), milliToMicro(1));
        }
        else {
            /* Non valid */
            resetSM(HDCP_ERR_SRM_FAIL);
        }
        mailboxMsg->isReady = false;
    }
}

static void A9_contentStreamManagementCb(void)
{
    if (CHANNEL_MASTER_isFree())
    {
     /* ACK message was sent, now send content stream */

         ENG2T_RA_Stream_Manage(hdcp2TData.buffer,  hdcp2TData.seqNum.M, hdcpGenData.contentType);

         /* RepeaterAuth_Stream_Manage message */
         writeHdcp22Message(HDCP2X_CMD_RPTR_AUTH_STREAM_MG, &A9_readAuthStreamReadyCb);

     } else {
         hdcp2TData.cb = &A5_authenticatedCb;
     }
}

static void A9_readAuthStreamReadyCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Wait for the respond up to 100ms */
        readHdcp22Message(HDCP2X_CMD_RPTR_AUTH_STREAM_READY, &A9_ackResponseCb, HDCP2X_AUTH_STREAM_READY_TIMEOUT_MS);
    }
}

/* A9_Content_Stream_Management_acknowledgement_res */
static void A9_ackResponseCb(void) {

    bool isAuthStreamOk = ENG2T_verify_streamAuth(hdcp2TData.buffer,
                                                             hdcpGenData.contentType,
                                                             (uint8_t *)&hdcp2TData.seqNum.M);
    if (isAuthStreamOk) {
        /* Success */
        HDCP_TRAN_sleep(milliToMicro(HDCP2X_REP_ENCRYPTION_START_LATENCY_MS),
                        milliToMicro(HDCP2X_ENCRYPTION_START_LATENCY_FAST_MS));
        hdcp2TData.cb = A5_authenticatedCb;
        hdcp2TData.seqNum.M++;

        hdcp2TData.status |= (uint16_t)(HDCP_STATUS_STREAM_MG_MASK);
    } else {
        /* Fail */
        hdcp2TData.seqNum.M++;
        hdcp2TData.cb = &A9_contentStreamManagementCb;
    }
}

/**
 * Auxiliary function used to set "AKE_Send_Cert" message request
 * @param[out] dpcdAddress, address of read segment
 * @param[out] dpcdSize, size of read segment
 * @param[out] offset, offset in read buffer
 */
static void akeSendCertHelper(uint32_t* dpcdAddress, uint16_t* dpcdSize, uint32_t* offset)
{
    /* Due to VIP bug this message needs to be read in three separate
     * stages. 522 bytes CertRx, 8 bytes RRx, 3 bytes RxCaps. */
    MsgHelper_t* helper = &msgData.msgHelper;

    *offset = helper->offset;

    if (helper->counter == 0U) {
        *dpcdAddress = (uint32_t)HDCP2X_CERTRX_ADDRESS;
        *dpcdSize = (uint16_t)HDCP2X_CERTRX_SIZE;
    } else if (helper->counter == 1U) {
        *dpcdAddress = (uint32_t)HDCP2X_RRX_ADDRESS;
        *dpcdSize = (uint16_t)HDCP2X_RRX_SIZE;
    } else {
        *dpcdAddress = (uint32_t)HDCP2X_RX_CAPS_ADDRESS;
        *dpcdSize = (uint16_t)HDCP2X_RX_CAPS_SIZE;
    }

    /* Process only one stage during one function call */
    if (helper->counter < 2U) {
         helper->counter++;
         helper->offset += *dpcdSize;
         hdcp2TData.cb = &readHdcpMsg;
    }
}

static void akeInitHelper(uint32_t* dpcdAddress, uint16_t* dpcdSize, uint32_t* offset)
{
    /* Due to VIP bug this message needs to be read in two separate
     * stages. 8 bytes RTx and 3 bytes TxCaps. */
    MsgHelper_t* helper = &msgData.msgHelper;

    *offset = helper->offset;

    /* Process only one stage during one function call */
    if (helper->counter == 0U) {
        *dpcdAddress = HDCP2X_RTX_ADDRESS;
        *dpcdSize = HDCP2X_RTX_SIZE;
        helper->counter++;
        helper->offset += *dpcdSize;
        hdcp2TData.cb = &writeHdcpMsg;
    } else {
        *dpcdAddress = HDCP2X_TX_CAPS_ADDRESS;
        *dpcdSize = HDCP2X_TX_CAPS_SIZE;
    }
}

/* Create request for message reading */
static void readHdcpMsg(void)
{
    uint32_t dpcdAddr;
    uint16_t dpcdSize;
    uint32_t offset = 0U;

    if (CHANNEL_MASTER_isFree()) {

        /* Next state is read from msgData structure */
        hdcp2TData.cb = msgData.nextCb;

        switch (msgData.msgId) {
        case HDCP2X_CMD_AKE_SEND_CERT:
                /* Run sub-procedure for AKE_SEND_CERT */
            akeSendCertHelper(&dpcdAddr, &dpcdSize, &offset);
            break;
        case HDCP2X_CMD_AKE_SEND_H_PRIME:
            dpcdAddr = HDCP2X_H_TAG_ADDRESS;
            dpcdSize = HDCP2X_H_TAG_SIZE;
            break;
        case HDCP2X_CMD_AKE_SEND_PAIRING_INFO:
            dpcdAddr = HDCP2X_EKH_KM_RD_ADDRESS;
            dpcdSize = HDCP2X_EKH_KM_RD_SIZE;
            break;
        case HDCP2X_CMD_LC_SEND_L_PRIME:
            dpcdAddr = HDCP2X_L_TAG_ADDRESS;
            dpcdSize = HDCP2X_L_TAG_SIZE;
            break;
        case HDCP2X_CMD_RPTR_AUTH_SEND_RECEIVER_ID_LIST:
            dpcdAddr = HDCP2X_RX_INFO_ADDRESS;
            dpcdSize = HDCP2X_RX_INFO_SIZE + HDCP2X_SEQ_NUM_V_SIZE
                     + HDCP2X_V_TAG_SIZE + HDCP2X_REC_ID_LIST_SIZE;
            break;
        default:
            /* Case for: HDCP2X_REPEATER_AUTH_STREAM_READY
               msgId is local parameter, so we can use 'default' statement as
               case for specific message */
            dpcdAddr = HDCP2X_M_TAG_ADDRESS;
            dpcdSize = HDCP2X_M_TAG_SIZE;
            break;
        }

        /* Generate read transaction */
        CHANNEL_MASTER_read(dpcdSize, dpcdAddr, &hdcp2TData.buffer[offset]);

    }
}

/* Create request for message writing */
static void writeHdcpMsg(void)
{
    uint32_t dpcdAddr;
    uint16_t dpcdSize;
    uint32_t offset = 0U;

    if (CHANNEL_MASTER_isFree()) {
        /* Next state is read from msgData structure */
        hdcp2TData.cb = msgData.nextCb;

        switch(msgData.msgId) {
        case HDCP2X_CMD_AKE_INIT:
                /* Run sub-procedure for AKE_INIT */
            akeInitHelper(&dpcdAddr, &dpcdSize, &offset);
            break;
        case HDCP2X_CMD_AKE_STORED_KM:
            dpcdAddr = HDCP2X_EKH_KM_WR_ADDRESS;
            dpcdSize = HDCP2X_EKH_KM_WR_SIZE + HDCP2X_M_SIZE;
            break;
        case HDCP2X_CMD_AKE_NO_STORED_KM:
            dpcdAddr = HDCP2X_EKPUB_KM_ADDRESS;
            dpcdSize = HDCP2X_EKPUB_KM_SIZE;
            break;
        case HDCP2X_CMD_LC_INIT:
            dpcdAddr = HDCP2X_RN_ADDRESS;
            dpcdSize = HDCP2X_RN_SIZE;
            break;
        case HDCP2X_CMD_SKE_SEND_EKS:
            dpcdAddr = HDCP2X_EDKEY_KS_ADDRESS;
            dpcdSize = HDCP2X_EDKEY_KS_SIZE + HDCP2X_RIV_SIZE;
            break;
        case HDCP2X_CMD_RPTR_AUTH_SEND_ACK:
            dpcdAddr = HDCP2X_V_ADDRESS;
            dpcdSize = HDCP2X_V_SIZE;
            break;
        default:
            /* Case for: HDCP2X_CMD_RPTR_AUTH_STREAM_MG
               msgId is local parameter, so we can use 'default' statement as
               case for specific message */
            dpcdAddr = HDCP2X_SEQ_NUM_M_ADDRESS;
            dpcdSize = HDCP2X_SEQ_NUM_M_SIZE + HDCP2X_K_SIZE + HDCP2X_STREAM_ID_TYPE_SIZE;
            break;
        }
        /* Generate write transaction */
        CHANNEL_MASTER_write(dpcdSize, dpcdAddr, &hdcp2TData.buffer[offset]);
    }
}

/**
* Initialization function for HDCP v2.2
*/
void HDCP2X_TRAN_init(void)
{
    /* Set the hdcp buffer */
    hdcp2TData.buffer = HDCP_TRAN_getBuffer();
    /* Set the callback to next state function */
    hdcp2TData.cb = &A1_exchangeKmCb;

    if (lib_handler.rsaRxstate > 0U) {
        LIB_HANDLER_Clean();
    }

    /* Set HDCP version to 2X */
    RegWrite(HDCP_DP_CONFIG, RegFieldWrite(HDCP_DP_CONFIG, HDCP_DP_VERSION, 0U, (uint32_t)HDCP_VERSION_2X));
}

/**
 * State machine for HDCP v2.2
 */
void HDCP2X_TRAN_handleSM(void)
{
    if (hdcp2TData.cb != NULL) {
        (hdcp2TData.cb)();
    } else {
        hdcp2TData.cb = &A1_exchangeKmCb;
    }

    /* Will be not set if error was occurred previously */
    HDCP_TRAN_setStatus(hdcp2TData.status);
}

/**
 * Get pairing data of HDCP v2.2
 * @param[in/out] buffer, pointer to buffer where data will be copied
 */
void HDCP2X_getPairingData(uint8_t* buffer)
{
    HdcpTransactionPairingData_t* data = &hdcp2TData.pairingData;
    uint8_t* bufferAddr = buffer;

    /* Copy data to buffer and update address */
    CPS_BufferCopy(bufferAddr, data->receiverId, HDCP_REC_ID_SIZE);
    bufferAddr = &bufferAddr[HDCP_REC_ID_SIZE];

    CPS_BufferCopy(bufferAddr, data->m, HDCP2X_M_SIZE);
    bufferAddr = &bufferAddr[HDCP2X_M_SIZE];

    CPS_BufferCopy(bufferAddr, data->Km, HDCP2X_EKH_KM_RD_SIZE);
    bufferAddr = &bufferAddr[HDCP2X_EKH_KM_RD_SIZE];

    CPS_BufferCopy(bufferAddr, data->Ekh, HDCP2X_EKH_KM_RD_SIZE);
}
/* parasoft-end-suppress METRICS-36 */

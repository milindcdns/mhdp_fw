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
 * hdcp14_tran.c
 *
 ******************************************************************************
 */

#include "hdcp14.h"
#include "hdcp14_tran.h"
#include "hdcp_tran.h"
#include "engine1T.h"
#include "modRunner.h"
#include "utils.h"
#include "timer.h"
#include "controlChannelM.h"
#include "cipher_handler.h"
#include "general_handler.h"
#include "reg.h"
#include "cdn_stdtypes.h"
#include "cp_irq.h"
#include "cdn_errno.h"
#include "events.h"

typedef struct {
    /* Pointer to current state function */
    StateCallback_t cb;
    /* Is device receiver or repeater-receiver */
    HdcpDevType_t devType;
    /* Current size of Ksv list */
    uint8_t ksvs_count;
    /* Pointer to Ksv list */
    uint8_t *ksv_list;
    /* Current value of Binfo register */
    uint16_t binfo;
    /* Current status of HDCP module */
    uint16_t status;
    /* Pointer to HDCP AUX buffer */
    uint8_t* buffer;
    /* Number of failed matches */
    uint8_t attempt;
} Hdcp1TData_t;

static Hdcp1TData_t hdcp1TData;

/* Clearing authentication and prepare SM to work */
static void A0_clearAuthCb(void);

/* Send AUX request to read BCaps register */
static void A1_readBcapsCb(void);

/* Do operations based on Bcaps value */
static void A1_processBcapsCb(void);

/* Generate and send to receiver An key */
static void A1_writeAnCb(void);

/* Send to receiver Aksv key (Ksv of transmitter */
static void A1_writeAksvCb(void);

/* Send AUX request to read Bksv key from receiver */
static void A1_readBksvCb(void);

/* Verify received Bksv key */
static void A1_processBksvCb(void);

/* Verify version of receiver's DPCD */
static void A1_checkDpcdRevisionCb(void);

/* Fold Ainfo register value and write it */
static void A1_writeAinfoCb(void);

/* Compute Km key value */
static void A2_computeKmCb(void);

/* Wait for KmDone and if is do LFSR calculation */
static void A2_LFSR_calculationsCb(void);

/* Wait for PrnmDone and if is compute M0 and R0 values */
static void A2_M0_R0_computeCb(void);

/* Start SRM checking */
static void A3_srmCheckCb(void);

/* Validate SRM checking */
static void A3_srmResultCb(void);

/* Send AUX request to read R0' value */
static void A3_readR0Cb(void);

/* Compare R0 and R0' values */
static void A3_processR0Cb(void);

/* Authenticated done successfully */
static void A4_authenticatedCb(void);

/* Check if receiver is also repeater and set state due to it */
static void A5_testIfRepeaterCb(void);

/* Send AUX request to read Binfo register */
static void A7_readBinfoCb(void);

/* Check if max number of devices was exceeded or cascade error occurred */
static void A7_validateTopologyCb(void);

/* Read Ksv list from Ksv Fifo */
static void A7_readKsvListCb(void);

/* Update local Ksv list and notify host about availability */
static void A7_verifyKsvListCb(void);

/* Wait from response from Host if ReceiverId is okay */
static void A7_waitForKsvListIsValidMsgCb(void);

/* Send AUX request to read V' value */
static void A7_readVCb(void);

/* Check repeater integrity by compare V and V' */
static void A7_checkRepeaterIntegrityCb(void);

/**
 * Function sets error code and resets state of machine
 * @param[in] errorCode, code of error that occured
 */
/* parasoft-begin-suppress METRICS-36 "Function is called from more than 5 different functions in translation unit, DRV-3823" */
static inline void resetSM(HdcpTransactionError_t errorCode)
{
    HDCP_TRAN_setError(errorCode);
    modRunnerTimeoutClear();
    hdcp1TData.cb = &A0_clearAuthCb;
}
/* parasoft-end-suppress METRICS-36 */

/**
 * Return MAX_DEV_EXCEEDED flag value from Binfo register
 * @param[in] binfo, value of register
 * @return 'true' if flag is set or 'false' if not set
 */
static inline bool isMaxDevExceeded(uint16_t binfo)
{
    return (binfo & (uint16_t)HDCP1X_BINFO_MAX_DEVS_EXCEEDED_MASK) != 0U;
}

/**
 * Return MAX_CASCADE_EXCEEDED flag value from Binfo register
 * @param[in] binfo, value of register
 * @return 'true' if flag is set or 'false' if not set
 */
static inline bool isMaxCascadeExceeded(uint16_t binfo)
{
    return (binfo & (uint16_t)HDCP1X_BINFO_MAX_CASCADE_EXCEEDED_MASK) != 0U;
}

/**
 * Returns number of devices from Binfo register
 * @param[in] binfo, value of register
 */
static inline uint8_t getDeviceCount(uint16_t binfo)
{
    /* Casting is to uint8_t is allowed, because devCount is on [6:0] bits in register */
    return (uint8_t)binfo & (uint8_t)HDCP1X_BINFO_DEV_COUNT_MASK;
}

/**
 * Return value of R0 from read data
 * @param[in] msg, read data
 */
static inline uint16_t getR0(const uint8_t* r0)
{
    return getLe16(r0);
}

/**
 * Used to get Binfo value from read buffer
 * @param[in] buffer, pointer to read data
 * @return value of Binfo register
 */
static inline uint16_t getBinfo(const uint8_t* buffer)
{
    return getLe16(buffer);
}

/* Returns 'true' if  device is HDCP capable, otherwise 'false' */
inline static bool isHdcpCapable(uint8_t bcaps)
{
    return (bcaps & (uint8_t)HDCP1X_BCAPS_HDCP_CAPABLE_MASK) != 0U;
}

/* Returns 'true' if receiver is also repeater, otherwise false */
inline static bool isRepeater(uint8_t bcaps)
{
    return (bcaps & (uint8_t)HDCP1X_BCAPS_REPEATER_MASK) != 0U;
}

/**
 * Auxiliary function used to get device type
 * @returns DEV_HDCP_REPEATER if device is repeater, otherwise DEV_HDCP_RECEIVER
 */
static HdcpDevType_t getDeviceType(uint8_t bcaps)
{
    HdcpDevType_t devType = DEV_HDCP_RECEIVER;

    /* Check 'REPEATER' bit in Bcaps register */
    if (isRepeater(bcaps)) {
        devType = DEV_HDCP_REPEATER;
    }

    return devType;
}

/**
 * Function used to clear internal data of module. Should be called
 * only on initialization or SM reset
 */
static void clearHdcpData(void)
{
    /* Cleanup internal data */
    hdcp1TData.devType = DEV_NON_HDCP_CAPABLE;
    hdcp1TData.binfo = 0U;
    hdcp1TData.ksv_list = NULL;
    hdcp1TData.ksvs_count = 0U;
}

static void A0_clearAuthCb(void)
{
    /* Clear process status */
    CIPHER_ClearAuthenticated();
    modRunnerTimeoutClear();
    clearHdcpData();

    /* HDCP v1.x is used */
    hdcp1TData.status = (uint16_t)safe_shift32(LEFT, (uint32_t)HDCP_RX_TYPE_1X, HDCP_STATUS_HDCP_TYPE_OFFSET);
    HDCP_TRAN_setError(HDCP_ERR_NO_ERROR);

    hdcp1TData.cb = &A1_readBcapsCb;
}

static void A1_readBcapsCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Read Bcaps register */
        CHANNEL_MASTER_read(HDCP1X_BCAPS_SIZE, HDCP1X_BCAPS_ADDRESS, hdcp1TData.buffer);

        hdcp1TData.cb = &A1_processBcapsCb;
    }
}

static void A1_processBcapsCb(void)
{
    uint8_t bcaps;

    if (CHANNEL_MASTER_isFree()) {

        /* Read and store Bcaps */
        bcaps = hdcp1TData.buffer[0];

        /* Check if HDCP device is capable */
        if (isHdcpCapable(bcaps)) {

            /* Get device type */
            hdcp1TData.devType = getDeviceType(bcaps);

            /* Update status if is receiver-repeater */
            if (hdcp1TData.devType == DEV_HDCP_REPEATER) {
                /* Set device as repeater bit in status word */
                hdcp1TData.status |= (uint16_t)HDCP_STATUS_DEVICE_TYPE_MASK;
            }

            hdcp1TData.cb = &A1_writeAnCb;

        } else {
            resetSM(HDCP_ERR_RSVD_NOT_ZERO);
        }
    }
}

static void A1_writeAnCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Fill hdcp_tran_buffer with An value */
        ENG1T_getAn(hdcp1TData.buffer);

        /* Send Transmitter's An value.
         * This step starts/restarts authentication process. */
        CHANNEL_MASTER_write(HDCP1X_AN_SIZE, HDCP1X_AN_ADDRESS, hdcp1TData.buffer);

        hdcp1TData.cb = &A1_writeAksvCb;
    }
}

static void A1_writeAksvCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Fill hdcp_tran_buffer with AKSV value */
        ENG1T_getAksv(hdcp1TData.buffer);

        /* Send Transmitter's KSV value */
        CHANNEL_MASTER_write(HDCP1X_AKSV_SIZE, HDCP1X_AKSV_ADDRESS, hdcp1TData.buffer);

        hdcp1TData.cb = &A1_readBksvCb;
    }
}

static void A1_readBksvCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Channel is free -> it means that Aksv was written correctly */
        modRunnerSetTimeout(milliToMicro(HDCP1X_R0_PRIME_TIMEOUT_MS));
        CHANNEL_MASTER_read(HDCP1X_BKSV_SIZE, HDCP1X_BKSV_ADDRESS, hdcp1TData.buffer);
        hdcp1TData.cb = &A1_processBksvCb;
    }
}

static void A1_processBksvCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        ENG1T_setBksv(hdcp1TData.buffer);

        if (ENG1T_verifyBksv() == CDN_EOK) {
            /* If Receiver is Repeater then set Ainfo register */
            if (hdcp1TData.devType == DEV_HDCP_REPEATER) {
                /* Read DPCD_REV register to check if RX Rep is DPCD 1.2+ capable */
                CHANNEL_MASTER_read(1U, DPCD_DCPD_REV_ADDRESS, hdcp1TData.buffer);
                hdcp1TData.cb = &A1_checkDpcdRevisionCb;
            } else {
                hdcp1TData.cb = &A2_computeKmCb;
            }


        } else {
            resetSM(HDCP_ERR_SRM_FAIL);
        }
    }
}

static void A1_checkDpcdRevisionCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Check if DPCD Receiver Revision is greater than 1p2 */
        if (hdcp1TData.buffer[0] >= (uint8_t)DPCD_RX_REV_1p2) {
            hdcp1TData.cb = &A1_writeAinfoCb;
        } else {
            hdcp1TData.cb = &A2_computeKmCb;
        }
    }
}

static void A1_writeAinfoCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Set REAUTHENTICATION_ENABLE_IRQ_HPD bit */
        hdcp1TData.buffer[0] = (uint8_t)HDCP1X_AINFO_REAUTHENTICATION_ENABLE_IRQ_HPD_MASK;

        /* and write it to AINFO register */
        CHANNEL_MASTER_write(HDCP1X_AINFO_SIZE, HDCP1X_AINFO_ADDRESS, hdcp1TData.buffer);

        hdcp1TData.cb = &A2_computeKmCb;
    }
}

static void A2_computeKmCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        ENG1T_computeKm();

        /* Check SRM first to give Receiver time to prepare R0' */
        hdcp1TData.cb = &A2_LFSR_calculationsCb;
    }
}

static void A2_LFSR_calculationsCb(void)
{
    /* Check if Km is calculated */
    if (ENG1T_isKmDone()) {
        /* Do LFSR calculation */
        ENG1T_LFSR_calculation(hdcp1TData.devType);
        hdcp1TData.cb = &A2_M0_R0_computeCb;
    }
}

static void A2_M0_R0_computeCb(void)
{
    /* Check if LFSR calculations are finished */
    if (ENG1T_isPrnmDone()) {
        /* Compute R0 and M0 keys */
        ENG1T_compute_M0_R0(hdcp1TData.devType);
        hdcp1TData.cb = &A3_srmCheckCb;
    }
}

static void A3_srmCheckCb(void)
{
    /* One BKSV, since we are not a HDPC Repeater */
    hdcpGenData.rid.command[0] = 1;
    /* Not HDCP Repeater */
    hdcpGenData.rid.command[1] = 0;
    /* Set BKSV */
    ENG1T_getBksv(&hdcpGenData.rid.command[2]);

    /* KSV size + num of KSVs + Repeater info + Binfo size */
    hdcpGenData.rid.size = HDCP_REC_ID_SIZE + 4U;

    /* Inform Host to read and validate the Receiver ID's */
    RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_IS_RECEIVER_ID_VALID);

    hdcp1TData.cb = &A3_srmResultCb;
}

static void A3_srmResultCb(void)
{
    MailboxHdcp_t* msgData = &hdcpGenData.mailboxHdcpMsg;
    bool cpIrq;

    /* Check if there is pending message from host */
    if (msgData->isReady) {

        /* No HDCP Receiver is on Revocation List */
        if (msgData->result != 0U) {

            cpIrq = (hdcp1TData.devType == DEV_HDCP_RECEIVER) && (!hdcpGenData.isMst);
            /* Start waiting for ' R0' AVAILABLE' assertion */
            /* TODO mbeberok: workaround, This part of code should be removed when UVM will be updated, VIPDISPLAY-1405
             * CP_IRQ should be used for both, Receivers and Repeaters. SM will be waiting for CP_IRQ */
            setCpIrqEvent(HDCP1X_BSTATUS_IS_R0_AVAILABLE_MASK, CP_IRQ_NO_TIMEOUT, cpIrq);

            hdcp1TData.cb = &A3_readR0Cb;
            hdcp1TData.attempt = 1U;
        } else {
            resetSM(HDCP_ERR_SRM_FAIL);
        }

        msgData->isReady = false;
    }
}

static void A3_readR0Cb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Read R0' value from sink device */
        CHANNEL_MASTER_read(HDCP1X_R0_PRIME_SIZE, HDCP1X_R0_PRIME_ADDRESS, hdcp1TData.buffer);
        hdcp1TData.cb = &A3_processR0Cb;
    }
}

static void A3_processR0Cb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Extract R0' value */
        uint16_t realRo = getR0(hdcp1TData.buffer);

        /* Check if R0' matches the R0 */
        if (ENG1T_compareR0(realRo) == CDN_EOK) {
            /* Enable Authentication as soon as read R0' is valid */
            CIPHER_SetAuthenticated();

            // XXX DK: to match sequence from .e test
            RegWrite(HDCP_DP_CONFIG, RegFieldWrite(HDCP_DP_CONFIG, HDCP_DP_VERSION, 0U, (uint32_t)HDCP_VERSION_1X));
            hdcp1TData.cb = &A5_testIfRepeaterCb;

        } else {
            /* If number of attempts was used, reset SM. If not, try again */
            if (hdcp1TData.attempt > (uint8_t)HDCP1X_R0_PRIME_VALIDATE_MAX_ATTEMPS) {
                resetSM(HDCP_ERR_RI_MISMATCH);
            } else {
                hdcp1TData.attempt++;
                hdcp1TData.cb = &A3_readR0Cb;
            }
        }
    }
}

static void A4_authenticatedCb(void)
{
    /* Update status */
    hdcp1TData.status |= (uint16_t)HDCP_STATUS_IS_AUTH_MASK;

    /* Check potential integrity error */
    setCpIrqEvent(((uint8_t)HDCP1X_BSTATUS_LINK_INTEGRITY_FAILURE_MASK | (uint8_t)HDCP1X_BSTATUS_REAUTHENTICATION_REQ_MASK), CP_IRQ_NO_TIMEOUT, true);
    hdcp1TData.cb = &A0_clearAuthCb;
}


static void A5_testIfRepeaterCb(void)
{
    /* If device is DEV_HDCP_REPEATER start waiting for 'READY' from Bstatus
     * If not, protocol is authenticated */
    if (hdcp1TData.devType == DEV_HDCP_REPEATER) {

        /* TODO mbeberok: workaround, CP_IRQ will be used when UVM will be updated, VIPDISPLAY-1405
           SM will be waiting for CP_IRQ there */
        setCpIrqEvent((uint8_t)HDCP1X_BSTATUS_READY_MASK, HDCP1X_WAIT_FOR_READY_TIMEOUT_MS, false);
        hdcp1TData.cb = &A7_readBinfoCb;

    } else {
        hdcp1TData.cb = &A4_authenticatedCb;
    }
}

static void A7_readBinfoCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Read Binfo register */
        CHANNEL_MASTER_read(HDCP1X_BINFO_SIZE, HDCP1X_BINFO_ADDRESS, hdcp1TData.buffer);

        hdcp1TData.cb = &A7_validateTopologyCb;
    }
}

static void A7_validateTopologyCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Extract and store Binfo register value */
        hdcp1TData.binfo = getBinfo(hdcp1TData.buffer);

        /* Check if there's a topology error */
        if ((isMaxDevExceeded(hdcp1TData.binfo)) || (isMaxCascadeExceeded(hdcp1TData.binfo))) {
            resetSM(HDCP_ERR_TOPOLOGY_ERROR);
        } else {
            /*
             * Assign buffer for computations regardless if device count > 0
             * hdcp_tran_buffer pointer will keep original pointer until all KSVs
             * are read.
             */
            hdcp1TData.ksv_list = hdcp1TData.buffer;
            hdcp1TData.ksvs_count = getDeviceCount(hdcp1TData.binfo);

            hdcp1TData.cb = &A7_readKsvListCb;
        }
    }
}

static void A7_readKsvListCb(void)
{
    uint8_t ksvs_to_read;
    uint8_t bytes_to_read;

    if (CHANNEL_MASTER_isFree()) {

        if (hdcp1TData.ksvs_count > 0U) {

            /* Reading must be done in steps. Ksv size is 15 bytes and data for one device
             * are 5 bytes, so in one transaction data for only 3 devices can be read */
            ksvs_to_read = get_minimum(HDCP1X_RID_LIST_MAX_IDS_PER_READ, hdcp1TData.ksvs_count);
            bytes_to_read = ksvs_to_read * (uint8_t)HDCP_REC_ID_SIZE;

            /* Read KSVs */
            CHANNEL_MASTER_read(bytes_to_read, HDCP1X_KSV_FIFO_ADDRESS, hdcp1TData.ksv_list);

            /* Increment KSV List pointer */
            hdcp1TData.ksv_list = &(hdcp1TData.ksv_list[bytes_to_read]);
            hdcp1TData.ksvs_count -= ksvs_to_read;

            /* Stay in this state until all KSVs are read! */
        } else {
            /*
             * Restore ksvs_count value as it was used and changed during KSV
             * List reading for tracking read count.
             */
            hdcp1TData.ksvs_count = getDeviceCount(hdcp1TData.binfo);
            /*
             * For KSV List reading ksv_list pointer was used and the
             * hdcp_tran_buffer still points for first KSV, so here
             * ksv_list pointer is restored.
             */
            hdcp1TData.ksv_list = hdcp1TData.buffer;

            ENG1T_getKsvListAndComputeV(hdcp1TData.ksv_list, hdcp1TData.ksvs_count, hdcp1TData.binfo);

            hdcp1TData.cb = &A7_verifyKsvListCb;
        }
    }
}

static void A7_verifyKsvListCb(void)
{
    /* No need to ask Host for SRM check for an empty KSV list */
    if (hdcp1TData.ksvs_count == 0U) {
        hdcp1TData.cb = &A7_readVCb;
    } else {

        /* Prepare HDCP_TX_IS_RECEIVER_ID_VALID Command response */
        HDCP_setReceiverIdList(hdcp1TData.ksv_list, hdcp1TData.ksvs_count, hdcp1TData.binfo, HDCP_VERSION_1X);

        /* Notify Host that KSV list is ready for reading and verification */
        RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_IS_RECEIVER_ID_VALID);

        hdcp1TData.ksv_list = NULL;
        hdcp1TData.ksvs_count = 0U;

        hdcp1TData.cb = &A7_waitForKsvListIsValidMsgCb;
    }
}

static void A7_waitForKsvListIsValidMsgCb(void)
{
    MailboxHdcp_t* msgData = &hdcpGenData.mailboxHdcpMsg;

    /* Check if message from Host is pending */
    if (msgData->isReady) {
        /* Check if ReceiverID is valid */
        if (msgData->result != 0U) {
            hdcp1TData.cb = &A7_readVCb;

            /* Counter used to store number of V-V' match attempts */
            hdcp1TData.attempt = 1U;

        } else {
            resetSM(HDCP_ERR_SRM_FAIL);
        }
    }
}

static void A7_readVCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Read V' value */
        CHANNEL_MASTER_read(HDCP1X_V_PRIME_SIZE, HDCP1X_V_PRIME_ADDRESS, hdcp1TData.buffer);
        hdcp1TData.cb = &A7_checkRepeaterIntegrityCb;
    }
}

static void A7_checkRepeaterIntegrityCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Check if V' value matches the V */
        if (ENG1T_validateV(hdcp1TData.buffer)) {
            hdcp1TData.cb = &A4_authenticatedCb;
        } else {
            /* If V is not equal V' try again or report error if number of
             * invalid attempts is exhausted */
            if (hdcp1TData.attempt > (uint8_t)HDCP1X_V_PRIME_VALIDATE_MAX_ATTEMPS) {
                resetSM(HDCP_ERR_V_HASH_MISMATCH);
            } else {
                hdcp1TData.attempt++;
                hdcp1TData.cb = &A7_readVCb;
            }
        }
    }
}

void HDCP14_TRAN_Init(void)
{
    /* Clear process data */
    hdcp1TData.cb = &A0_clearAuthCb;
    hdcp1TData.buffer = HDCP_TRAN_getBuffer();

    RegWrite(HDCP_DP_CONFIG, RegFieldWrite(HDCP_DP_CONFIG, HDCP_DP_VERSION, 0U, (uint32_t)HDCP_VERSION_1X));
}

void HDCP14_TRAN_handleSM(void)
{
    /* Call SM function */
    if (hdcp1TData.cb != NULL) {
        (hdcp1TData.cb)();
    } else {
        hdcp1TData.cb = &A0_clearAuthCb;
    }


    HDCP_TRAN_setStatus(hdcp1TData.status);
}

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
 * hdcp_tran.c
 *
 ******************************************************************************
 */

#include "hdcp_tran.h"
#include "cdn_stdtypes.h"
#include "cipher_handler.h"
#include "controlChannelM.h"
#include "cp_irq.h"
#include "cps.h"
#include "dp_tx_mail_handler.h"
#include "engine1T.h"
#include "engine2T.h"
#include "general_handler.h"
#include "hdcp14.h"
#include "hdcp14_tran.h"
#include "hdcp2.h"
#include "hdcp2_tran.h"
#include "libHandler.h"
#include "mailBox.h"
#include "modRunner.h"
#include "reg.h"
#include "utils.h"
#include "events.h"

/* Masks of bits used in configuration message */
#define HDCP_CFG_VERSION_SUPPORT_MASK 0x03U
#define HDCP_CFG_START_TX_MASK        0x04U
#define HDCP_CFG_CONTENT_TYPE_MASK    0x08U
#define HDCP_CFG_KM_ENCRYPTION_MASK   0x10U

/* Definition of message handlers pointer */
typedef void (*HdcpMailboxMsgHandler_t)(const MailboxData_t *mailboxData);

uint8_t pHdcpLc128[LC_128_LEN];

HdcpGenTransData_t hdcpGenData;

/* Wait for configuration message */
static void waitForConfigCb(void);

/* Check if required HDCP module is capable */
static void checkCapabilityCb(void);

/* Initialize HDCP module */
static void hdcpInitCb(void);

/* Perform next step from authentication procedure */
static void hdcpWorkingCb(void);

/**
 * Auxiliary function used to inform specified HDCP module about receiver message (via mailbox)
 * @param[in] result, byte of data transfered into module
 */
inline static void setMsgReady(uint8_t result) {
    hdcpGenData.mailboxHdcpMsg.isReady = true;
    hdcpGenData.mailboxHdcpMsg.result = result;
}

/**
 * Returns information which HDCP version should be used
 * @param[in] cfg, byte of configuration data (received via mailbox)
 */
inline static HdcpVerSupport_t getHdcpSupportedMode(uint8_t cfg) {
    HdcpVerSupport_t version;
    uint8_t val = cfg & (uint8_t)HDCP_CFG_VERSION_SUPPORT_MASK;

    if (val == (uint8_t)HDCP_2_SUPPORT) {
        version = HDCP_2_SUPPORT;
    } else if (val == (uint8_t)HDCP_1_SUPPORT) {
        version = HDCP_1_SUPPORT;
    } else if (val == (uint8_t)HDCP_BOTH_SUPPORT) {
        version = HDCP_BOTH_SUPPORT;
    } else {
        /* Catch invalid mode */
        version = HDCP_RESERVED;
    }

    return version;
}

/**
 * Returns information about used content type
 * @param[in] cfg, byte of configuration data (received via mailbox)
 */
inline static HdcpContentStreamType_t getContentType(uint8_t cfg) {
    HdcpContentStreamType_t contentType;

    if ((cfg & (uint8_t)HDCP_CFG_CONTENT_TYPE_MASK) != 0U) {
        contentType = HDCP_CONTENT_TYPE_1;
    } else {
        contentType = HDCP_CONTENT_TYPE_0;
    }

    return contentType;
}

/**
 * Starts working of module if required
 * @param[in] cfg, byte of configuration data (received via mailbox)
 */
inline static void startModuleIfGiven(uint8_t cfg) {
    if ((cfg & (uint8_t)HDCP_CFG_START_TX_MASK) == 0U) {
        CIPHER_ClearAuthenticated();
        hdcpGenData.stateCb = &waitForConfigCb;
    }
}

/**
 * Returns if custom KM encryption code will be used
 * @param[in] cfg, byte of configuration data (received via mailbox)
 */
inline static bool getKmEncrypt(uint8_t cfg) {
    return (cfg & (uint8_t)HDCP_CFG_KM_ENCRYPTION_MASK) != 0U;
}

static void waitForConfigCb(void) {
    /* Send empty status word */
    HDCP_TRAN_setStatus(0U);
}

/* Used to read RxCaps buffer (for HDCP v2.x). Value of register is needed to check if module is capable */
static void sendCapabilityReqCb(void) {
    if (CHANNEL_MASTER_isFree()) {
        /* Read RxCaps */
        CHANNEL_MASTER_read(HDCP2X_RX_CAPS_SIZE, HDCP2X_RX_CAPS_ADDRESS, hdcpGenData.hdcpBuffer);
        hdcpGenData.stateCb = &checkCapabilityCb;
    }
}

/* Used to handle HPD pulse and low interrupts
 * @return 'true' if connection is lost (HPD_LOW), otherwise 'false'
 */
static bool isHpdDown(void) {
    bool isDown = ((hpdState & (uint8_t)DP_TX_EVENT_CODE_HPD_LOW) != 0U);

    /* Handle HPD_PULSE interrupt */
    if ((hpdState & (uint8_t)DP_TX_EVENT_CODE_HPD_PULSE) != 0U) {
        hdcpGenData.hpdPulseIrq = true;
        hpdState &= ~((uint8_t)DP_TX_EVENT_CODE_HPD_PULSE);
    }

    if (isDown) {
        hpdState &= ~((uint8_t)DP_TX_EVENT_CODE_HPD_LOW);
    }

    return isDown;
}

/* TODO mbeberok: workaround, will be removed
 * - HDCP v1.3: MST receiver don't generate HPD_PULSE;
 * - HDCP v2.2: for MST and DSC CP_IRQ is not set in DEVICE_SERVICE_VECTOR
 *              for MST without DSC receiver don't generate HPD_PULSE */
static void checkIfMst(void) {
    if (CHANNEL_MASTER_isFree()) {
        CHANNEL_MASTER_read(1U, MSTM_CAP_ADDRESS, hdcpGenData.hdcpBuffer);
        hdcpGenData.stateCb = &hdcpInitCb;
    }
}

static void checkCapabilityCb(void) {
    bool hdcp2IsCapable;

    if (CHANNEL_MASTER_isFree()) {

        /* Read 'CAPABLE' bit from RxCaps */
        hdcp2IsCapable = ((hdcpGenData.hdcpBuffer[2] & (uint8_t)HDCP2X_RXCAPS_IS_CAPABLE_MASK) != 0U);

        /* If both (HDCP v1.4 and v2.2) are supported - first check v2.2. If not capable, run
         * authentication procedure with v1.4 */
        if (hdcp2IsCapable) {
            hdcpGenData.usedHdcpVer = HDCP_VERSION_2X;
            hdcpGenData.hdcpHandler.initCb = &HDCP2X_TRAN_init;
            hdcpGenData.hdcpHandler.threadCb = &HDCP2X_TRAN_handleSM;
            hdcpGenData.stateCb = &checkIfMst;
        } else {
            if (hdcpGenData.supportedMode == HDCP_BOTH_SUPPORT) {
                hdcpGenData.usedHdcpVer = HDCP_VERSION_1X;
                hdcpGenData.hdcpHandler.initCb = &HDCP14_TRAN_Init;
                hdcpGenData.hdcpHandler.threadCb = &HDCP14_TRAN_handleSM;
                hdcpGenData.stateCb = &checkIfMst;
            } else {
                /* If only HDCP v2.2 is available wait as long as will be capable */
                hdcpGenData.stateCb = &sendCapabilityReqCb;
                HDCP_TRAN_sleep(milliToMicro(25), 0);
            }
        }
    }
}

/* HDCP init callback */
static void hdcpInitCb(void) {
    if (CHANNEL_MASTER_isFree()) {
        hdcpGenData.isMst = (hdcpGenData.hdcpBuffer[0] & MSTM_CAP_MST_CAP_MASK) != 0U;

        /* Call initialization function and go to next state */
        hdcpGenData.hdcpHandler.initCb();

        initCpIrqRoutine();

        hdcpGenData.stateCb = &hdcpWorkingCb;
    }
}

static void hdcpWorkingCb(void) {
    /* Call thread function */
    if (isCpIrqRoutineFinished()) {
        hdcpGenData.hdcpHandler.threadCb();
    } else {
        callCpIrqRoutine();
    }
}

/* Used to call proper flow due to required version of HDCP */
static void checkHdcpVersion(void) {
    if (hdcpGenData.supportedMode != HDCP_1_SUPPORT) {
        /* If HDCP 2.x is required, check if capable */
        hdcpGenData.stateCb = &sendCapabilityReqCb;
    } else {
        /* HDCP v1.4 need not checking capability - can start authentication */
        hdcpGenData.usedHdcpVer = HDCP_VERSION_1X;
        hdcpGenData.hdcpHandler.initCb = &HDCP14_TRAN_Init;
        hdcpGenData.hdcpHandler.threadCb = &HDCP14_TRAN_handleSM;
        hdcpGenData.stateCb = &checkIfMst;
    }
}

/**
 * Should be call when any error occurred (clean module and inform Host about error)
 * @param[in] errorCode, code of error which occurred
 */
static void catchError(HdcpTransactionError_t errorCode) {
    HDCP_TRAN_setError(errorCode);

    /* End current transaction */
    CHANNEL_MASTER_transactionOver();

    CIPHER_ClearAuthenticated();

    if (lib_handler.rsaRxstate > 0U) {
        LIB_HANDLER_Clean();
    }

    modRunnerTimeoutClear();

    /* Stop authentication */
    hdcpGenData.stateCb = NULL;
}

/* Used to inform host about change of module status */
static void notifyHostAboutStatusChange(void) {
    /* Notify host */
    RegWrite(XT_EVENTS0, (uint8_t)EVENT_ID_HDCPTX_STATUS);

    /* Clear notifier */
    hdcpGenData.statusUpdate = false;
}

/**
 *  Handler for "HDCP_TRAN_CONFIGURATION" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void config_handler(const MailboxData_t *mailboxData) {
    hdcpGenData.supportedMode = getHdcpSupportedMode(mailboxData->message[0]);

    if (hdcpGenData.supportedMode != HDCP_RESERVED) {
        /* Valid mode was passed */
        hdcpGenData.contentType = getContentType(mailboxData->message[0]);
        hdcpGenData.customKmEnc = getKmEncrypt(mailboxData->message[0]);

        checkHdcpVersion();

        startModuleIfGiven(mailboxData->message[0]);
    }
}

/**
 *  Handler for "HDCP2X_TX_SET_PUBLIC_KEY_PARAMS" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void set_public_keys_params_handler(const MailboxData_t *mailboxData) {
    ENG2T_SetKey(&mailboxData->message[0], &mailboxData->message[HDCP2X_PUB_KEY_MODULUS_N_SIZE]);
}

/**
 *  Handler for "HDCP_TRAN_SET_DEBUG_RANDOM_NUMBERS" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void set_debug_random_handler(const MailboxData_t *mailboxData) {
    ENG2T_SetDebugRandomNumbers(mailboxData->message, false);
}

/**
 *  Handler for "HDCP2X_TX_SET_KM_KEY_PARAMS" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void set_custom_km_enc_handler(const MailboxData_t *mailboxData) {
    ENG2T_SetDebugRandomNumbers(mailboxData->message, true);
}

/**
 *  Handler for "HDCP1_TX_SEND_KEYS" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void set_hdcp1_keys_handler(const MailboxData_t *mailboxData) {
    ENG1T_loadKeys(mailboxData->message, &mailboxData->message[HDCP1X_AKSV_SIZE]);
}

/**
 *  Handler for "HDCP1_TX_SEND_RANDOM_AN" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void set_hdcp1_random_an_handler(const MailboxData_t *mailboxData) {
    ENG1T_loadDebugAn(mailboxData->message);
}

/**
 *  Handler for "HDCP_TRAN_STATUS_CHANGE" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void status_change_handler(const MailboxData_t *mailboxData) {
    uint8_t *txBuff = MB_GetTxBuff(MB_TYPE_SECURE);

    txBuff[0] = GetByte1(hdcpGenData.status);
    txBuff[1] = GetByte0(hdcpGenData.status);

    MB_SendMsg(MB_TYPE_SECURE, HDCP_STATUS_CHANGE_RESP_SIZE, mailboxData->opCode, MB_MODULE_ID_HDCP);
}

/**
 *  Handler for "HDCP2X_TX_IS_KM_STORED" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void is_km_stored_handler(const MailboxData_t *mailboxData) {
    uint8_t *txBuff = MB_GetTxBuff(MB_TYPE_SECURE);
    ENG2T_GetReceiverId(txBuff);
    MB_SendMsg(MB_TYPE_SECURE, HDCP2X_IS_KM_STORED_RESP_SIZE, mailboxData->opCode, MB_MODULE_ID_HDCP);
}

/**
 *  Handler for "HDCP2X_TX_STORE_KM" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void store_km_handler(const MailboxData_t *mailboxData) {
    uint8_t *txBuff = MB_GetTxBuff(MB_TYPE_SECURE);
    HDCP2X_getPairingData(txBuff);
    MB_SendMsg(MB_TYPE_SECURE, HDCP2X_PAIRING_DATA_SIZE, mailboxData->opCode, MB_MODULE_ID_HDCP);
}

/**
 *  Handler for "HDCP_TRAN_IS_REC_ID_VALID" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void is_receiver_id_valid_handler(const MailboxData_t *mailboxData) {
    uint8_t *txBuff = MB_GetTxBuff(MB_TYPE_SECURE);
    CPS_BufferCopy(txBuff, &hdcpGenData.rid.command[0], hdcpGenData.rid.size);
    MB_SendMsg(MB_TYPE_SECURE, hdcpGenData.rid.size, mailboxData->opCode, MB_MODULE_ID_HDCP);
}

/**
 *  Handler for "HDCP2X_TX_RESPOND_KM" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void respond_km_handler(const MailboxData_t *mailboxData) {
    /* Length of message is 0U for 'No Stored' or 'HDCP2X_STORE_KM_RESP_SIZE (53U)' for stored, so value could be casted */
    setMsgReady((uint8_t)mailboxData->length);

    /* If empty message was sent, no-stored km is used */
    if (mailboxData->length != 0U) {
        ENG2T_set_AKE_Stored_km(hdcpGenData.hdcpBuffer, mailboxData->message);
    }
}

/**
 *  Handler for "HDCP_TRAN_RESPOND_RECEIVER_ID_VALID" message
 *  @param[in] mailboxData, structure with data received via mailbox
 */
static void respond_rec_id_valid_handler(const MailboxData_t *mailboxData) {
    setMsgReady(mailboxData->message[0] & (uint8_t)HDCP_MSG_IS_REC_ID_VALID_MASK);
}

/**
 * For HDCP module, only messages via Secure-APB are allowed. If message was send via APB,
 * FW should respond with message filled with zeros to avoid blockage of driver.
 */
static void catchInvalidBusMsg(void) {
    MailboxData_t mailboxData;
    uint8_t responseSize = 0U;
    uint8_t *opCode;
    uint8_t *txBuff;

    /* Read message */
    MB_getCurMessage(MB_TYPE_REGULAR, &mailboxData.message, &mailboxData.opCode, &mailboxData.length);
    MB_FinishReadMsg(MB_TYPE_REGULAR);

    opCode = &mailboxData.opCode;
    txBuff = MB_GetTxBuff(MB_TYPE_REGULAR);

    if (*opCode == (uint8_t)HDCP_TRAN_STATUS_CHANGE) {
        responseSize = (uint8_t)HDCP_STATUS_CHANGE_RESP_SIZE;
    } else if (*opCode == (uint8_t)HDCP2X_TX_STORE_KM) {
        responseSize = (uint8_t)HDCP2X_STORE_KM_RESP_SIZE;
    } else if (*opCode == (uint8_t)HDCP2X_TX_IS_KM_STORED) {
        responseSize = (uint8_t)HDCP2X_IS_KM_STORED_RESP_SIZE;
    } else if (*opCode == (uint8_t)HDCP_TRAN_IS_REC_ID_VALID) {
        responseSize = (uint8_t)HDCP_IS_REC_ID_VALID_RESP_SIZE;
    } else {
        /* If messageId is invalid or response is not needed - ignore message */
        responseSize = 0U;
    }

    /* If response required - will send it */
    if (responseSize != 0U) {
        (void)memset(txBuff, 0, responseSize);
        MB_SendMsg(MB_TYPE_REGULAR, responseSize, *opCode, MB_MODULE_ID_HDCP);
    }
}

static void managementMsgHandler(void) {
    MailboxData_t mailboxData;

    MB_getCurMessage(MB_TYPE_SECURE, &mailboxData.message, &mailboxData.opCode, &mailboxData.length);

    if (mailboxData.opCode == (uint8_t)HDCP_GENERAL_SET_LC_128) {
        CPS_BufferCopy(pHdcpLc128, mailboxData.message, LC_128_LEN);
    } else if (mailboxData.opCode == (uint8_t)HDCP_SET_SEED) {
        // TODO DK: move (function) from utils to hdcp-related files.
        // The way PRNG seed is being created does not matter exactly. It changes when message
        // changes, though.
        uint32_t prng_seed[8] = {
            getLe32(mailboxData.message),      getLe32(&mailboxData.message[4]),
            getLe32(&mailboxData.message[8]),  getLe32(&mailboxData.message[12]),
            getLe32(&mailboxData.message[16]), getLe32(&mailboxData.message[20]),
            getLe32(&mailboxData.message[24]), getLe32(&mailboxData.message[28])};
        UTIL_PRNG_SetSeed(prng_seed);
    } else {
        /**
         * All 'if ... else if' constructs shall be terminated with an 'else' statement [MISRA2012-RULE-15_7-2]
         */
    }

    MB_FinishReadMsg(MB_TYPE_SECURE);
}

static void deviceMessagesHandler(void) {
    MailboxData_t mailboxData;

    static HdcpMailboxMsgHandler_t handlers[] = {
        /* "HDCP_TRAN_CONFIGURATION" message handler (0) */
        &config_handler,
        /* "HDCP2X_TX_SET_PUBLIC_KEY_PARAMS" message handler (1) */
        &set_public_keys_params_handler,
        /* "HDCP2X_TX_SET_DEBUG_RANDOM_NUMBERS" message handler (2) */
        &set_debug_random_handler,
        /* "HDCP2X_TX_RESPOND_KM" message handler (3) */
        &respond_km_handler,
        /* "HDCP1_TX_SEND_KEYS" message handler (4) */
        &set_hdcp1_keys_handler,
        /* "HDCP1_TX_SEND_RANDOM_AN" message handler (5) */
        &set_hdcp1_random_an_handler,
        /* "HDCP_TRAN_STATUS_CHANGE" message handler (6) */
        &status_change_handler,
        /* "HDCP2X_TX_IS_KM_STORED" message handler (7) */
        &is_km_stored_handler,
        /* "HDCP2X_TX_STORE_KM" message handler (8) */
        &store_km_handler,
        /* "HDCP_TRAN_IS_REC_ID_VALID" message handler (9) */
        &is_receiver_id_valid_handler,
        /* "HDCP_TRAN_RESPOND_RECEIVER_ID_VALID" message handler (10) */
        &respond_rec_id_valid_handler,
        /* "HDCP_TRAN_TEST_KEYS" message handler - unused (11) */
        NULL,
        /* "HDCP2X_TX_SET_KM_KEY_PARAMS" message handler (12) */
        &set_custom_km_enc_handler};

    MB_getCurMessage(MB_TYPE_SECURE, &mailboxData.message, &mailboxData.opCode, &mailboxData.length);

    /* Check if msgId is supported by controller. If is call handler, if not - ignore message */
    if ((mailboxData.opCode < (uint8_t)HDCP_NUM_OF_SUPPORTED_MESSAGES) && (handlers[mailboxData.opCode] != NULL)) {
        handlers[mailboxData.opCode](&mailboxData);
    }

    MB_FinishReadMsg(MB_TYPE_SECURE);
}

static void handle_hdcp_message(void) {
    /* Respond with 0's for all commands sent via APB */
    if (MB_isWaitingModuleMessage(MB_TYPE_REGULAR, MB_MODULE_ID_HDCP)) {
        catchInvalidBusMsg();
    }

    /* Read messages received via Secure-APB and send response if required */
    if (MB_isWaitingModuleMessage(MB_TYPE_SECURE, MB_MODULE_ID_HDCP)) {
        deviceMessagesHandler();
    }

    /* Read messages sent into HDCP_GENERAL module */
    if (MB_isWaitingModuleMessage(MB_TYPE_SECURE, MB_MODULE_ID_HDCP_GENERAL)) {
        managementMsgHandler();
    }
}

/**
 * Used to check if Host processor reads error code. Used to avoid lost of error
 * (Error occured, but HDCP SM restarted and reseted status)
 * @return 'true' if error occured and was read by host, or if error not occured,
 *          otherwise 'false'
 */
static bool ifHostReadsError(void) {

    if (hdcpGenData.errorUpdate) {
        if ((RegRead(XT_EVENTS0) & (uint32_t)EVENT_ID_HDCPTX_STATUS) == 0U) {
            hdcpGenData.errorUpdate = false;
        }
    }

    return !hdcpGenData.errorUpdate;
}

/**
 * Function used to initialize HDCP module. It is static, but is called from
 * another module by pointer to function.
 */
static void HDCP_TRAN_init(void) {
    /* XXX mbeberok: fastDelays cannot be reset there, because this option
     * is enabled/disabled by GENERAL_MAIN_CONTROL message before initialization
     * of HDCP module */

    hdcpGenData.stateCb = &waitForConfigCb;
    hdcpGenData.statusUpdate = false;
    hdcpGenData.hpdPulseIrq = false;

    /* No error and any info about status */
    hdcpGenData.status = 0U;

    /* If HDCP is used, bypass has to be disabled */
    RegWrite(HDCP_DP_CONFIG, 0U);
}

/**
 * Do current task if possible
 */
static void doThread(void) {
    /* Call function connected with state */
    if ((ifHostReadsError()) && (hdcpGenData.stateCb != NULL)) {
        hdcpGenData.stateCb();
    }
}

/**
 * Main thread of HDCP module. It is static, but is called from
 * another module by pointer to function.
 */
static void HDCP_TRAN_thread(void) {
    /* Check if any message is pending and handle if is */
    handle_hdcp_message();

    /* Check if device was down */
    if (isHpdDown()) {
        catchError(HDCP_ERR_HPD_DOWN);
    } else if (CHANNEL_MASTER_isErrorOccurred()) {
        catchError(HDCP_ERR_DDC_ERROR);
    } else if (modRunnerIsTimeoutExpired()) {
        catchError(HDCP_ERR_WATCHDOG_EXPIRED);
    } else {
        doThread();
    }

    if (hdcpGenData.statusUpdate) {
        notifyHostAboutStatusChange();
    }
}

/**
 * Function used to start HDCP module. It is static, but is called from
 * another module by pointer to function.
 */
static void HDCP_TRAN_start(void) {
    modRunnerWakeMe();
}

void HDCP_TRAN_InsertModule(void) {
    static Module_t hdcpTxModule;

    /* Assign module functions into pointers */
    hdcpTxModule.initTask = &HDCP_TRAN_init;
    hdcpTxModule.startTask = &HDCP_TRAN_start;
    hdcpTxModule.thread = &HDCP_TRAN_thread;

    hdcpTxModule.moduleId = MODRUNNER_MODULE_HDCP_TX;

    /* Set priority of module */
    hdcpTxModule.pPriority = 0U;

    /* Attach module to system */
    modRunnerInsertModule(&hdcpTxModule);
}

/**
 * Set status bits for HDCP and set flag to inform SM about sending event to host.
 * Error takes precedence over status (because setting error clean status bits)
 * so as long as host will not be informed about error status bits cannot be set.
 */
void HDCP_TRAN_setStatus(uint16_t status) {
    /* Notify about status change only when it is actually done, not for each function call */
    if (((status ^ hdcpGenData.status) & (uint16_t)HDCP_STATUS_INFO_BITS_MASK) != 0U) {
        hdcpGenData.statusUpdate = true;
        hdcpGenData.status &= (uint16_t)HDCP_STATUS_ERROR_TYPE_MASK;
        hdcpGenData.status |= status & ((uint16_t)HDCP_STATUS_INFO_BITS_MASK);
    }
}

void HDCP_TRAN_setError(HdcpTransactionError_t errorVal) {
    uint16_t shiftedError = (uint16_t)safe_shift32l((uint8_t)errorVal, HDCP_STATUS_ERROR_TYPE_OFFSET);

    /* Notify about error only when it is actually occur, not for each function call */
    if (((shiftedError ^ hdcpGenData.status) & (uint16_t)HDCP_STATUS_ERROR_TYPE_MASK) != 0U) {

        /* Notify module about change */
        hdcpGenData.statusUpdate = true;
        hdcpGenData.errorUpdate = true;

        /* Set error code */
        hdcpGenData.status &= (uint16_t)HDCP_STATUS_INFO_BITS_MASK;
        hdcpGenData.status |= shiftedError;
    }
}

void HDCP_TRAN_setFastDelays(bool enable) {
    /* Enable/disable fast delays option */
    hdcpGenData.fastDelays = enable;
}

void HDCP_TRAN_sleep(uint32_t us, uint32_t us_fast) {
    /* Put module to sleep */
    modRunnerSleep(hdcpGenData.fastDelays ? us_fast : us);
}

void HDCP_TRAN_initOnReset(void) {
    /* Clean status of HPD */
    hpdState = 0U;
}

uint8_t *HDCP_TRAN_getBuffer(void) {
    /* Return address of HDCP buffer */
    return &(hdcpGenData.hdcpBuffer[0]);
}

void HDCP_setReceiverIdList(uint8_t *list, uint8_t devCount, uint16_t receiverInfo, HdcpVer_t hdcpVer) {
    /* Receiver IDs list */
    uint8_t *ridList = hdcpGenData.rid.command;
    uint16_t index;

    ridList[HDCP_RID_LIST_DEV_COUNT_OFFSET] = devCount;

    /* XXX mbeberok: list[1] is not used in driver */

    index = devCount * (uint16_t)HDCP_REC_ID_SIZE;
    CPS_BufferCopy(&ridList[HDCP_RID_LIST_ID_OFFSET], list, index);

    /* Append receiver data */
    if (hdcpVer == HDCP_VERSION_1X) {
        /* In HDCP v1.x data are stored in little-endian order */
        ridList[index + 2U] = GetByte1(receiverInfo);
        ridList[index + 3U] = GetByte0(receiverInfo);
    } else {
        /* In HDCP v2.x data are stored in big-endian order */
        ridList[index + 2U] = GetByte0(receiverInfo);
        ridList[index + 3U] = GetByte1(receiverInfo);
    }
}

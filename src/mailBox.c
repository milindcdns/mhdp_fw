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
 * mailBox.c
 *
 ******************************************************************************
 */

#include "cdn_stdint.h"
#include "cdn_stdtypes.h"
#include "mailBox.h"
#include "modRunner.h"
#include "string.h"
#include "hdcp2.h"
#include "mhdp_apb_regs.h"
#include "cps_drv.h"
#include "timer.h"
#include "utils.h"
#include "reg.h"

#define CLEAR_PREV_VAL 0

/** Check if mailbox is full */
static inline bool isMailBoxFull(MB_TYPE type) {
    bool answer = false; // there is only one port, value is 1-bit, need to initialize
    switch (type) {
        case MB_TYPE_REGULAR:
            answer = (RegFieldRead(MAILBOX_FULL, MAILBOX_FULL, RegRead(MAILBOX_FULL)) != 0U);
            break;
        case MB_TYPE_SECURE:
            answer = (RegFieldRead(SMAILBOX_FULL, SMAILBOX_FULL, RegRead(SMAILBOX_FULL)) != 0U);
            break;
        default:
            // no action, all cases covered
            break;
    }
    return answer;
}

/** Check if mailbox is empty */
static inline bool isMailBoxEmpty(MB_TYPE type) {
    bool answer = false; // there is only one port, value is 1-bit, need to initialize
    switch (type) {
        case MB_TYPE_REGULAR:
            answer = (RegFieldRead(MAILBOX_EMPTY, MAILBOX_EMPTY, RegRead(MAILBOX_EMPTY)) != 0U);
            break;
        case MB_TYPE_SECURE:
            answer = (RegFieldRead(SMAILBOX_EMPTY, SMAILBOX_EMPTY, RegRead(SMAILBOX_EMPTY)) != 0U);
            break;
        default:
            // no action, all cases covered
            break;
    }
    return answer;
}

/** Get the mailbox rd data from MAILBOX_RD_DATA_p/SMAILBOX_RD_DATA_p registers */
inline static uint8_t getMailBoxRdData(MB_TYPE type) {
    uint8_t rd_data = 0; // value is 8-bit, need to initialize
    switch (type) {
        case MB_TYPE_REGULAR:
            rd_data = (uint8_t) RegFieldRead(MAILBOX_RD_DATA, MAILBOX_RD_DATA,
                    RegRead(MAILBOX_RD_DATA));
            break;
        case MB_TYPE_SECURE:
            rd_data = (uint8_t) RegFieldRead(SMAILBOX_RD_DATA, SMAILBOX_RD_DATA,
                    RegRead(SMAILBOX_RD_DATA));
            break;
        default:
            // no action, all cases covered
            break;
    }
    return rd_data;
}

/** Write the mailbox wr data to MAILBOX_WR_DATA_p/SMAILBOX_WR_DATA_p registers*/
inline static void writeMailBoxWrData(MB_TYPE type, uint8_t wr_data) {
    switch (type) {
        case MB_TYPE_REGULAR:
            // We overwrite whole area available to write, no need to read other register fields
            RegWrite(MAILBOX_WR_DATA,
                    RegFieldWrite(MAILBOX_WR_DATA, MAILBOX_WR_DATA, CLEAR_PREV_VAL, wr_data));
            break;
        case MB_TYPE_SECURE:
            RegWrite(SMAILBOX_WR_DATA,
                    RegFieldWrite(SMAILBOX_WR_DATA, SMAILBOX_WR_DATA, CLEAR_PREV_VAL, wr_data));
            break;
        default:
            // no action, all cases covered
            break;
    }
}

static S_MAIL_BOX_DATA mailBoxData[MB_TYPE_COUNT];

/** Get address of Tx buffer */
uint8_t* MB_GetTxBuff(MB_TYPE type) {
    return &mailBoxData[(uint8_t)type].txBuff[MB_TXRXBUFF_DATA_IDX];
}

/** Check if Tx is ready */
bool MB_IsTxReady(MB_TYPE type) {
    return !mailBoxData[(uint8_t)type].portTxBusy;
}

/** Send message */
void MB_SendMsg(MB_TYPE type, uint32_t len, uint8_t opCode, MB_MODULE_ID moduleId) {
    // tx buffer (message) has couple of fields
    mailBoxData[type].txBuff[MB_TXRXBUFF_OPCODE_IDX] = opCode;
    mailBoxData[type].txBuff[MB_TXRXBUFF_MODULE_ID_IDX] = (uint8_t) moduleId;
    mailBoxData[type].txBuff[MB_TXRXBUFF_SIZE_MSB_IDX] = GetByte1(len);
    mailBoxData[type].txBuff[MB_TXRXBUFF_SIZE_LSB_IDX] = GetByte0(len);
    mailBoxData[type].txCur = 0U;
    mailBoxData[type].txTotal = len + (uint8_t) MB_TXRXBUFF_DATA_IDX;
    mailBoxData[type].portTxBusy = true;
}

/** Initialize Regular mail box module */
static void MB_Init_Regular(void) {
    mailBoxData[(uint8_t) MB_TYPE_REGULAR].rxState = MB_STATE_EMPTY;
    mailBoxData[(uint8_t) MB_TYPE_REGULAR].portTxBusy = false;

}

/** Initialize Secure mail box module */
static void MB_Init_Secure(void) {
    mailBoxData[(uint8_t) MB_TYPE_SECURE].rxState = MB_STATE_EMPTY;
    mailBoxData[(uint8_t) MB_TYPE_SECURE].portTxBusy = false;
}

/** Start to run mailbox thread */
static void MB_Start(void) {
    // start to run my thread, in real mail box that wont be needed (interrupt will do it)
    modRunnerWakeMe();
}

/** Mailbox thread receive function */
static void MB_ThreadTx(MB_TYPE type) {
    S_MAIL_BOX_DATA *mailBoxDataPtr = &mailBoxData[(uint8_t) type];

    // while there is anything to send
    while (mailBoxDataPtr->portTxBusy && (!isMailBoxFull(type))) {
        writeMailBoxWrData(type, mailBoxDataPtr->txBuff[mailBoxDataPtr->txCur]);
        mailBoxDataPtr->txCur++;
        if (mailBoxDataPtr->txCur == mailBoxDataPtr->txTotal) {
            // finish TX
            mailBoxDataPtr->portTxBusy = false;
        }
    }
}

/** Mailbox thread receive function */
static void MB_ThreadRx(MB_TYPE type) {
    S_MAIL_BOX_DATA *mailBoxDataPtr = &mailBoxData[(uint8_t)type];
    uint8_t mailBoxRdData;

    // every iteration the empty mailbox status is being checked.
    while ((!isMailBoxEmpty(type)) && (mailBoxDataPtr->rxState != MB_STATE_MSG_READY)) { // while there is something to read
        mailBoxRdData = getMailBoxRdData(type);
        switch (mailBoxDataPtr->rxState) {
            case MB_STATE_EMPTY:
                mailBoxDataPtr->rxBuff[MB_TXRXBUFF_OPCODE_IDX] = mailBoxRdData;
                mailBoxDataPtr->rxState = MB_STATE_WAIT_MODULE_ID;
                break;

            case MB_STATE_WAIT_MODULE_ID:
                mailBoxDataPtr->rxBuff[MB_TXRXBUFF_MODULE_ID_IDX] = mailBoxRdData;
                mailBoxDataPtr->rxState = MB_STATE_WAIT_SIZE_MSB;
                break;

            case MB_STATE_WAIT_SIZE_MSB:
                mailBoxDataPtr->rxBuff[MB_TXRXBUFF_SIZE_MSB_IDX] = mailBoxRdData;
                mailBoxDataPtr->rxState = MB_STATE_WAIT_SIZE_LSB;
                break;

            case MB_STATE_WAIT_SIZE_LSB:
                mailBoxDataPtr->rxBuff[MB_TXRXBUFF_SIZE_LSB_IDX] = mailBoxRdData;
                mailBoxDataPtr->rx_final_msgSize =
                        ((uint32_t) mailBoxDataPtr->rxBuff[MB_TXRXBUFF_SIZE_MSB_IDX] << 8U)
                                + (uint32_t) mailBoxDataPtr->rxBuff[MB_TXRXBUFF_SIZE_LSB_IDX];
                mailBoxDataPtr->rx_data_idx = 0U;

                mailBoxDataPtr->rxState =
                        (mailBoxDataPtr->rx_final_msgSize == 0U) ?
                                MB_STATE_MSG_READY : MB_STATE_READ_DATA;
                break;

            case MB_STATE_READ_DATA:
                mailBoxDataPtr->rxBuff[mailBoxDataPtr->rx_data_idx + (uint8_t) MB_TXRXBUFF_DATA_IDX] =
                        mailBoxRdData;
                mailBoxDataPtr->rx_data_idx++;

                if (mailBoxDataPtr->rx_data_idx == mailBoxDataPtr->rx_final_msgSize) {
                    mailBoxDataPtr->rxState = MB_STATE_MSG_READY;
                }
                break;

            case MB_STATE_MSG_READY:
            default: // (all cases covered, default not needed)
                // don't do anything, we still didn't handle last command
                break;
        }
    }
}

/** Regular Mail box thread handler */
static void MB_Thread_Regular(void) {
    MB_ThreadTx(MB_TYPE_REGULAR);
    MB_ThreadRx(MB_TYPE_REGULAR);
}

/** Secure Mail box thread handler */
static void MB_Thread_Secure(void) {
    MB_ThreadTx(MB_TYPE_SECURE);
    MB_ThreadRx(MB_TYPE_SECURE);
}


/** Check if there is waiting message for specified module */
bool MB_isWaitingModuleMessage(MB_TYPE type, MB_MODULE_ID moduleId) {
    bool answer;
    if (mailBoxData[type].rxState == MB_STATE_MSG_READY) {
        uint8_t rxModuleId = mailBoxData[(uint8_t)type].rxBuff[MB_TXRXBUFF_MODULE_ID_IDX];
        if (rxModuleId == (uint8_t) moduleId) {
            answer = true;
        } else {
            answer = false;
        }
    } else {
        answer = false;
    }
    return answer;
}

/** Get current message */
void MB_getCurMessage(MB_TYPE type, uint8_t **message, uint8_t *opCode,
        uint16_t *msgLen) {
    *message = &mailBoxData[(uint8_t)type].rxBuff[MB_TXRXBUFF_DATA_IDX];
    *opCode = mailBoxData[(uint8_t)type].rxBuff[MB_TXRXBUFF_OPCODE_IDX];
    *msgLen = ((uint16_t)mailBoxData[(uint8_t)type].rxBuff[MB_TXRXBUFF_SIZE_MSB_IDX] << 8U)
                            | (mailBoxData[(uint8_t)type].rxBuff[MB_TXRXBUFF_SIZE_LSB_IDX]);
}

/** Finish reading the message */
void MB_FinishReadMsg(MB_TYPE type) {
    mailBoxData[type].rxState = MB_STATE_EMPTY;
}

/** Insert new mail box module */
void MB_InsertModule(void) {
    static Module_t mailModule;

    /* Set parameters specific per module */
    mailModule.initTask = &MB_Init_Regular;
    mailModule.startTask = &MB_Start;
    mailModule.thread = &MB_Thread_Regular;
    mailModule.moduleId = MODRUNNER_MODULE_MAIL_BOX;

    modRunnerInsertModule(&mailModule);
}

/** Insert new secure mail box module */
void MB_Secure_InsertModule(void) {
    static Module_t secureMailModule;

    /* Set parameters specific per module */
    secureMailModule.initTask = &MB_Init_Secure;
    secureMailModule.startTask = &MB_Start;
    secureMailModule.thread = &MB_Thread_Secure;
    secureMailModule.moduleId = MODRUNNER_MODULE_SECURE_MAIL_BOX;

    modRunnerInsertModule(&secureMailModule);
}

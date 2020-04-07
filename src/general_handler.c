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
 * general_handler.c
 *
 ******************************************************************************
 */

// parasoft-suppress METRICS-36-3 "A function should not be called from more than 5 different functions" DRV-3823

#include "general_handler.h"
#include "mailBox.h"
#include <stdio.h>
#include <string.h>
#include "hdcp_tran.h"
#include "cipher_handler.h"
#include "watchdog.h"
#include "reg.h"
#include "timer.h"
#include "controlChannelM.h"
#include "dp_tx_mail_handler.h"
#include "dp_tx.h"
#include "xtUtils.h"
#include "utils.h"

#include "apbChecker.h"
#include "mode.h"

/* Pointer to request handlers */
typedef void (*General_handler_req_handler_t)(uint8_t message[], uint16_t len, MB_TYPE type);

// Base addresses of UCPU, CRYPTO, CIPHER, DPTX_HDCP register sections
#define ADDR_UCPU_CFG (uint32_t)(&mhdpRegBase->mhdp_apb_regs.VER_DAY_p)
#define ADDR_CRYPTO (uint32_t)(&mhdpRegBase->mhdp_apb_regs.CRYPTO_HDCP_REVISION_p)
#define ADDR_CIPHER (uint32_t)(&mhdpRegBase->mhdp_apb_regs.HDCP_REVISION_p)
#define ADDR_DPTX_HDCP (uint32_t)(&mhdpRegBase->mhdp_apb_regs.HDCP_DP_STATUS_p)

#define REG_BANK_SIZE 256U // Bytes

// parasoft-begin-suppress MISRA2012-RULE-11_4 "A conversion should not be performed between a pointer to object and an integer type, DRV-4620"
bool is_mb_access_permitted(const uint32_t *addr, bool via_sapb) {
    // ranges of addresses that are blocked for apb
    uint32_t apb_blocked[][2] = {
        { ADDR_UCPU_CFG, (ADDR_UCPU_CFG + REG_BANK_SIZE) },
        { ADDR_CRYPTO, (ADDR_CRYPTO + REG_BANK_SIZE) },
        { ADDR_CIPHER, (ADDR_CIPHER + REG_BANK_SIZE) },
        { ADDR_DPTX_HDCP, (ADDR_DPTX_HDCP + REG_BANK_SIZE) },
    };
    // ranges of addresses that are blocked for sapb
    uint32_t sapb_blocked[][2] = {
        { ADDR_UCPU_CFG, (ADDR_UCPU_CFG + REG_BANK_SIZE) },
    };

    static const uint8_t apb_list_size = (uint8_t) (sizeof(apb_blocked) / sizeof(apb_blocked[0]));
    static const uint8_t sapb_list_size = (uint8_t) (sizeof(sapb_blocked) / sizeof(sapb_blocked[0]));

    uint8_t i;
    bool answer = true; // Permit access to any addresses other than cases below

    if (via_sapb) {
        for (i = 0U; i < sapb_list_size; ++i) {
            if (((uint32_t) addr >= sapb_blocked[i][0]) && ((uint32_t) addr < sapb_blocked[i][1])) {
                answer = false;
                break;
            }
        }
    } else {
        for (i = 0U; i < apb_list_size; ++i) {
            if (((uint32_t) addr >= apb_blocked[i][0]) && ((uint32_t) addr < apb_blocked[i][1])) {
                answer = false;
                break;
            }
        }
    }
    return answer;
}
// parasoft-end-suppress MISRA2012-RULE-11_4


DpMode_t dpMode = DISPLAYPORT_FIRMWARE_STANDBY;
extern uint8_t g_hpd_state;

static S_GENERAL_HANDLER_DATA generalHandlerData;

/** Set up source_dptx_car, source_pkt_car registers.
 * This is part od startAll function's procedure. */
static void startAllSetUpSourceRegisters1(void) {
    uint32_t reg = MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_SYS_CLK_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_SYS_CLK_RSTN_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__SOURCE_AUX_SYS_CLK_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__SOURCE_AUX_SYS_CLK_RSTN_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_PHY_CHAR_CLK_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_PHY_CHAR_RSTN_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_PHY_DATA_CLK_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_PHY_DATA_RSTN_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_FRMR_DATA_CLK_EN_MASK
         | MHDP__MHDP_APB_REGS__SOURCE_DPTX_CAR_P__DPTX_FRMR_DATA_CLK_RSTN_EN_MASK;
         // other fields are 0

    RegWrite(source_dptx_car, reg);

    // For source_pkt_car we use similar data as for source_dptx_car for which we used reg
    // That is why we use reg's value here
    RegWrite(source_pkt_car,
             (RegFieldWrite(SOURCE_PKT_CAR, SOURCE_PKT_SYS_CLK_EN, reg, 1)
             | RegFieldWrite(SOURCE_PKT_CAR, SOURCE_PKT_SYS_RSTN_EN, 0, 1)));
}

/** Set up source_phy_car, source_aif_car, source_cbus_car,
 * source_cipher_car, source_crypto_car registers.
 * This is part od startAll function's procedure. */
static void startAllSetUpSourceRegisters2(void) {
    // These registers are filled from scratch - no use of reg
    RegWrite(source_phy_car, 0xFFU); // 0x0003));
    RegWrite(source_aif_car,
             RegFieldWrite(SOURCE_AIF_CAR, SOURCE_AIF_SYS_CLK_EN, 0, 1)
             | RegFieldWrite(SOURCE_AIF_CAR, SOURCE_AIF_SYS_RSTN_EN, 0, 1));
    RegWrite(source_cbus_car, 0xFF);//0x0003));
    RegWrite(source_cipher_car, 0);
    RegWrite(source_crypto_car, 0);
}

static void startAll(void) {
    // Read uCPU frequency and set up internal timing parameters
    updateClkFreq();
    // set up source_*_car registers
    startAllSetUpSourceRegisters1();
    startAllSetUpSourceRegisters2();

#ifdef WITH_CIPHER
    CIPHER_ClearAuthenticated();
    HDCP_TRAN_initOnReset();
#endif

    DP_TX_hdpInit();
    CHANNEL_MASTER_init();
}

/** Set standby mode */
static void gen_handler_set_standby_mode(void) {

    dpMode = DISPLAYPORT_FIRMWARE_STANDBY;
    /* Remove non-essential modules */
    for (int16_t id = (int16_t) MODRUNNER_MODULE_LAST - 1; id >= 0; --id) {
        switch (id) {
            case (int16_t) MODRUNNER_MODULE_GENERAL_HANDLER:
            case (int16_t) MODRUNNER_MODULE_SECURE_MAIL_BOX:
            case (int16_t) MODRUNNER_MODULE_MAIL_BOX:
                break;
            default:
                // every other module is removed (id is also checked later)
                modRunnerRemoveModule((uint8_t) id);
                break;
        }
    }

    RegWrite(source_dptx_car,
        RegFieldWrite(SOURCE_DPTX_CAR, DPTX_SYS_CLK_EN, 0, 1) |
        RegFieldWrite(SOURCE_DPTX_CAR, DPTX_SYS_CLK_RSTN_EN, 0, 1) |
        RegFieldWrite(SOURCE_DPTX_CAR, SOURCE_AUX_SYS_CLK_EN, 0, 1) |
        RegFieldWrite(SOURCE_DPTX_CAR, SOURCE_AUX_SYS_CLK_RSTN_EN, 1, 1));

    RegWrite(source_phy_car, 0);
    RegWrite(source_pkt_car, 0);
    RegWrite(source_aif_car, 0);
    RegWrite(source_cbus_car, 0);

    RegWrite(source_cipher_car, 0);
    RegWrite(source_crypto_car, 0);
}

/** Set active mode */
static void general_handler_set_active_mode(void) {
    dpMode = DISPLAYPORT_FIRMWARE_ACTIVE;
    startAll();

#ifdef WITH_CIPHER
    HDCP_TRAN_InsertModule();
#endif

#ifdef USE_TEST_MODULE
    TM_InsertModule();
#endif // USE_TEST_MODULE
    DP_TX_InsertModule();
    DP_TX_MAIL_HANDLER_InsertModule();
}

/** Initialize general handler module */
static void GENERAL_handler_init(void) {
    // nothing to do, function is for modules' unification reason
    generalHandlerData.delay = 0U;
}

/** Start general handler module */
static inline void GENERAL_handler_start(void) {
    modRunnerWakeMe();
}

/**
 * Handler for GENERAL_MAIN_CONTROL request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void main_control_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint8_t* response_buffer;
    bool isActive = isActiveMode();
    response_buffer = MB_GetTxBuff(type);
    response_buffer[0] = 0U;

#if defined(WITH_CIPHER)
    /*
     * FIXME DK: temporary workaround until hdcp_tran_set_fast_delays is moved to
     * HDCP generic place and not TX only.
     */
    if ((message[0] & (uint8_t) GEN_MAINCTRL_SET_FAST_HDCP_DELAYS_MASK) != 0U) {
        HDCP_TRAN_setFastDelays(true);
        response_buffer[0] |= (uint8_t) GEN_MAINCTRL_SET_FAST_HDCP_DELAYS_MASK;
    } else {
        HDCP_TRAN_setFastDelays(false);
    }
#endif

    if (((message[0] & (uint8_t) GEN_MAINCTRL_SET_ACTIVE_BIT_MASK) != 0U)
            && (!isActive)) {
        general_handler_set_active_mode();
    } else if (((message[0] & (uint8_t) GEN_MAINCTRL_SET_ACTIVE_BIT_MASK) == 0U)
            && isActive) {
        gen_handler_set_standby_mode();
    } else {
        // no action
    }

    /* Need be recalled, cos value could be changed in above if-else statement */
    isActive = isActiveMode();
    if (isActive) {
        response_buffer[0] |= (uint8_t) GEN_MAINCTRL_SET_ACTIVE_BIT_MASK;
    }

    MB_SendMsg(type, len, (uint8_t) GENERAL_MAIN_CONTROL, MB_MODULE_ID_GENERAL);
}

// parasoft-begin-suppress MISRA2012-RULE-2_7-4 "parameter not used in function" DRV-4608

/**
 * Handler for GENERAL_TEST_ECHO request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void test_echo_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint8_t* response_buffer;
    response_buffer = MB_GetTxBuff(type);
    (void) memcpy(&response_buffer[0], &message[0], len);
    MB_SendMsg(type, len, (uint8_t) GENERAL_TEST_ECHO, MB_MODULE_ID_GENERAL);
}

/**
 * Handler for GENERAL_WRITE_REGISTER request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void write_register_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint32_t *addr;
    addr = uintToPointer(getBe32(message));

    if (is_mb_access_permitted(addr, (type != MB_TYPE_REGULAR))) {
        *addr = getBe32(&message[4]);
    }
}

/**
 * Handler for GENERAL_WRITE_FIELD request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void write_field_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint32_t *addr;
    uint32_t mask;
    addr = uintToPointer(getBe32(message));

    switch (type) {
        case MB_TYPE_REGULAR:
            if (is_mb_access_permitted(addr, false)) {
                // intentionally shift left and cut some most significant bits
                mask = safe_shift32(LEFT, 0xFFFFFFFFU, 32U - message[5]);
                // 2nd operation on mask - shift right
                mask = safe_shift32(RIGHT, mask, 32U - message[4] - message[5]);
                *addr = (getBe32(&message[6]) & mask) | (*addr & ~mask);
            }
            break;
        case MB_TYPE_SECURE:
            if (is_mb_access_permitted(addr, true)) {
                // intentionally shift left and cut some most significant bits
                mask = safe_shift32(LEFT, 0xFFFFFFFFU, 32U - message[3]);
                // 2nd operation on mask - shift right
                mask = safe_shift32(RIGHT, mask, 31U - message[2]);
                *addr = ((getBe32(&message[4])) & mask) | (*addr & ~mask);
            }
            break;
        default:
            // no action, all cases covered
            break;
    }
}

/**
 * Handler for GENERAL_READ_REGISTER request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void read_register_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint32_t *addr;
    uint32_t retVal;
    uint8_t* response_buffer;
    response_buffer = MB_GetTxBuff(type);
    // get address from message
    addr = uintToPointer(getBe32(message));

    if (is_mb_access_permitted(addr, (type != MB_TYPE_REGULAR))) {
        retVal = *addr;
        response_buffer[0] = message[0];
        response_buffer[1] = message[1];
        response_buffer[2] = message[2];
        response_buffer[3] = message[3];
        setBe32(retVal, &response_buffer[4]);
    } else {
        // set zeros to response_buffer[0...7]
        setBe32(0U, &response_buffer[0]);
        setBe32(0U, &response_buffer[4]);
    }

    MB_SendMsg(type, 8, (uint8_t) GENERAL_READ_REGISTER_RESP, MB_MODULE_ID_GENERAL);
}

/**
 * Handler for GENERAL_GET_HPD_STATE request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void hpd_state_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    MB_GetTxBuff(type)[0] = g_hpd_state;
    MB_SendMsg(type, 1, (uint8_t) GENERAL_GET_HPD_STATE, MB_MODULE_ID_GENERAL);
}

/**
 * Handler for GENERAL_WAIT request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void wait_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    generalHandlerData.delay = getBe32(&message[0]);
    modRunnerSleep(generalHandlerData.delay);
}

/**
 * Handler for GENERAL_SET_WATCHDOG_CFG request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void set_watchdog_cfg_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint32_t watchdogMin = getBe32(&message[0]);
    uint32_t watchdogMax = getBe32(&message[4]);
    WatchdogSetConfig(watchdogMin, watchdogMax);
}

/**
 * Handler for GENERAL_INJECT_ECC_ERROR request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void inject_ecc_error_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    uint32_t mask = getBe32(&message[0]);
    uint8_t memType = message[4];
    uint8_t errorType = message[5];

    xtMemepInjectError(memType, errorType, mask);
    xtMemepExtortError(memType);
}

/**
 * Handler for GENERAL_FORCE_FATAL_ERROR request
 * @param[in] message[] - retrieved message data
 * @param[in] len - message length
 * @param[in] type - type of mailbox module (regular or secure)
 */
static void force_fatal_error_req_handler(uint8_t message[], uint16_t len, MB_TYPE type) {
    xtExecFatalInstr();
}
// parasoft-end-suppress MISRA2012-RULE-2_7-4 "parameter not used in function" DRV-4608

/**
 * Mailbox message handler. Part of General Handler's main thread function.
 * @param[in] response_buffer
 */
static void GENERAL_handler_msg_handler(void) {
    uint8_t *msg;
    uint8_t opCode;
    uint16_t len;

    // Assigning handlers pointers in order with opcodes - just use opcode as index
    #define REQ_HANDLERS_ARRAY_LENGTH 18U
    static const General_handler_req_handler_t handlers[REQ_HANDLERS_ARRAY_LENGTH] = {
            (General_handler_req_handler_t) NULL,   // index 0x00 - unused
            main_control_req_handler,               // GENERAL_MAIN_CONTROL = 0x01
            test_echo_req_handler,                  // GENERAL_TEST_ECHO = 0x02,
            (General_handler_req_handler_t) NULL,   // 0x03 unused
            (General_handler_req_handler_t) NULL,   // 0x04 unused
            write_register_req_handler,             // GENERAL_WRITE_REGISTER = 0x05,
            write_field_req_handler,                // GENERAL_WRITE_FIELD = 0x06,
            read_register_req_handler,              // GENERAL_READ_REGISTER = 0x07,
            wait_req_handler,                       // GENERAL_WAIT = 0x08,
            set_watchdog_cfg_req_handler,           // GENERAL_SET_WATCHDOG_CFG = 0x09,
            inject_ecc_error_req_handler,           // GENERAL_INJECT_ECC_ERROR = 0x0A,
            force_fatal_error_req_handler,          // GENERAL_FORCE_FATAL_ERROR = 0x0B,
            (General_handler_req_handler_t) NULL,   // 0x0c unused
            (General_handler_req_handler_t) NULL,   // 0x0d unused
            (General_handler_req_handler_t) NULL,   // 0x0e unused
            (General_handler_req_handler_t) NULL,   // 0x0f unused
            (General_handler_req_handler_t) NULL,   // 0x10 unused
            hpd_state_req_handler                   // GENERAL_GET_HPD_STATE = 0x11,
            };

    static const MB_TYPE checked_mb_types[2] = { MB_TYPE_REGULAR, MB_TYPE_SECURE };

    uint8_t i;
    for (i = 0U; i < 2U; i++) {
        if (MB_isWaitingModuleMessage(checked_mb_types[i], MB_MODULE_ID_GENERAL)) {
            MB_getCurMessage(checked_mb_types[i], &msg, &opCode, &len);
            // run request handler for regular mailbox
            if ((opCode < REQ_HANDLERS_ARRAY_LENGTH) && (handlers[opCode] != NULL)) {
                (handlers[opCode])(msg, len, checked_mb_types[i]);
            }
            MB_FinishReadMsg(checked_mb_types[i]);
        }
    }
}

/** Main thread of general handler module */
static void GENERAL_handler_thread(void) {
    if (generalHandlerData.delay != 0U) {
        // Wait was requested, and is now complete. Sending response.
        uint8_t* response_buffer;
        response_buffer = MB_GetTxBuff(MB_TYPE_REGULAR);
        setBe32(generalHandlerData.delay, &response_buffer[0]);

        generalHandlerData.delay = 0U;

        MB_SendMsg(MB_TYPE_REGULAR, 4, (uint8_t) GENERAL_WAIT_RESP, MB_MODULE_ID_GENERAL);
    } else {
        GENERAL_handler_msg_handler();
    }
}

/** Insert new general handler module */
void GENERAL_Handler_InsertModule(void)
{
    static Module_t GeneralMailHandlerModule;
    // set up a structure
    GeneralMailHandlerModule.initTask=&GENERAL_handler_init;
    GeneralMailHandlerModule.startTask=&GENERAL_handler_start;
    GeneralMailHandlerModule.thread=&GENERAL_handler_thread;
    GeneralMailHandlerModule.moduleId = MODRUNNER_MODULE_GENERAL_HANDLER;
    GeneralMailHandlerModule.pPriority=0;
    // tell modRunner to insert the module
    modRunnerInsertModule(&GeneralMailHandlerModule);
}

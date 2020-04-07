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
 * IPMain.c
 *
 ******************************************************************************
 */

#include "general_handler.h"
#include "utils.h"
#include "timer.h"
#include "interrupt.h"
#include "mailBox.h"
#include "watchdog.h"
#include "reg.h"

#include "xtUtils.h"

#include "controlChannelM.h"

#include "dp_tx_mail_handler.h"
#include "dp_tx.h"
#include "cdn_log.h"

#ifndef FW_VERSION
#error "Please specify firmware version!"
#endif

#ifndef REVISION_NUM
#error "Please specify revision number used to compile firmware!"
#endif

/* These variables set up logging. Only relevant for DEBUG build (DEBUG defined) */
uint32_t g_dbg_enable_log = 1U;
uint32_t g_dbg_log_lvl = DBG_CRIT;
uint32_t g_dbg_log_cnt = 0U;


static inline void VersionSet(void)
{
    /* Set firmware version in registers */
    RegWrite(VER_L, GetByte0(FW_VERSION));
    RegWrite(VER_H, GetByte1(FW_VERSION));
    RegWrite(VER_LIB_L, GetByte0(REVISION_NUM));
    RegWrite(VER_LIB_H, GetByte1(REVISION_NUM));
}


/* Setup essential clocks in appropriate registers. Part of main.  */
static inline void init_essential_clocks(void) {
    /* Enable essential clocks */
    RegWrite(source_hdtx_car,
            RegFieldWrite(SOURCE_HDTX_CAR, HDTX_SYS_CLK_EN, 0, 1) |
            RegFieldWrite(SOURCE_HDTX_CAR, HDTX_SYS_CLK_RSTN_EN, 0, 1)
    );
    RegWrite(source_cec_car,
            RegFieldWrite(SOURCE_CEC_CAR, SOURCE_CEC_SYS_CLK_EN, 0, 1) |
            RegFieldWrite(SOURCE_CEC_CAR, SOURCE_CEC_SYS_CLK_RSTN_EN, 0, 1)
    );
    RegWrite(source_dptx_car,
            RegFieldWrite(SOURCE_DPTX_CAR, DPTX_SYS_CLK_EN, 0, 1) |
            RegFieldWrite(SOURCE_DPTX_CAR, DPTX_SYS_CLK_RSTN_EN, 0, 1) |
            RegFieldWrite(SOURCE_DPTX_CAR, SOURCE_AUX_SYS_CLK_EN, 0, 1) |
            RegFieldWrite(SOURCE_DPTX_CAR, SOURCE_AUX_SYS_CLK_RSTN_EN, 0, 1)
    );
}

/* parasoft-begin-suppress METRICS-37-3 "A function should not call more than 7 different functions, DRV-5253" */
int main(void)
{
    /* Disable watchdog */
    WatchdogSetEnable(false);

    /* Disable DMEM and IMEM access from Host's side */
    RegWrite(UCPU_MEM_CTRL, 0);

    /* APB access by Host, CAPB by uCPU */
    RegWrite(UCPU_BUS_CTRL, 0);

    /* Reset debug registers */
    RegWrite(SW_DEBUG_L, 0x0);
    RegWrite(SW_DEBUG_H, 0x0);
    cDbgMsg(DBG_GEN_MSG, DBG_CRIT, "test string %d\n", 11);

    /* Set Firmware and Lib versions in registers. */
    VersionSet();

    /* Read uCPU frequency and set up internal timing parameters */
    updateClkFreq();

    DP_TX_MAIL_HANDLER_initOnReset();
    init_essential_clocks();
    DP_TX_hdpInit();
    interruptInit();
    modRunnerInit();

    /* Start Mailbox modules */
    MB_InsertModule();
    MB_Secure_InsertModule();

    /* Start Main module */
    GENERAL_Handler_InsertModule();

    WatchdogSetup(WATCHDOG_MIN_VALUE, WATCHDOG_MAX_VALUE);

#if defined(EXT_ECC_EN)
    xtSetEccEnable(1);
#endif

    /* Run main loop */
    modRunnerRun();

    /* Return 1 (one) */
    return 1;
}
/* parasoft-end-suppress METRICS-37-3 */


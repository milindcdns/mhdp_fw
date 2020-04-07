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
 * interrupt.c
 *
 ******************************************************************************
 */

#include "interrupt.h"
#include "dp_tx.h"
#include <stdint.h>
#include "general_handler.h"
#include "reg.h"
#include "dp_tx.h"
#include <xtensa/xtruntime.h>

extern uint8_t g_hpd_state;
uint8_t g_hpd_state = 0;

/** Interrupt service routine for Hpd event */
static void HpdEventDetectedIsr(void) {
    uint32_t hpd_event = RegRead(HPD_EVENT_DET);
    // Decode what kind of hpd event needs to be handled
    if (RegFieldRead(HPD_EVENT_DET, HPD_UNPLUGGED_DET_ACLK, hpd_event) != 0U) {
        g_hpd_state = 0;
        DP_TX_disconnect();
    }

    if (RegFieldRead(HPD_EVENT_DET, HPD_RE_PLGED_DET_EVENT, hpd_event) != 0U) {
        g_hpd_state = 1U;
        DP_TX_connect();
    }

    if (RegFieldRead(HPD_EVENT_DET, HPD_IRQ_DET_EVENT, hpd_event) != 0U) {
        DP_TX_interrupt();
    }
}

// parasoft-begin-suppress MISRA2012-RULE-8_13_a-4 "A pointer parameter should be declared as pointer to const if it is not used to modify the addressed object, DRV-5251"
// parasoft-begin-suppress MISRA2012-RULE-2_7 "Parameter is not used, DRV-5251"

/** Main interrupt handler */
static void interruptHandler(void *arg) {
    if (RegFieldRead(DPTX_INT_STATUS, DPTX_SRC_INT, RegRead(DPTX_INT_STATUS)) != 0U) {
        // HPD event detected
        HpdEventDetectedIsr();
    }

    uint32_t dp_aux_event = RegRead(DP_AUX_INTERRUPT_SOURCE);
    if (RegFieldRead(DP_AUX_INTERRUPT_SOURCE, AUX_TX_DONE, dp_aux_event) != 0U) {
        modRunnerWake(MODRUNNER_MODULE_DP_AUX_TX);
        DP_TX_setTxFlag();
    }
    if (RegFieldRead(DP_AUX_INTERRUPT_SOURCE, AUX_MAIN_RX_STATUS_DONE, dp_aux_event) != 0U) {
        modRunnerWake(MODRUNNER_MODULE_DP_AUX_TX);
        DP_TX_setRxFlag();
    }
}

// parasoft-end-suppress MISRA2012-RULE-8_13_a-4
// parasoft-end-suppress MISRA2012-RULE-2_7

/** Initialize interrupts */
void interruptInit(void) {
    // set up interrupt masks
    RegWrite(INT_MASK1, 0xFFFFFFFEU);
    RegWrite(INT_MASK_XT, 0xFFFFFFFCU);
    RegWrite(DPTX_INT_MASK, 0xFFFFFFFEU);
    RegWrite(HPD_EVENT_MASK, 0xFFFFFFF2U);
    RegWrite(DP_AUX_INTERRUPT_MASK, 0xFFFFFFF5U);

    (void) xtos_set_interrupt_handler(3U, &interruptHandler, NULL, NULL);
    (void) xtos_interrupt_enable(3U);
}

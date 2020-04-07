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
 * testModule.c
 *
 ******************************************************************************
 */

#ifdef USE_TEST_MODULE
#include "modRunner.h"
#include "mailBox.h"
#include "utils.h"

Module_t  testModule;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
void TM_init(void)
{

}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
void TM_start(void)
{
    modRunnerWakeMe();
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
void TM_thread(void)
{
    uint8_t *txBuff;
    uint8_t xounter = xthal_get_ccount();
    uint16_t mili = xounter / 100000U;
    static uint8_t firstTime = 0U;
    firstTime++;
    if (firstTime < 5U)
    {

        return;
    }
    if (MB_IsTxReady(0))
    {
        txBuff = MB_GetTxBuff();

        txBuff[0] = mili & 0xFF;
        txBuff[1] = (mili & 0xFF00) >> 8;
        MB_SendMsg(0, 2 , 0);
        modRunnerSleep(milliToMicro(1));
    }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
void TM_InsertModule(void)
{
    testModule.initTask = &TM_init;
    testModule.startTask = &TM_start;
    testModule.thread = &TM_thread;
    testModule.moduleId = MODRUNNER_TEST_MODULE;
    testModule.pPriority = 1;

    modRunnerInsertModule(&testModule);
}
#endif// USE_TEST_MODULE

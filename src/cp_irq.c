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
 * cp_irq.c
 *
 ******************************************************************************
 */

#include "cp_irq.h"
#include "hdcp_tran.h"
#include "hdcp2.h"
#include "hdcp14.h"
#include "controlChannelM.h"
#include "modRunner.h"

typedef struct {
    uint32_t statusRegAddr;
    uint16_t statusRegSize;
    uint16_t readTimeoutMs;
    StateCallback_t cb;
    uint8_t evMask;
} CpIrqEvData_t;


static CpIrqEvData_t cpIrqEvData;

static bool cpIrqUsed;

static void processStatusCb(void);
static void processCpIrq(void);
static void readStatusCb(void);

void initCpIrqRoutine(void)
{
    /* Initialize callback as NULL */
    cpIrqEvData.cb = NULL;

    /* Set status address */
    if (hdcpGenData.usedHdcpVer == HDCP_VERSION_2X) {
        cpIrqEvData.statusRegAddr = HDCP2X_RXSTATUS_ADDRESS;
        cpIrqEvData.statusRegSize = HDCP2X_RXSTATUS_SIZE;
    } else {
        cpIrqEvData.statusRegAddr = HDCP_BSTATUS_ADDRESS;
        cpIrqEvData.statusRegSize = HDCP_BSTATUS_SIZE;
    }
}

static void waitForCpIrq(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* HPD Pulse event was occurred */
        if (hdcpGenData.hpdPulseIrq) {
            hdcpGenData.hpdPulseIrq = false;

            /* Read IRQ vector to check if there's a IRQ for HDCP */
            CHANNEL_MASTER_read(1U, DEVICE_SERVICE_IRQ_VECTOR, hdcpGenData.hdcpBuffer);
            cpIrqEvData.cb = &processCpIrq;
        }
    }
}

static void processCpIrq(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Read CP_IRQ flag */
        bool cpIrq = (hdcpGenData.hdcpBuffer[0] & (uint8_t)DEVICE_SERVICE_CP_IRQ_MASK) != 0U;

        /* If CP_IRQ occurred, handle it. If not, sleep 1ms and pool again */
        if (cpIrq) {
            /* Cleanup CP_IRQ bit */
            hdcpGenData.hdcpBuffer[0] = (uint8_t)DEVICE_SERVICE_CP_IRQ_MASK;
            CHANNEL_MASTER_write(1U, DEVICE_SERVICE_IRQ_VECTOR, hdcpGenData.hdcpBuffer);

            cpIrqEvData.cb = &readStatusCb;

        } else {
            cpIrqEvData.cb = &waitForCpIrq;
            /* Try again handle interrupt */
            modRunnerSleep(milliToMicro(CP_IRQ_LATENCY_TIME_MS));
        }
    }
}

static void readStatusCb(void)
{
    if (CHANNEL_MASTER_isFree()) {
        /* Read Bstatus register */
        CHANNEL_MASTER_read(cpIrqEvData.statusRegSize, cpIrqEvData.statusRegAddr, hdcpGenData.hdcpBuffer);

        cpIrqEvData.cb = &processStatusCb;
    }
}

static void processStatusCb(void)
{
    if (CHANNEL_MASTER_isFree()) {

        /* Set correct flow due to Bstatus word */
        if ((hdcpGenData.hdcpBuffer[0] & cpIrqEvData.evMask) != 0U) {
            modRunnerTimeoutClear();
            cpIrqEvData.cb = NULL;
        } else {
            /* Set a wait-for-interrupt callback if interrupts are used */
            if (cpIrqUsed) {
                cpIrqEvData.cb = &waitForCpIrq;
            } else {
                cpIrqEvData.cb = &readStatusCb;
            }
        }
    }
}

/* Call cp callback */
void callCpIrqRoutine(void)
{
    if (cpIrqEvData.cb != NULL) {
        cpIrqEvData.cb();
    }
}

bool isCpIrqRoutineFinished(void)
{
    return cpIrqEvData.cb == NULL;
}

/* Workaround to not working CP_IRQs, VIPDISPLAY-1405 */
void setCpIrqEvent(uint8_t evMask, uint32_t timeoutMs, bool cpIrq)
{
    cpIrqEvData.evMask = evMask;

    cpIrqUsed = cpIrq;

    if (timeoutMs != CP_IRQ_NO_TIMEOUT) {
        modRunnerSetTimeout(milliToMicro(timeoutMs));
    }

    /* Go to status reading */
    if (cpIrqUsed) {
        cpIrqEvData.cb = &waitForCpIrq;
    } else {
        cpIrqEvData.cb = &readStatusCb;
    }
}

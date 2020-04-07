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
 * modRunner.c
 *
 ******************************************************************************
 */

// parasoft-begin-suppress METRICS-36-3 "A function should not be called from more than 5 different functions" DRV-3823

#include "cdn_stdint.h"
#include "cdn_stdtypes.h"
#include "modRunner.h"
#include "timer.h"
#include "watchdog.h"
#include "reg.h"

#include <stdio.h>
#include <string.h>

static S_MODRUNNER_DATA modRunnerState;

// private function for modRunner use only

// modRunner use it to run threads
static void modRunnerRunThreads(void);

// transition module to next state (INIT -> READY -> RUNNING)
static void modRunnerTransitionState(Module_t* module);

// find module by id
static uint8_t modRunnerFindModule(uint8_t id);

static inline void KeepAlive(void)
{
    /* Write anything to increment it */
    RegWrite(KEEP_ALIVE, 1);
}

/* Run mod runner */
void modRunnerRun(void)
{
    startTimer(MODRUNNER_SYS_TIMER);

    while (1) {  // go indefinitely
        modRunnerRunThreads();
        KeepAlive();
        WatchdogClear();
    }
}

/* Initialize mod runner */
void modRunnerInit(void)
{
    modRunnerState.activeTasks = 0;
    modRunnerState.runningThread = 0;

    for (uint8_t i = 0U; i < (uint8_t) MODRUNNER_MODULE_LAST; ++i) {
        modRunnerState.pModList[i] = NULL;
    }
}

static void modRunnerCheckThreadTimeout(Module_t* module, int32_t timeElapsed) {

    /* If timeout set */
    if (module->pTaskTimeOutState == MODRUNNER_TIMEOUT_SET) {
        module->pTimeOut -= (int32_t)timeElapsed;

        /* This means that it timed out */
        if (module->pTimeOut <= 0) {
            module->pTaskTimeOutState = MODRUNNER_TIMEOUT_EXPIRED;
        }
    }
}

/* If put to sleep */
static bool modRunnerIsThreadRunning(Module_t* module, int32_t timeElapsed) {

    int32_t* sleepUs = &(module->pSleepMicro);
    bool isRunning = module->pThreadState.running;

    if (*sleepUs > 0) {
        *sleepUs -= timeElapsed;
        /* This means the sleep time has not ended, must still sleep */
        if (*sleepUs > 0) {
            isRunning = false;
        }
    }

    return isRunning;
}

static void modRunnerRunThreads(void) {

    Module_t *module;
    bool isThreadRunning;
    uint8_t* threadNum = &(modRunnerState.runningThread);
    int32_t timeElapsed = (int32_t)getTimerUsWithUpdate(MODRUNNER_SYS_TIMER);

    for (*threadNum = 0; *threadNum < modRunnerState.activeTasks; (*threadNum)++) {

        module = modRunnerState.pModList[*threadNum];

        /* Update state of thread */
        modRunnerTransitionState(module);

        /* Update tiemout property */
        modRunnerCheckThreadTimeout(module, timeElapsed);

        /* Check if thread is sleep/running */
        isThreadRunning = modRunnerIsThreadRunning(module, timeElapsed);

        if (isThreadRunning) {
            module->thread();
        }
    }
}

/* Insert new module to mod runner */
void modRunnerInsertModule(Module_t *module)
{
    /* check if it is allowed to insert a module (a module must not be already inserted) */
    if (modRunnerFindModule((uint8_t) module->moduleId) == MODRUNNER_NOT_FOUND) {
        modRunnerState.pModList[modRunnerState.activeTasks] = module;
        module->pSleepMicro = 0;
        module->pTimeOut = 0;
        module->pTaskTimeOutState = MODRUNNER_TIMEOUT_EMPTY;
        module->pPriority = 0;
        module->pThreadState.running = false;
        module->pThreadState.has_mail = false;
        module->curState = MODRUNNER_INIT;

        modRunnerState.activeTasks++;
    }
}

/* Make a state transition for a mod runner (go to a next state, up to MODRUNNER_RUNNING) */
static void modRunnerTransitionState(Module_t* module)
{
    switch (module->curState)
    {
    case MODRUNNER_INIT:
        module->initTask();
        module->curState = MODRUNNER_READY;
        break;
    case MODRUNNER_READY:
        module->startTask();
        module->curState = MODRUNNER_RUNNING;
        break;
    default:
        // no action
        break;
    }
}

/* Wake up current thread */
void modRunnerWakeMe(void)
{
    // this function must be called only by running module to itself
    modRunnerState.pModList[modRunnerState.runningThread]->pThreadState.running = true; // set running
    modRunnerState.pModList[modRunnerState.runningThread]->pSleepMicro = 0;
}

/* Suspend current thread */
void modRunnerSuspendMe(void)
{
    // this function must be called only by running module to itself
    modRunnerState.pModList[modRunnerState.runningThread]->pThreadState.running = false; // clear running
}

/* Wake up module of given id */
void modRunnerWake(MODRUNNER_MODULE_ID id)
{
    uint8_t const idx = modRunnerFindModule((uint8_t) id);

    modRunnerState.pModList[idx]->pThreadState.running = true; // set running
    modRunnerState.pModList[idx]->pSleepMicro = 0;
}

/* Suspend module of given id */
void modRunnerSuspend(MODRUNNER_MODULE_ID id)
{
    uint8_t const idx = modRunnerFindModule((uint8_t) id);

    modRunnerState.pModList[idx]->pThreadState.running = false; // clear running
}

/* Put the current running module to sleep for a given time */
void modRunnerSleep(uint32_t micro)
{
    // this function must be called only by running module to itself
    modRunnerState.pModList[modRunnerState.runningThread]->pSleepMicro = (int32_t) micro;
}

/* Set timeout for current thread (in microseconds) */
void modRunnerSetTimeout(uint32_t micro)
{
    modRunnerState.pModList[modRunnerState.runningThread]->pTaskTimeOutState = MODRUNNER_TIMEOUT_SET;
    modRunnerState.pModList[modRunnerState.runningThread]->pTimeOut = (int32_t) micro;
}

/* Check if timeout of current thread expired */
bool modRunnerIsTimeoutExpired(void)
{
    return modRunnerState.pModList[modRunnerState.runningThread]->pTaskTimeOutState == MODRUNNER_TIMEOUT_EXPIRED;
}

/* Clear timeout for current thread */
void modRunnerTimeoutClear(void)
{
    modRunnerState.pModList[modRunnerState.runningThread]->pTaskTimeOutState = MODRUNNER_TIMEOUT_EMPTY;
}

/* Find a module with a given id. If a module with given id exists, return its index. */
static uint8_t modRunnerFindModule(uint8_t id) {
    uint8_t idx = MODRUNNER_NOT_FOUND;
    for (uint8_t i = 0; i < modRunnerState.activeTasks; ++i) {
        // i < activeTasks condition already satisfies out-of-bounds checking
        if ((uint8_t) modRunnerState.pModList[i]->moduleId == id) {
            idx = i;
        }
    }
    return idx;
}

void modRunnerRemoveModule(uint8_t id) {
    uint8_t idx = modRunnerFindModule(id);

    if (idx != MODRUNNER_NOT_FOUND) {
        /* Remove module pointer from the list */
        while (idx < (modRunnerState.activeTasks - 1U)) {
            // parasoft-begin-suppress MISRA2012-DIR-4_1_a-2 "Avoid accessing arrays out of bounds, DRV-5223"
            // parasoft-begin-suppress MISRA2012-RULE-18_1_a-2 "Avoid accessing arrays out of bounds, DRV-5223"
            modRunnerState.pModList[idx] = modRunnerState.pModList[idx + 1U];
            // parasoft-end-suppress MISRA2012-DIR-4_1_a-2
            // parasoft-end-suppress MISRA2012-RULE-18_1_a-2
            ++idx;
        }
        --modRunnerState.activeTasks; // module removed, decrement active tasks
        // parasoft-begin-suppress MISRA2012-DIR-4_1_a-2 "Avoid accessing arrays out of bounds, DRV-5223"
        // parasoft-begin-suppress MISRA2012-RULE-18_1_a-2 "Avoid accessing arrays out of bounds, DRV-5223"
        modRunnerState.pModList[modRunnerState.activeTasks] = NULL;
        // parasoft-end-suppress MISRA2012-DIR-4_1_a-2
        // parasoft-end-suppress MISRA2012-RULE-18_1_a-2
    }
}

// parasoft-end-suppress METRICS-36-3


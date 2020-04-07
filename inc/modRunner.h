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
 * modRunner.h
 *
 ******************************************************************************
 */

/* parasoft-begin-suppress METRICS-36-3 "function called from more than 5 different functions, DRV-3823" */

#ifndef MODRUNNER_H_
# define MODRUNNER_H_

# include <sys/time.h>

# include <stdint.h>
# include "cdn_stdint.h"
# include "cdn_stdtypes.h"
# include "mailBox.h" // to recognize MB_TYPE here
#include "timer.h"

/**
 * \addtogroup INFRASTRUCTURES
 * \{
 */

 /**
 *  \file modRunner.h
 *  \brief Implementation of simple task scheduler for internal use
 * Implementation of simple task scheduler for internal use the
 * scheduler knows to run tasks, enable sleep request, resume or
 * suspend task, each module has and init and start function, each
 * module can be separated externally be in different state. Working
 * states are :
 * MODRUNNER_READY - module functions called - init
 * MODRUNNER_RUNNING - module functions called - init, start
 * modRunner supports 2 priorities, normal and high, it commonly used
 * to increase priority of task after interrupt call to respond the
 * interrupt as fast as possible thread of module will start to run
 * only after it get modRunnerWakeMe command
 * Author - yehonatan levin - cadence
 *
 */

/**
 *  \brief internal modRunner struct that holds module thread state
 */
typedef struct {
    bool running;
    bool has_mail;
} ModRunnerThreadState;

/**
 *
 *  \brief module states
 * MODRUNNER_INIT - initial state
 * MODRUNNER_READY - module functions called: init
 * MODRUNNER_RUNNING - module functions called: init, start
 */
typedef enum
{
    MODRUNNER_INIT,
    MODRUNNER_READY,
    MODRUNNER_RUNNING
} TASK_STATE;


typedef enum
{
    MODRUNNER_TIMEOUT_EMPTY,
    MODRUNNER_TIMEOUT_SET,
    MODRUNNER_TIMEOUT_EXPIRED,
} TASK_TIMEOUT;

typedef enum
{
    MODRUNNER_MODULE_HDCP_TX,
    MODRUNNER_MODULE_NUM_OF_PORTS,
    MODRUNNER_MODULE_MAIL_BOX,
    MODRUNNER_MODULE_SECURE_MAIL_BOX,
    MODRUNNER_MODULE_DP_AUX_TX,
    MODRUNNER_MODULE_DP_AUX_TX_MAIL_HANDLER,
    MODRUNNER_MODULE_GENERAL_HANDLER,
#ifdef USE_TEST_MODULE
    MODRUNNER_TEST_MODULE,
#endif // USE_TEST_MODULE
    MODRUNNER_MODULE_LAST
} MODRUNNER_MODULE_ID;

/**
 *
 *  \brief  module struct used by each module, each module should set the following params when insert module to the system
 * initTask - pointer to init function
 * startTask - pointer to start function
 * thread - pointer to thread
 */
typedef struct
{
    //used by user to insert new module
    void (*initTask) (void);
    void (*startTask)(void);
    void (*thread) (void);
    MODRUNNER_MODULE_ID moduleId;
    // internally used by modRunner
    int32_t pSleepMicro;
    int32_t pTimeOut;
    TASK_TIMEOUT pTaskTimeOutState;
    uint8_t pPriority;
    ModRunnerThreadState pThreadState;
    TASK_STATE curState;
} Module_t;

/**
 *
 *  \brief  all of modRunner data, include all modules pointers
 */
typedef struct
{
Module_t *pModList[(uint8_t) MODRUNNER_MODULE_LAST];
    uint8_t activeTasks;
    uint8_t runningThread;
} S_MODRUNNER_DATA;

# define MODRUNNER_NOT_FOUND (uint8_t)0xFF

// public functions

/**
 *
 *  \brief  endless loop, called it when you start the modRunner
 */
void modRunnerRun(void);

/**
 *
 *  \brief  must be called before modRunner run
 */
void modRunnerInit(void);

/**
 *
 *  \brief  insert new module to modRunner, see Module_t documentation
 */
void modRunnerInsertModule(Module_t *module);

/**
 *
 *  \brief start my thread
 */
void modRunnerWakeMe(void);

/**
 *
 *  \brief suspend my thread
 */
void modRunnerSuspendMe(void);

/**
 *
 *  \brief thread go to sleep to micro (should be called from thread only)
 */
void modRunnerSleep(uint32_t micro);

/**
 *
 *  \brief remove module from modRunner
 */
void modRunnerRemoveModule(uint8_t id);

/**
 *
 *  \brief wake up specific module thread (can be called from everywhere
 */
void modRunnerWake(MODRUNNER_MODULE_ID id);

/**
 *
 *  \brief suspend specific module thread (can be called from everywhere
 */
void modRunnerSuspend(MODRUNNER_MODULE_ID id);

void modRunnerSetTimeout(uint32_t micro);
bool modRunnerIsTimeoutExpired(void);
void modRunnerTimeoutClear(void);

#endif

/* parasoft-end-suppress METRICS-36-3 */

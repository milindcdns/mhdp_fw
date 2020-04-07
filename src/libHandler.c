/* parasoft-begin-suppress MISRA2012-RULE-3_1_b-2, "Do not embed // comment marker inside C-style comment", DRV-5199 */
/******************************************************************************
 * Copyright (C) 2019 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************
 *
 * libHandler.c
 *
 ******************************************************************************
 */
/* parasoft-end-suppress MISRA2012-RULE-3_1_b-2 */

#include "libHandler.h"

/** Instance of library handler */
LibHandler_t lib_handler;

/** Used to reset library state */
void LIB_HANDLER_Clean(void)
{
    lib_handler.rsa_index = 0U;
    lib_handler.rsaRxstate = 0U;
    lib_handler.expModCalcCb = NULL;
    lib_handler.divCalcCb = NULL;
}

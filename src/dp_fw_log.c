// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 ******************************************************************************
 * dp_fw_log.c
 * Print debug logs
 *
 * This is the example implementation of DbgPrint used in debug build
 ******************************************************************************
 */

#include "cdn_stdint.h"
#include "reg.h"
#include <stdarg.h>
#include <stdio.h>

// parasoft-begin-suppress MISRA2012-RULE-17_1_a-2 "The features of stdarg.h shall not be used, DRV-5235"
// parasoft-begin-suppress MISRA2012-RULE-17_1_b-2 "The features of stdarg.h shall not be used, DRV-5235"
// parasoft-begin-suppress MISRA2012-RULE-21_6-2 "The Standard Library input/output functions shall not be used, DRV-5260"

#define DEBUG_STRING_LENGTH 100U

/* Write 2 bytes to SW_DEBUG registers */
static void dbg_reg_log2(uint8_t byte1, uint8_t byte2) {
    RegWrite(SW_DEBUG_H, (byte1));
    RegWrite(SW_DEBUG_L, (byte2));
}
void DbgPrint(const char *fmt, ...);

/* Convert your custom debug string with formatting and use SW_DEBUG logging functionality
 * to display list of integer casts of string chars.
 * Output will then have to be parsed back to string.
 * Limitation: Do not use characters other than regular ASCII (UTF-8 not supported). */
void DbgPrint(const char *fmt, ...) {
    char debug_string[DEBUG_STRING_LENGTH] = {0};
    va_list arg_list;
    va_start(arg_list, fmt);
    // save the formatted string
    (void)vsnprintf(debug_string, DEBUG_STRING_LENGTH, fmt, arg_list); // sprintf gives garbage when passing variables
    uint8_t i = 0;
    // parasoft-begin-suppress MISRA2012-DIR-4_1_a-2 "Avoid accessing arrays out of bounds, DRV-5392"
    // parasoft-begin-suppress MISRA2012-RULE-18_1_a-2 "Avoid accessing arrays out of bounds, DRV-5392"
    while (i < (DEBUG_STRING_LENGTH - 1U)) {
        if (debug_string[i + 1U] == '\0') { // if there is no next char, add space
            dbg_reg_log2((uint8_t)debug_string[i], (uint8_t)' ');
            break;
        } else {
            dbg_reg_log2((uint8_t)debug_string[i], (uint8_t)debug_string[i + 1U]);
        }
        i += 2U;
    }
    // parasoft-end-suppress MISRA2012-DIR-4_1_a-2
    // parasoft-end-suppress MISRA2012-RULE-18_1_a-2
    dbg_reg_log2((uint8_t)'\n', (uint8_t)'\0');
    va_end(arg_list);
}
// parasoft-end-suppress MISRA2012-RULE-17_1_a-2
// parasoft-end-suppress MISRA2012-RULE-17_1_b-2
// parasoft-end-suppress MISRA2012-RULE-21_6-2

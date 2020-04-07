// SPDX-License-Identifier: GPL-2.0-only
/**
 * Cadence Display Port Xtensa Firmware
 *
 * Copyright (C) 2019 Cadence Design Systems 
 * 
 * http://www.cadence.com
 *
 */

#ifndef REG_H_
#define REG_H_

#include "cps_drv.h"
#include "cdn_stdtypes.h"
#include "mhdp_apb_regs.h"
#include "mhdp_apb_regs_macros.h"

// TODO DK: Rename to Regs (as a tribute to Regs tool)
extern MHDP_ApbRegs *mhdpRegBase;

// TODO DK: Add proper tickets to waivers
// parasoft-begin-suppress MISRA2012-DIR-4_1_b "Avoid null pointer dereferencing." DRV-4464
// parasoft-begin-suppress MISRA2012-DIR-4_9-4 "function-like macro"
// parasoft-begin-suppress MISRA2012-RULE-20_10-4 "## preprocessor operator"

/**
 * Write 32-bit value to MHDP register.
 * @param [in] reg Name of the register in the mhdp_apb_regs structure.
 * @returns Void.
 */
#define RegWrite(reg, val) CPS_RegWrite \
( \
    &mhdpRegBase->mhdp_apb_regs.reg##_p, \
    (val) \
)

/**
 * Read 32-bit value from MHDP register.
 * @param [in] reg Name of the register in the mhdp_apb_regs structure.
 * @param [in] val Value to be written.
 * @returns Value of the register.
 */
#define RegRead(reg) CPS_RegRead \
( \
    &mhdpRegBase->mhdp_apb_regs.reg##_p \
)

/**
 * Sets the value of a register field.
 * @param [in] reg Uppercase name of the register in the mhdp_apb_reg structure.
 * @param [in] fld Name of the field.
 * @param [in] regval Current value of the register.
 * @param [in] val Value to be written to the register field.
 * @returns Modified register value.
 */
#define RegFieldWrite(reg, fld, regval, val) CPS_FldWrite \
( \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_MASK, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_SHIFT, \
    (regval), (val) \
)

/**
 * Returns the value of a register field.
 * @param [in] reg Uppercase name of the register in the mhdp_apb_reg structure.
 * @param [in] fld Name of the field.
 * @param [in] regval Current value of the register.
 */
#define RegFieldRead(reg, fld, regval) CPS_FldRead( \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_MASK, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_SHIFT, \
    (regval) \
)

/**
 * Sets the bit of a register field to 1.
 * @param [in] reg Uppercase name of the register in the mhdp_apb_reg structure.
 * @param [in] fld Name of the field.
 * @param [in] regval Current value of the register.
 * @returns Modified register value.
 */
#define RegFieldSet(reg, fld, regval) CPS_FldSet \
( \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_WIDTH, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_MASK, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_WOCLR, \
    (regval) \
)

/**
 * Clears the bit of a register field.
 * @param [in] reg Uppercase name of the register in the mhdp_apb_reg structure.
 * @param [in] fld Name of the field.
 * @param [in] regval Current value of the register.
 * @returns Modified register value.
 */
#define RegFieldClear(reg, fld, regval) CPS_FldClear( \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_WIDTH, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_MASK, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_WOSET, \
    MHDP__MHDP_APB_REGS__##reg##_P__##fld##_WOCLR, \
    (regval) \
)

// parasoft-end-suppress MISRA2012-DIR-4_1_b
// parasoft-end-suppress MISRA2012-DIR-4_9-4
// parasoft-end-suppress MISRA2012-RULE-20_10-4

#endif

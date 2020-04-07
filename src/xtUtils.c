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
 * xtUtils.c
 *
 ******************************************************************************
 */

#include "cdn_stdint.h"
#include "xtUtils.h"

#include <xtensa/corebits.h>
#include <xtensa/xtruntime.h>
#include <xtensa/config/specreg.h>

#define INTERRUPT_LEVEL 15U

/* Dummy value to check if misrepresentation in data was detected and repaired. */
static volatile uint32_t dataForEccErrorInjection = 0x12345678U;

/* Dummy function to check if misrepresentation in instruction code was detected and repaired */
static void eccErrorInjectionTestFunction(void)
{
    static uint32_t eccInjCounter = 0U;
    eccInjCounter++;
}

static inline void asm_wsr(uint32_t regVal, uint32_t regNum) {
    /* Write value to specified SR (special register) */
    asm ("wsr %[regVal], %[regNum]"
        : /* No output parameters */
        : [regVal] "a" (regVal), [regNum] "n" (regNum));
}

/* parasoft-begin-suppress MISRA2012-RULE-8_13_a-4, "Parameter should be passed as const", DRV-5203*/
static inline void asm_rsr(uint32_t* regVal, uint32_t regNum) {
    /* Read value from specified SR */
    asm ("rsr %[regVal], %[regNum]"
        : [regVal] "=a" (*regVal)
        : [regNum] "n" (regNum));
}
/* parasoft-end-suppress MISRA2012-RULE-8_13_a-4 */

static inline void asm_xsr(uint32_t mask, uint32_t regNum) {
    /* Use assembly to read value from SR, xor it with specified mask
     * and write it back into SR */
    asm ("xsr %[mask], %[regNum]"
        : /* No output parameters */
        : [mask] "a" (mask), [regNum] "n" (regNum));
}

/* parasoft-begin-suppress MISRA2012-RULE-8_13_a-4, "Parameter should be passed as const", DRV-5203*/
static inline void asm_rsil(uint32_t* oldLvl, uint32_t newLvl) {
    /* Save interrupt level and write new value */
    asm ("rsil %[value], %[intLvl]"
        : [value] "=a" (*oldLvl)
        : [intLvl] "n" (newLvl));
}
/* parasoft-begin-suppress MISRA2012-RULE-8_13_a-4 */

static inline void asm_ill(void) {
    /* Execute illegal instruction to force error */
    asm ("ill");
}

static inline void asm_s32in(uint32_t val, uintptr_t addr, uint32_t off) {
    /* Write 32b value into register */
    asm ("s32i.n %[value], %[regAddr], %[offset]"
        : /* No outputs */
        : [value] "a" (val), [regAddr] "a" (addr), [offset] "n" (off));
}

static inline void asm_rsync(void) {
    /* Synchronize register read */
    asm ("rsync");
}

/* Function return address of memory where misrepresentation will be injected */
static inline volatile uint32_t* getSpoiledValue(uint8_t memType)
{
    volatile uint32_t* spoiledVal;

    if (memType == (uint8_t)ECC_ERROR_MEM_TYPE_INSTRUCTION_RAM) {
        /* parasoft-begin-suppress MISRA2012-RULE-11_1_a-2, "Pointer to function should not be cast to other type", DRV-5202*/
        spoiledVal = (volatile uint32_t*)(&eccErrorInjectionTestFunction);
        /* parasoft-end-suppress MISRA2012-RULE-11_1_a-2 */
    } else {
        spoiledVal = (&dataForEccErrorInjection);
    }

    return spoiledVal;
}

/* Function used to inject error into data */
static inline void doInjection(uint8_t memType, uint8_t errorType, uint32_t mask)
{
    uint8_t i;
    uint32_t checkbits, mesr;
    uint32_t dummyVal;

    volatile uint32_t* spoiledVal = getSpoiledValue(memType);

    /* Save current MESR and set test mode: */
    asm_rsr(&mesr, MESR);

    /* If memory error test mode is 'normal', set it to 'special' and disable memory ECC/Parity errors
       (refer to MESR register fields in ISA Reference manual) */
    if ((mesr & (uint32_t)MESR_ERRTEST) == 0U) {
        mesr |=  ((uint32_t)MESR_ERRTEST);
        mesr &= ~((uint32_t)MESR_ERRENAB);

        asm_xsr(mesr, MESR);
    }

    dummyVal = *spoiledVal;

    if (errorType == (uint8_t)ECC_ERROR_TYPE_DATA) {
        /* Inject error into data bits */
        dummyVal ^= mask;
    } else {

        /* dummy instruction to save actual content of MECR into the memory check bits
           use 'asm volatile' instead library function to avoid optimize of code */
        *spoiledVal = dummyVal;

        /* apply error mask to check bits, rewrite to target location
           so that checkbits also get written out to ECC memory */
        asm_rsr(&checkbits, MECR);
        checkbits ^= mask;
        asm_wsr(checkbits, MECR);
    }

    *spoiledVal = dummyVal;

    /* set memory error test mode as 'normal' and enable error checks */
    mesr &= ~((uint32_t)MESR_ERRTEST);
    mesr |=  ((uint32_t)MESR_ERRENAB);
    asm_xsr(mesr, MESR);

}

void xtMemepInjectError(uint8_t memType, uint8_t errorType, uint32_t mask)
{

#ifdef XCHAL_HAVE_INTERRUPTS

    uint32_t saved_interrupts;
    asm_rsil(&saved_interrupts, INTERRUPT_LEVEL);

#endif /* XCHAL_HAVE_INERRUPTS */

    doInjection(memType, errorType, mask);

#ifdef XCHAL_HAVE_INTERRUPTS
    /* Restore PS.INTLEVEL: */
    asm_wsr(saved_interrupts, PS);
    asm_rsync();

#endif /* XCHAL_HAVE_INTERRUPTS */

}

void xtMemepExtortError(uint8_t memType)
{
    /* Dummy variable to force DRAM ECC error */
    uint32_t value;

    if (memType == (uint8_t)ECC_ERROR_MEM_TYPE_INSTRUCTION_RAM) {
        eccErrorInjectionTestFunction();
    } else {
        value = dataForEccErrorInjection;
    }
}

void xtSetEccEnable(uint8_t enable)
{
    uint32_t regVal;

    if (enable == 1U) {
    	/* Set ECC enabled */
        _xtos_memep_enable(0);
    } else {
    	/* Read MESR register and cleanup ERRENAB bit */
        asm_rsr(&regVal, MESR);
        regVal &= ~((uint32_t)MESR_ERRENAB);
        asm_wsr(regVal, MESR);
    }
}

void xtExecFatalInstr(void)
{
    /* Cleanup first word of User and Debug Exception Vector to constrain ASF integrity error */

    uintptr_t userVectorAddr = XCHAL_USER_VECTOR_PADDR;
    uintptr_t debugVectorAddr = XCHAL_DEBUG_VECTOR_PADDR;

    /* Write 0 to first address of USER_VECTOR */
    asm_s32in(0U, userVectorAddr, 0U);

    /* Write 0 to first address of DEBUG_VECTOR */
    asm_s32in(0U, debugVectorAddr, 0U);

    /* Force illegal instruction */
    asm_ill();
}

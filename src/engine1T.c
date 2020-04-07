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
 * engine1T.c
 *
 ******************************************************************************
 */

#include "engine1T.h"
#include "engine.h"
#include "hdcp14.h"
#include "cdn_stdtypes.h"
#include "utils.h"
#include "sha.h"
#include "utils.h"
#include "hdcp_tran.h"
#include "cipher_handler.h"
#include "reg.h"
#include "cdn_errno.h"

/* HDCP v1.x module operates on data in little-endian order.
 * Therefore: An, Bksv, V, M0 and Aksv are stored in little-endian.
 * KeySv are stored in big-endian, as were received (KeySv is used
 * only on Tx side and any endianness could be) */
typedef struct {
    /* Pseudo-random valie sent to HDCP Receiver/Repeater by transmitter */
    uint8_t An[HDCP1X_AN_SIZE];
    /* HDCP 1.x Receiver/repeater's device keys */
    uint8_t Bksv[HDCP1X_BKSV_SIZE];
    /* KSV list integrity value of generated by transmitter */
    uint8_t V[HDCP1X_V_PRIME_SIZE];
    /* Value generated at the tranmitter that indicates the success of the authentication exchange */
    uint16_t R0;
    /* Integrity verification key used in the second part of authentication protocol */
    uint8_t M0[HDCP1X_M0_SIZE];
    /* HDCP 1.x Transmitter's device keys */
    uint8_t Aksv[HDCP1X_AKSV_SIZE];
    /* HDCP 1.4 Device Keys */
    uint8_t KeySv[HDCP1X_DEVICE_PRIVATE_KEY_NUMBER][HDCP1X_DEVICE_PRIVATE_KEY_SIZE];
    /* Flag indicates if An key is custom or generated */
    bool useDebugAn;
} Hdcp1xEngineData_t;

static Hdcp1xEngineData_t transData;

/**
 * Function used to read M0 key from registers
 */
static inline void readM0(void)
{
    setLe32(RegRead(CRYPTO14_MI_0), &transData.M0[0]);
    setLe32(RegRead(CRYPTO14_MI_1), &transData.M0[4]);
}

void ENG1T_getAn(uint8_t* An)
{
    /* If custom An is not set, randomize it */
    if (!transData.useDebugAn) {
        UTIL_FillRandomNumber(transData.An, HDCP1X_AN_SIZE);
    }

    CPS_BufferCopy(An, transData.An, HDCP1X_AN_SIZE);
}

void ENG1T_getAksv(uint8_t* Aksv)
{
    /* Return current value of Aksv key */
    CPS_BufferCopy(Aksv, transData.Aksv, HDCP1X_AKSV_SIZE);
}

void ENG1T_setBksv(uint8_t* Bksv)
{
    /* Bksv received from HDCP v1.3 register is in little-endian order */
    CPS_BufferCopy(transData.Bksv, Bksv, HDCP1X_BKSV_SIZE);

}

void ENG1T_getBksv(uint8_t* Bksv)
{
    /* Bksv received from HDCP v1.3 register is in little-endian order */
    CPS_BufferCopy(Bksv, transData.Bksv, HDCP1X_BKSV_SIZE);
}

uint32_t ENG1T_verifyBksv(void)
{
    uint8_t counter = 0U;
    uint8_t byteNum;
    uint8_t bitNum;
    uint8_t value;
    uint32_t result = CDN_EINVAL;

    for (byteNum = 0U; byteNum < (uint8_t)HDCP1X_BKSV_SIZE; byteNum++) {
        /* Load next byte of Bksv */
        value = transData.Bksv[byteNum];
        for (bitNum = 0U; bitNum < 8U; bitNum++) {
            /* Increment counter if '1' occured */
            counter += value & (uint8_t)HDCP1X_BKSV_CHECKER_MASK;
            /* Shift to check next bit */
            value = (uint8_t)safe_shift32(RIGHT, value, 1U);
        }
    }

    /* Check if number of '1' and '0' in Bksv are correct */
    if (counter == (uint8_t)HDCP1X_BKSV_NUMBER_OF_ONES) {
        result = CDN_EOK;
    }

    return result;
}

void ENG1T_loadKeys(uint8_t* Aksv, uint8_t* ksv)
{
    /* Aksv is stored in little-endian order */
    CPS_BufferCopy(transData.Aksv, Aksv, HDCP1X_AKSV_SIZE);
    convertEndianness(transData.Aksv, HDCP1X_AKSV_SIZE);

    CPS_BufferCopy(&transData.KeySv[0][0], ksv, HDCP1X_DEVICE_PRIVATE_KEYS_TOTAL_SIZE);

    transData.useDebugAn = false;
}


void ENG1T_loadDebugAn(uint8_t* An)
{
    /* An is stored in little-endian order */
    CPS_BufferCopy(transData.An, An, HDCP1X_AN_SIZE);
    convertEndianness(transData.An, HDCP1X_AN_SIZE);

    transData.useDebugAn = true;
}

bool ENG1T_isKmDone(void)
{
    return 0U != RegFieldRead(CRYPTO14_STATUS, KM_DONE, RegRead(CRYPTO14_STATUS));
}

bool ENG1T_isPrnmDone(void)
{
    return 0U != RegFieldRead(CRYPTO14_STATUS, PRNM_DONE, RegRead(CRYPTO14_STATUS));
}

static void computeDeviceKeys(void)
{
    uint8_t i;
    uint8_t* keys;

    for (i = 0U; i < (uint8_t)HDCP1X_DEVICE_PRIVATE_KEY_NUMBER; i++) {
        /* Get pointer to key */
        keys = &transData.KeySv[i][0];
        /* Write data into DKS block key RAM */
        RegWrite(CRYPTO14_KEY_MEM_DATA_0, getBe32(&keys[3]));
        RegWrite(CRYPTO14_KEY_MEM_DATA_1, getBe24(&keys[0]));
    }
}

void ENG1T_computeKm(void)
{
    uint32_t regVal;

    /* Reset Crypto */ // TODO DK: Why?
    RegWrite(CRYPTO14_CONFIG, 0U);

    RegWrite(HDCP_CRYPTO_CONFIG, RegFieldSet(HDCP_CRYPTO_CONFIG, CRYPTO_SW_RST, 0U));
    RegWrite(HDCP_CRYPTO_CONFIG, 0U);

    /* Set BKSV */
    RegWrite(CRYPTO14_YOUR_KSV_0, getLe32(&transData.Bksv[0]));
    RegWrite(CRYPTO14_YOUR_KSV_1, transData.Bksv[4]);
    regVal = RegFieldSet(CRYPTO14_CONFIG, GET_KSV, 0U);
    RegWrite(CRYPTO14_CONFIG, regVal);

    /* Set AKSV */
    RegWrite(CRYPTO14_KEY_MEM_DATA_0, getLe32(&transData.Aksv[0]));
    RegWrite(CRYPTO14_KEY_MEM_DATA_1, transData.Aksv[4]);
    regVal = RegFieldSet(CRYPTO14_CONFIG, VALID_KSV, regVal);
    RegWrite(CRYPTO14_CONFIG, regVal);

    /* Compute Device Keys */
    computeDeviceKeys();
}

void ENG1T_LFSR_calculation(HdcpDevType_t devType)
{
    uint32_t cryptoCfg = RegFieldSet(CRYPTO14_CONFIG, VALID_KSV, 0U)
                       | RegFieldSet(CRYPTO14_CONFIG, GET_KSV, 0U);

    if (devType == DEV_HDCP_REPEATER) {
        cryptoCfg = RegFieldSet(CRYPTO14_CONFIG, HDCP_REPEATER, cryptoCfg);
    }

    /* Set An */
    RegWrite(CRYPTO14_AN_0, getLe32(&transData.An[0]));
    RegWrite(CRYPTO14_AN_1, getLe32(&transData.An[4]));

    /* Start LFSR calculation */
    RegWrite(CRYPTO14_CONFIG, cryptoCfg);
    RegWrite(CRYPTO14_CONFIG, RegFieldSet(CRYPTO14_CONFIG, START_BLOCK_SEQ, cryptoCfg));
    RegWrite(CRYPTO14_CONFIG, cryptoCfg);
}

void ENG1T_compute_M0_R0(HdcpDevType_t devType)
{
    uint32_t regVal;

    /* Don't set 'HDCP_SELECT' field - for HDCP v1.x it should be 0 */
    uint32_t cipherCfg = RegFieldSet(HDCP_CIPHER_CONFIG, START_FREE_RUN, 0U);
    cipherCfg = RegFieldWrite(HDCP_CIPHER_CONFIG, HDCP_SELECT, cipherCfg, (uint32_t)HDCP_VERSION_1X);

    if (devType == DEV_HDCP_REPEATER) {
        cipherCfg = RegFieldSet(HDCP_CIPHER_CONFIG, CFG_REPEATER, cipherCfg); /* Repeater bit for Ri calculation */
    }

    regVal = RegRead(CRYPTO14_CONFIG);
    regVal = RegFieldSet(CRYPTO14_CONFIG, HDCP_AUTHENTICATED, regVal);
    RegWrite(CRYPTO14_CONFIG, regVal);

    /* Read M0 */
    readM0();

    RegWrite(HDCP_CIPHER_CONFIG, cipherCfg);

    /* Write computed Km keys into cipher */
    regVal = RegRead(CRYPTO14_KM_0);
    RegWrite(CIPHER14_KM_0, regVal);

    regVal = RegRead(CRYPTO14_KM_1);
    RegWrite(CIPHER14_KM_1, regVal);

    /* Write An */
    RegWrite(CIPHER14_AN_0, getLe32(&transData.An[0]));
    RegWrite(CIPHER14_AN_1, getLe32(&transData.An[4]));

    /* Disable free run */
    cipherCfg = RegFieldClear(HDCP_CIPHER_CONFIG, START_FREE_RUN, cipherCfg);
    RegWrite(HDCP_CIPHER_CONFIG, cipherCfg);

    /* Read Tx R0 value */
    transData.R0 = (uint16_t)RegRead(CRYPTO14_TI_0);
}

uint32_t ENG1T_compareR0(uint16_t r0)
{
    uint32_t result  = CDN_EINVAL;

    /* Compare R0 and R0' */
    if (r0 == transData.R0) {
        result = CDN_EOK;
    }

    return result;
}

void ENG1T_getKsvListAndComputeV(uint8_t *ksv_list, uint8_t ksv_list_size, uint16_t binfo)
{
    uint8_t size = ksv_list_size * (uint8_t)HDCP1X_KSV_LIST_ELEMENT_SIZE;

    /* Concatenate binfo with ksv_list */
    ksv_list[size] = GetByte1(binfo);
    size++;
    ksv_list[size] = GetByte0(binfo);
    size++;

    /* Concatenate M0 with ksv_list */
    CPS_BufferCopy(&ksv_list[size], transData.M0, HDCP1X_M0_SIZE);
    size += (uint8_t)HDCP1X_M0_SIZE;

    /* Now we have the input for SHA-1 = (KSV list || Binfo || M0) */
    sha1(ksv_list, size, transData.V);
}

bool ENG1T_validateV(const uint8_t* V)
{
    /* Compare V and V' */
    return if_buffers_equal(V, transData.V, HDCP1X_V_PRIME_SIZE);
}
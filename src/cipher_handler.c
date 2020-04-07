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
 * cipher_handler.c
 *
 ******************************************************************************
 */

#include "cipher_handler.h"
#include "utils.h"
#include "reg.h"
#include "engine.h"

/**
 * Save input key of AES-128 module into registers.
 * Input key is session key xored with lc128 (global constant)
 * @param[in] sessionKey, pointer to array with input keys.
 */
static void setAesKeys(const uint8_t* sessionKey)
{
    uint32_t regVal;

    /* Holds the [31:0] of the input key */
    regVal = getBe32(&sessionKey[12]);
    RegWrite(AES_128_KEY_0, regVal);

    /* Holds the [63:32] of the input key */
    regVal = getBe32(&sessionKey[8]);
    RegWrite(AES_128_KEY_1, regVal);

    /* Holds the [95:64] of the input key */
    regVal = getBe32(&sessionKey[4]);
    RegWrite(AES_128_KEY_2, regVal);

    /* Holds the [127:96] of the input key */
    regVal = getBe32(&sessionKey[0]);
    RegWrite(AES_128_KEY_3, regVal);
}

/**
 * Save Riv number value into registers. Riv is 64bit pseudo-random number
 * used in HDCP cipher
 * @param[in] riv, pointer to array with Riv values
 * @param[in] contentType, value associated with the content stream to be encrypted
 *                         xored with LSB of riv
 */
static void setAesRiv(const uint8_t* riv, uint8_t contentType)
{
    uint32_t regVal;

    /* XOR LSB of riv with contentType */
    regVal = getBe32(&riv[4]);
    regVal ^= (uint32_t)contentType;

    RegWrite(AES_128_RANDOM_0, regVal);

    regVal = getBe32(&riv[0]);
    RegWrite(AES_128_RANDOM_1, regVal);
}

/**
 * Clear input key of AES-128 module and Riv value in registers.
 * Input key is session key xored with lc128 (global constant).
 * Riv is 64bit pseudo-random number used in HDCP cipher
 */
static void clearAesRegs(void)
{
    /* Clear input key */
    RegWrite(AES_128_KEY_0, 0U);
    RegWrite(AES_128_KEY_1, 0U);
    RegWrite(AES_128_KEY_2, 0U);
    RegWrite(AES_128_KEY_3, 0U);

    /* Clear Riv */
    RegWrite(AES_128_RANDOM_0, 0U);
    RegWrite(AES_128_RANDOM_1, 0U);
}

void CIPHER_SetAuthenticated(void)
{
    /* Set authentication for HDCP 1.4 */
    RegWrite(CIPHER14_BOOTSTRAP, 1U);

    /* Set authentication for HDCP 2.2 */
    RegWrite(CIPHER22_AUTH, 1U);
}

void CIPHER_ClearAuthenticated(void)
{
    uint32_t regVal;
    uint8_t hdcpDpAuth;

    /* Check if HDCP was authenticated */
    regVal = RegRead(HDCP_DP_STATUS);
    hdcpDpAuth = (uint8_t)RegFieldRead(HDCP_DP_STATUS, HDCP_DP_AUTHENTICATED, regVal);

    if (hdcpDpAuth == 1U) {

        /* Clear authentication status */
        RegWrite(CIPHER14_BOOTSTRAP, 0U);
        RegWrite(CIPHER22_AUTH, 0U);

        RegWrite(HDCP_CIPHER_CONFIG, 0U);

        /* Clear Km and An values */
        RegWrite(CIPHER14_KM_0, 0U);
        RegWrite(CIPHER14_KM_1, 0U);
        RegWrite(CIPHER14_AN_0, 0U);
        RegWrite(CIPHER14_AN_1, 0U);

        clearAesRegs();
    }
}

void CIPHER_StartAuthenticated(const uint8_t* inputKey, const uint8_t* riv, uint8_t contentType)
{
    /* Send software reset signal for the Cipher core, set core version support for HDCP v2.x
       and start free running enable for operation hdcpRingCipher */
    uint32_t regVal = RegFieldSet(HDCP_CIPHER_CONFIG, CORE_SW_RESET, 0U)
                    | RegFieldWrite(HDCP_CIPHER_CONFIG, HDCP_SELECT, 0U, (uint32_t)HDCP_VERSION_2X);

    RegWrite(HDCP_CIPHER_CONFIG, regVal);

    regVal = RegFieldSet(HDCP_CIPHER_CONFIG, START_FREE_RUN, 0U)
           | RegFieldWrite(HDCP_CIPHER_CONFIG, HDCP_SELECT, 0U, (uint32_t)HDCP_VERSION_2X);

    RegWrite(HDCP_CIPHER_CONFIG, regVal);

    /* Set HDMI mode status as DVI */
    RegWrite(CIPHER_MODE, 0U);

    /* Save session key into registers */
    setAesKeys(inputKey);

    /* Save Riv value into registers */
    setAesRiv(riv, contentType);
}



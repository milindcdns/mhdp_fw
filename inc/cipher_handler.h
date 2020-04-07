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
 * cipher_handler.h
 *
 ******************************************************************************
 */

#ifndef CIPHER_HANDLER_H_
#define CIPHER_HANDLER_H_

#include "cdn_stdint.h"

/**
 *  Start Authentication for HDCP v2.x
 *  @param[in] inputKey, pointer to array with AES-CTR input key (ks ^ lc128);
 *      				 'ks' is session key, lc128 is global constant
 *  @param[in] riv, pointer to array with pseudo-random number used to cipher
 *  @param[in] contentType, value associated with the content stream to be encrypted
 */
void CIPHER_StartAuthenticated(const uint8_t* inputKey, const uint8_t* riv, uint8_t contentType);

/**
 * Clear authentication registers
 */
void CIPHER_ClearAuthenticated(void);

/**
 * Save in registers information, that authentication was finished successfully
 */
void CIPHER_SetAuthenticated(void);

#endif /* CIPHER_HANDLER_H_ */


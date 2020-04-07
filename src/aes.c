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
 * aes.c
 *
 ******************************************************************************
 */

#include "utils.h"
#include "reg.h"
#include "aes.h"

void aes_setkey(const uint8_t* key)
{
	uint8_t offset = AES_CRYPT_DATA_SIZE_IN_BYTES -  NUMBER_OF_BYTES_IN_UINT32T;

	/* Put into register bytes 12..15 in big-endian order */
	RegWrite(AES_32_KEY_0, getBe32(&key[offset]));
	offset -= NUMBER_OF_BYTES_IN_UINT32T;

	/* Put into register bytes 8..11 in big-endian order */
	RegWrite(AES_32_KEY_1, getBe32(&key[offset]));
	offset -= NUMBER_OF_BYTES_IN_UINT32T;

	/* Put into register bytes 4..7 in big-endian order */
	RegWrite(AES_32_KEY_2, getBe32(&key[offset]));
	offset -= NUMBER_OF_BYTES_IN_UINT32T;

	/* Put into register bytes 0..3 in big-endian order */
	RegWrite(AES_32_KEY_3, getBe32(&key[offset]));
	offset -= NUMBER_OF_BYTES_IN_UINT32T;
}

void aes_crypt(const uint8_t* input, uint8_t* output)
{
    uint8_t offset = 0U;
    uint32_t regVal;
    uint32_t mask = RegFieldSet(CRYPTO22_STATUS, AES_32_DONE_ST, 0);

    /* Send input key into AES module */
    while (offset < AES_CRYPT_DATA_SIZE_IN_BYTES) {
    	/* Put into register bytes [offset..offset + 3] in big-endian order */
   		RegWrite(AES_32_DATA_IN, getBe32(&input[offset]));
   		offset += NUMBER_OF_BYTES_IN_UINT32T;
    }

    /* Cleanup offset */
    offset = 0U;

    /* Wait for AES-32 output ready */
    do {
    	regVal = RegRead(CRYPTO22_STATUS);
    } while ((regVal & mask) == 0U);


    /* Read AES-32 output data */
    regVal = RegRead(AES_32_DATA_OUT_3);
    setBe32(regVal, &output[offset]);
    offset += NUMBER_OF_BYTES_IN_UINT32T;

    regVal = RegRead(AES_32_DATA_OUT_2);
    setBe32(regVal, &output[offset]);
    offset += NUMBER_OF_BYTES_IN_UINT32T;

    regVal = RegRead(AES_32_DATA_OUT_1);
    setBe32(regVal, &output[offset]);
    offset += NUMBER_OF_BYTES_IN_UINT32T;

    regVal = RegRead(AES_32_DATA_OUT_0);
    setBe32(regVal, &output[offset]);
    offset += NUMBER_OF_BYTES_IN_UINT32T;
}

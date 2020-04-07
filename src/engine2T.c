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
 * engine2T.c
 *
 ******************************************************************************
 */

#include "engine2T.h"
#include "hdcp2.h"
#include "hdcp_tran.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "sha.h"
#include "pkcs1.h"
#include "aes.h"
#include "utils.h"
#include "hdcp_tran.h"
#include "libHandler.h"
#include "cipher_handler.h"
#include "cps.h"
#include "cdn_stdtypes.h"
#include "cdn_errno.h"

/* Look on: "HDCP mapping to DisplayPort", Rev 2.2, s2.3, p15  */
#define HDCP2X_COMPUTE_L_NON_XOR_BYTES 24U
/* Look on: "HDCP mapping to DisplayPort", Rev 2.2, s2.7, p25  */
#define HDCP2X_KEY_DERIV_NON_XOR_BYTES 8U
/* Look on: "HDCP mapping to DisplayPort", Rev 2.2, s2.2, p12 */
#define HDCP2X_H_SHA256_SIZE 14U
/* Look on: "HDCP mapping to DisplayPort", Rev 2.2, s2.15, , table 2.11, p50 */
#define HDCP_TRANSMITER_VERSION 0x02U
/* Size of derived key */
#define HDCP2X_KD_SIZE 32U
/* Look on: "HDCP mapping to DisplayPort", Rev 2.2, s2.5.2, p23 */
#define HDCP2X_M_SHA256_SIZE 5U

/* Structure used to store transmitter's public key
 * This is a single key, comprised in 2 parts */
typedef struct {
    uint8_t modulusN[HDCP2X_PUB_KEY_MODULUS_N_SIZE];
    uint8_t exponentE[HDCP2X_PUB_KEY_EXPONENT_E_SIZE];
} Hdcp22PublicKey_t;

typedef struct {
    /* Data send via "AKE_Init" command */
    AKE_Init txData;
    /* Data received via "AKE_Send_Cert" command */
    AKE_Send_Cert rxData;
    /* Data send via "LC_Init" command */
    LC_Init lcInit;
    /* Master Key, received from driver (in debug mode) or generated as pseudo-random number */
    uint8_t Km[HDCP2X_EKH_KM_RD_SIZE];
    /* 256b key based on derived keys (dkey0 | dkey1) */
    uint8_t kd[HDCP2X_KD_SIZE];
    /* Session Key, received from driver (in debug mode) or generated as pseudo-random number */
    uint8_t ks[HDCP2X_EDKEY_KS_SIZE];
    /* 64-bit pseudo-random number, could be set as custom with mailbox message */
    uint8_t RIV[HDCP2X_RIV_SIZE];
    /* 'true' if custom keys are used */
    bool useDebugRandomNumbers;
    /* 'true' if Master Key (Km) is custom */
    bool useCustomKmEnc;
    /* Current value of counter. According to specification should be 64b, but FW use value only in range 0-2 */
    uint8_t ctr;
} Hdcp2xEngineData_t;

static Hdcp2xEngineData_t trans2Data;

static Hdcp22PublicKey_t publicKeys;

static void computeL(uint8_t L[HDCP2X_L_TAG_SIZE])
{
    uint8_t i;
    uint8_t sha256Key[SHA256_HASH_SIZE_IN_BYTES];
    uint8_t offset;

    /* sha256Key is (kd[255:192] ^ r_rx[63:0]) | kd[191:0] , "HDCP mapping to DisplayPort", Rev 2.2, p15 */
    for (i = 0U; i < (uint8_t)SHA256_HASH_SIZE_IN_BYTES; i++) {
        sha256Key[i] = trans2Data.kd[i];

        if (i >= (uint8_t)HDCP2X_COMPUTE_L_NON_XOR_BYTES) {
            offset = i - (uint8_t)HDCP2X_COMPUTE_L_NON_XOR_BYTES;
            sha256Key[i] ^=  trans2Data.rxData.r_rx[offset];
        }
    }

    /* sha256Key is now filled, can pass it as key, L as hmacBuffer to sha256_hmac function */
    sha256_hmac(sha256Key, SHA256_HASH_SIZE_IN_BYTES, trans2Data.lcInit.rn, HDCP2X_RN_SIZE, L);
}

static void computeH(uint8_t H[HDCP2X_H_TAG_SIZE])
{
    AKE_Init *txData = &trans2Data.txData;
    uint8_t offset = 0U;
    uint8_t shaInput[HDCP2X_H_SHA256_SIZE];

    /* shaInput will be acquired in 3 copy stages */
    CPS_BufferCopy(&shaInput[offset], txData->r_tx, HDCP2X_RTX_SIZE);

    offset += (uint8_t)HDCP2X_RTX_SIZE;
    CPS_BufferCopy(&shaInput[offset], trans2Data.rxData.rx_caps, HDCP2X_RX_CAPS_SIZE);

    offset += (uint8_t)HDCP2X_RX_CAPS_SIZE;

    CPS_BufferCopy(&shaInput[offset], txData->tx_caps, HDCP2X_TX_CAPS_SIZE);

    /* shaInput is now filled, can pass it as inputBuffer, H as hmacBuffer to sha256_hmac function */
    sha256_hmac(trans2Data.kd, HDCP2X_KD_SIZE, shaInput, HDCP2X_H_SHA256_SIZE, H);
}

/*
 * Used to generate input data for AES-Cipher module : r_tx | (r_rx ^ ctr)
 * Look on: "HDCP mapping to DisplayPort", Rev 2.3, s. 2.7, p. 27
 * @param[out] input, array with input data
 */
static void prepareAesCtrInput(uint8_t input[AES_CRYPT_DATA_SIZE_IN_BYTES])
{
    uint8_t offset = 0U;

    CPS_BufferCopy(&input[offset], trans2Data.txData.r_tx, HDCP2X_RTX_SIZE);

    offset += (uint8_t)HDCP2X_RTX_SIZE;
    CPS_BufferCopy(&input[offset], trans2Data.rxData.r_rx, HDCP2X_RRX_SIZE);

    /* FW uses only ctr from range 0-2, so only last byte is XORed */
    input[AES_CRYPT_DATA_SIZE_IN_BYTES - 1U] ^= trans2Data.ctr;

}

/*
 * Used to generate input data for AES-Cipher module : k_m XOR r_n
 * Look on: "HDCP mapping to DisplayPort", Rev 2.3, s. 2.7, p. 27
 * @param[out] input, array with input data
 */
static void prepareAesCtrKey(uint8_t key[AES_CRYPT_DATA_SIZE_IN_BYTES])
{
    uint8_t i;
    uint8_t index = HDCP2X_RN_SIZE;

    CPS_BufferCopy(key, trans2Data.Km, AES_CRYPT_DATA_SIZE_IN_BYTES);

    /* Km is 16B and Rn is 8B, so need XOR only with last 8B of Km */
    for (i = 0U; i < HDCP2X_RN_SIZE; i++) {
        key[index] ^= trans2Data.lcInit.rn[i];
        index++;
    }
}

/*
 * Used to generate dkey value
 * Look on: "HDCP mapping to DisplayPort", Rev 2.3, s. 2.7, p. 27
 * @param[out] input, array with input data
 */
static void generateDkey(uint8_t dkey[AES_CRYPT_DATA_SIZE_IN_BYTES])
{
    uint8_t input[AES_CRYPT_DATA_SIZE_IN_BYTES];
    uint8_t key[AES_CRYPT_DATA_SIZE_IN_BYTES];

    /* For dkey0 and dkey1 rn must be 0U. Rn is changed after calculations of this values so can be set only once */
    if (trans2Data.ctr == 0U) {
        (void)memset(trans2Data.lcInit.rn, 0, HDCP2X_RN_SIZE);
    }

    prepareAesCtrInput(input);
    prepareAesCtrKey(key);

    aes_setkey(key);
    aes_crypt(input, dkey);

    trans2Data.ctr++;
}

static void calculateEdkeyKs(uint8_t edkeyKs[HDCP2X_EDKEY_KS_SIZE])
{
    uint8_t i;
    uint8_t offset;

    /* Calculate dkey2 */
    generateDkey(edkeyKs);

    /* Edkey(ks) = ks XOR (dkey2 XOR rrx) where rrx is XORed with the least-significant
     * 64-bits of dkey2 */
    for (i = 0U; i < (uint8_t)HDCP2X_EDKEY_KS_SIZE; i++) {
        edkeyKs[i] ^= trans2Data.ks[i];

        if (i >= (uint8_t)HDCP2X_KEY_DERIV_NON_XOR_BYTES) {
            offset = i - (uint8_t)HDCP2X_KEY_DERIV_NON_XOR_BYTES;
            edkeyKs[i] ^= trans2Data.rxData.r_rx[offset];
        }
    }
}

void ENG2T_set_AKE_Init(uint8_t* const buffer)
{
    AKE_Init* txData = &trans2Data.txData;

    if (!trans2Data.useDebugRandomNumbers) {
        UTIL_FillRandomNumber(txData->r_tx, (uint8_t)HDCP2X_RTX_SIZE);
    }

    /* Fill TxCaps data, looks Table 2.11 in specification */
    txData->tx_caps[0] = (uint8_t)HDCP_TRANSMITER_VERSION;
    txData->tx_caps[1] = 0U;
    txData->tx_caps[2] = 0U;

    /* Copy transmitter data into "AKE_Init" message buffer */
    CPS_BufferCopy(&buffer[offsetof(AKE_Init, r_tx)], txData->r_tx, HDCP2X_RTX_SIZE);
    CPS_BufferCopy(&buffer[offsetof(AKE_Init, tx_caps)], txData->tx_caps, HDCP2X_TX_CAPS_SIZE);

    /* Look on: HDCP mapping to DisplayPort", Rev 2.3, s. 2.7, p. 27 */
    trans2Data.ctr = 0U;
}

/** Used to extract CertRx data from byte buffer into structure */
static void CertRxCopy(const uint8_t* const buffer) {

    /* Base address in received data */
    const uint8_t* address = &buffer[offsetof(AKE_Send_Cert, cert_rx)];

    /* Base address of structure */
    CertRx_t* cert_rx = &trans2Data.rxData.cert_rx;

    /* Copy data into structure field and update address */
    CPS_BufferCopy(cert_rx->receiver_id, address, HDCP2X_CERTRX_REC_ID_SIZE);
    address = &address[HDCP2X_CERTRX_REC_ID_SIZE];

    CPS_BufferCopy(cert_rx->modulus_n, address, HDCP2X_CERTRX_MODULUS_N_SIZE);
    address = &address[HDCP2X_CERTRX_MODULUS_N_SIZE];

    CPS_BufferCopy(cert_rx->exponent_e, address, HDCP2X_CERTRX_EXPONENT_E_SIZE);
    address = &address[HDCP2X_CERTRX_EXPONENT_E_SIZE];

    CPS_BufferCopy(cert_rx->reserved, address, HDCP2X_CERTRX_RESERVED_SIZE);
    address = &address[HDCP2X_CERTRX_RESERVED_SIZE];

    CPS_BufferCopy(cert_rx->dcp_dll_signature, address, HDCP2X_CERTRX_DCP_LLC_SIG_SIZE);
}

void ENG2T_get_AKE_Send_Cert(const uint8_t* const buffer)
{
    AKE_Send_Cert* rxData = &trans2Data.rxData;

    /* Read receiver data from "AKE_Send_Cert" message buffer */
    CPS_BufferCopy(rxData->r_rx, &buffer[offsetof(AKE_Send_Cert, r_rx)], HDCP2X_RRX_SIZE);
    CPS_BufferCopy(rxData->rx_caps, &buffer[offsetof(AKE_Send_Cert, rx_caps)], HDCP2X_RX_CAPS_SIZE);
    CertRxCopy(buffer);
}

HdcpDevType_t ENG2T_getDeviceTypeFromRxCaps(void)
{
    /* RX_CAPS_REPEATER bit is in third byte */
    return ((trans2Data.rxData.rx_caps[2] & (uint8_t)HDCP2X_RXCAPS_REPEATER_MASK) != 0U) ? DEV_HDCP_REPEATER : DEV_HDCP_RECEIVER;
}

/**
 * Get data received via "RepeaterAuth_Send_ReceiverID_List" message
 * @param[in] buffer, buffer with data from message
 * @param[out] rxInfo, value of rxInfo register
 * @param[out] seq_num_V, pointer to value of seq_num_V parameter
 * @param[out] ksv_list, pointer to list with IDs
 */
void ENG2T_get_receiverIdList(const uint8_t* const buffer, uint16_t* const rxInfo,
        uint32_t* const seq_num_V, const uint8_t* ksv_list)
{
    *rxInfo = getBe16(&buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, RxInfo)]);
    *seq_num_V = getBe32(&buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, seq_num_V)]);
    ksv_list = &buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, ksv_list)];
}

void ENG2T_set_AKE_Stored_km(uint8_t* const buffer, const uint8_t* const message)
{
    /* Copy M */
    CPS_BufferCopy(&buffer[offsetof(AKE_Stored_km, m)], &message[offsetof(HdcpTransactionPairingData_t, m)], HDCP2X_M_SIZE);

    /* Copy KM */
    CPS_BufferCopy(trans2Data.Km, &message[offsetof(HdcpTransactionPairingData_t, Km)], HDCP2X_EKH_KM_RD_SIZE);

    /* Copy EKH_KM */
    CPS_BufferCopy(&buffer[offsetof(AKE_Stored_km, ekh_km)], &message[offsetof(HdcpTransactionPairingData_t, Ekh)], HDCP2X_EKH_KM_RD_SIZE);
}

bool ENG2T_valid_H(const uint8_t* const buffer)
{
    //we get h' calculated by receiver, now need to calculate it in transmitter and compare
    //perform key derivation

    uint8_t H[HDCP2X_H_TAG_SIZE];

    /* Generate dkey0 */
    generateDkey(trans2Data.kd);

    /* Generate dkey1 */
    generateDkey(&trans2Data.kd[AES_CRYPT_DATA_SIZE_IN_BYTES]);

    computeH(H);

    return if_buffers_equal(H, buffer, HDCP2X_H_TAG_SIZE);
}

/**
 * Get data received via "AKE_Send_Pairing_Info" command
 * @param[in] buffer, pairing information (E_kh_km) from receiver
 * @param[out] pairingData, buffer to storage pairingData, used to send it to the host via mailbox
 */
void ENG2T_AKE_Send_Pairing_Info(const uint8_t* const buffer,
        HdcpTransactionPairingData_t* const pairingData)
{
    CPS_BufferCopy(pairingData->Ekh, &buffer[offsetof(AKE_Send_Pairing_Info, ekh_km)], HDCP2X_EKH_KM_RD_SIZE);

    /* RTX and RRX go one after another to the same buffer */
    CPS_BufferCopy(&pairingData->m[0], trans2Data.txData.r_tx, HDCP2X_RTX_SIZE);
    CPS_BufferCopy(&pairingData->m[HDCP2X_RTX_SIZE], trans2Data.rxData.r_rx, HDCP2X_RRX_SIZE);

    CPS_BufferCopy(pairingData->Km, trans2Data.Km, HDCP2X_EKH_KM_RD_SIZE);

    CPS_BufferCopy(pairingData->receiverId, trans2Data.rxData.cert_rx.receiver_id, HDCP_REC_ID_SIZE);
}

void ENG2T_set_LC_Init(uint8_t* const buffer)
{
    if (!trans2Data.useDebugRandomNumbers) {
        UTIL_FillRandomNumber(trans2Data.lcInit.rn, (uint8_t)HDCP2X_RN_SIZE);
    }

    /* Copy data into "LC_Init" message buffer */
    CPS_BufferCopy(&buffer[offsetof(LC_Init, rn)], trans2Data.lcInit.rn, HDCP2X_RN_SIZE);
}

void ENG2T_set_SKE_Send_Eks(uint8_t* const buffer, HdcpContentStreamType_t contentType)
{
    uint8_t offset = 0U;
    uint8_t aesKey[HDCP2X_EDKEY_KS_SIZE];
    uint8_t i;

    //generate KS and RIV
    if (!trans2Data.useDebugRandomNumbers) {
        UTIL_FillRandomNumber(trans2Data.ks, (uint8_t)HDCP2X_EDKEY_KS_SIZE);
        UTIL_FillRandomNumber(trans2Data.RIV, (uint8_t)HDCP2X_RIV_SIZE);
    }

    /* Prepare "SKE_Send_Eks" message */
    calculateEdkeyKs(&buffer[offsetof(SKE_Send_Eks, Edkey_Ks)]);
    CPS_BufferCopy(&buffer[offsetof(SKE_Send_Eks, Riv)], trans2Data.RIV, HDCP2X_RIV_SIZE);

    /* Prepare session key (KS ^ lc128) */
    for (i = 0; i< (uint8_t)HDCP2X_EDKEY_KS_SIZE; i++) {
        aesKey[i] = trans2Data.ks[i] ^ pHdcpLc128[i];
    }

    CIPHER_StartAuthenticated(aesKey, trans2Data.RIV, (uint8_t)contentType);
}

/**
 * Prepare "RepeaterAuth_Stream_Manage" command
 * @param[out] buffer, buffer to be filled with command data
 * @param[in] seq_num_M, current value of seq_num_M
 * @param[in] contentType, stream content type
 */
void ENG2T_RA_Stream_Manage(uint8_t* const buffer, uint32_t seq_num_M,
        HdcpContentStreamType_t contentType)
{
    size_t offset = offsetof(RepeaterAuth_Stream_Manage, k);
    buffer[offset] = 0U;
    buffer[offset + 1U] = 1U;

    /* seq_num_M is HDCP2X_SEQ_NUM_M_SIZE bytes */
    offset = offsetof(RepeaterAuth_Stream_Manage, seq_num_M);
    buffer[offset] = GetByte2(seq_num_M);
    buffer[offset + 1U] = GetByte1(seq_num_M);
    buffer[offset + 2U] = GetByte0(seq_num_M);

    offset = offsetof(RepeaterAuth_Stream_Manage, StreamId_Type);
    buffer[offset] = 0U;
    buffer[offset + 1U] = (uint8_t)contentType;
}

bool ENG2T_verify_receiverIdList(uint8_t* const buffer, uint8_t* ackVal)
{
    bool result;
    uint8_t v_res[2 * HDCP2X_V_SIZE];
    uint16_t rxInfo = getBe16(&buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, RxInfo)]);
    uint8_t* const ksv_list = &buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, ksv_list)];

    /* Each device need 5B for storage information in receiverList */
    uint16_t devIndex = ((rxInfo & (uint16_t)RX_INFO_DEVICE_COUNT_MASK) >> RX_INFO_DEVICE_COUNT_OFFSET) * 5U;

    /* V' (or V) = HMAC-SHA256(Receiver ID list | RxInfo | seq_num_V, kd) */

    /* Put RxInfo in ksv_list */
    ksv_list[devIndex] = GetByte1(rxInfo);
    devIndex++;

    ksv_list[devIndex] = GetByte0(rxInfo);
    devIndex++;

    CPS_BufferCopy(&ksv_list[devIndex], &buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, seq_num_V)], HDCP2X_SEQ_NUM_V_SIZE);
    devIndex += (uint16_t)HDCP2X_SEQ_NUM_V_SIZE;

    sha256_hmac(trans2Data.kd, HDCP2X_KD_SIZE, ksv_list, devIndex , v_res);

    result = if_buffers_equal(&buffer[offsetof(RepeaterAuth_Send_ReceiverID_List, V)], v_res, HDCP2X_V_SIZE);

    if (result) {
        CPS_BufferCopy(ackVal, &v_res[HDCP2X_V_SIZE], HDCP2X_V_SIZE);
    }

    return result;
}

/**
 * Set HDCP 2.2 private keys (N,E)
 * @param[in] N, module parameter for TX public key
 * @param[in] E, exponent parameter for TX public key
 */
void ENG2T_SetKey(uint8_t const * const N, uint8_t const * const E)
{
    CPS_BufferCopy(publicKeys.modulusN, N, (uint32_t)HDCP2X_PUB_KEY_MODULUS_N_SIZE);
    CPS_BufferCopy(publicKeys.exponentE, E, (uint32_t)HDCP2X_PUB_KEY_EXPONENT_E_SIZE);
    trans2Data.useDebugRandomNumbers = false;
}

/**
 * Set random numbers for debugging, if predefined numbers are preferred to random ones
 * @param[in] buffer, pointer to buffer with received keys
 * @param[in] onlyKm, set to true for copying only the km part of the buffer
 */
void ENG2T_SetDebugRandomNumbers(const uint8_t* const buffer, bool onlyKm)
{
    const uint8_t* l_buffer = buffer;
    /* Copy the beginning part of the buffer to Km */
    CPS_BufferCopy(trans2Data.Km, l_buffer, HDCP2X_EKH_KM_RD_SIZE);

    /* Copy next sections of the buffer into other buffers in trans2Data structure */
    if (!onlyKm) { /* but only if requested to copy not only to Km */
        l_buffer = &buffer[HDCP2X_EKH_KM_RD_SIZE];

        CPS_BufferCopy(trans2Data.lcInit.rn, l_buffer, HDCP2X_RN_SIZE);
        l_buffer = &buffer[HDCP2X_RN_SIZE];

        CPS_BufferCopy(trans2Data.ks, l_buffer, HDCP2X_EDKEY_KS_SIZE);
        l_buffer = &buffer[HDCP2X_EDKEY_KS_SIZE];

        CPS_BufferCopy(trans2Data.RIV, l_buffer, HDCP2X_RIV_SIZE);
        l_buffer = &buffer[HDCP2X_RIV_SIZE];

        CPS_BufferCopy(trans2Data.txData.r_tx, l_buffer, HDCP2X_RTX_SIZE);

        trans2Data.useDebugRandomNumbers = true;
    }

    trans2Data.useCustomKmEnc = true;
}

bool ENG_ENG2T_valid_L(const uint8_t* const buffer)
{
    uint8_t L[HDCP2X_L_TAG_SIZE];

    computeL(L);
    return if_buffers_equal(L, buffer, HDCP2X_L_TAG_SIZE);
}

/**
 * Get receiver ID from "sCertRx"
 * @param[out] receiverId, ID of receiver
 */
void ENG2T_GetReceiverId(uint8_t* const receiverId)
{
    CPS_BufferCopy(receiverId, trans2Data.rxData.cert_rx.receiver_id, HDCP_REC_ID_SIZE);
}

bool ENG2T_verify_streamAuth(const uint8_t* const buffer, HdcpContentStreamType_t StreamID_Type,
        const uint8_t* const seqNumM)
{
    // M' (or M) = HMAC-SHA256(StreamID_Type | seq_num_M, SHA256(kd)).
    uint8_t shaInput[HDCP2X_M_SHA256_SIZE];
    uint8_t sha_res[HDCP2X_M_TAG_SIZE];
    uint8_t key[HDCP2X_M_TAG_SIZE];

    sha256(trans2Data.kd, HDCP2X_KD_SIZE, key);

    shaInput[0] = 0U;
    shaInput[1] = (uint8_t)StreamID_Type;

    shaInput[2] = seqNumM[0];
    shaInput[3] = seqNumM[1];
    shaInput[4] = seqNumM[2];

    sha256_hmac(key, HDCP2X_KD_SIZE, shaInput, HDCP2X_M_SHA256_SIZE, sha_res);

    return if_buffers_equal(&buffer[offsetof(RepeaterAuth_Stream_Ready, M)], sha_res, HDCP2X_M_TAG_SIZE);
}

/**
 * Check receiver certificate signature
 * @return CDN_EOK if signature is valid
 * @return CDN_EINVAL if signature is not valid
 * @return CDN_EINPROGRESS if operation is processed
 */
uint32_t ENG2T_valid_cert_signature(void)
{
    uint32_t retVal;
    uint16_t offset;

    static uint8_t shaOutput[SHA256_HASH_SIZE_IN_BYTES];
    static PkcsParam_t pkcs_params_sig;
    static uint8_t key_from_signature[HDCP2X_PUB_KEY_MODULUS_N_SIZE];

    if (lib_handler.rsaRxstate == 0U) {
        LIB_HANDLER_Clean();
        lib_handler.rsaRxstate = 1U;
    }

    if (lib_handler.rsa_index == 0U) {
         /* Host calculates SHA for receiver certification */

        /* Hash all data from cert_rx except signature */
        offset = HDCP2X_CERTRX_SIZE - HDCP2X_CERTRX_DCP_LLC_SIG_SIZE;
        sha256(trans2Data.rxData.cert_rx.receiver_id, (uint32_t)offset, shaOutput);

        set_pkcs_parameter(&pkcs_params_sig.input, trans2Data.rxData.cert_rx.dcp_dll_signature, HDCP2X_PUB_KEY_MODULUS_N_SIZE);
        set_pkcs_parameter(&pkcs_params_sig.output, key_from_signature, HDCP2X_PUB_KEY_MODULUS_N_SIZE);
        set_pkcs_parameter(&pkcs_params_sig.modulus_n, publicKeys.modulusN, HDCP2X_PUB_KEY_MODULUS_N_SIZE);
        set_pkcs_parameter(&pkcs_params_sig.exponent_e, publicKeys.exponentE, HDCP2X_PUB_KEY_EXPONENT_E_SIZE);
    }

    retVal = pkcs1_v15_rsassa_verify(&pkcs_params_sig, shaOutput);

    if (retVal != CDN_EINPROGRESS) {
        /* Cleanup state if finish or error */
        lib_handler.rsaRxstate = 0U;
    }

    return retVal;
}

/**
 * Prepare "AKE_No_Stored_km" command
 * @param[out] buffer, buffer to be filled with command data
 * @return CDN_EOK if success
 * @return CDN_EINPROGRESS if operation is in progress
 * @return CDN_EINVAL if invalid parameters passed
 */
uint32_t ENG2T_set_AKE_No_Stored_km(uint8_t* const buffer)
{
    static PkcsParam_t pkcs_params_km;
    static AKE_No_Stored_km AkeNoStored;

    CertRx_t* cert_rx = &trans2Data.rxData.cert_rx;

    uint32_t retVal;

    if (lib_handler.rsaRxstate == 0U) {
        LIB_HANDLER_Clean();
        lib_handler.rsaRxstate = 1U;
        // generate KM
        if (!trans2Data.useDebugRandomNumbers) {
            UTIL_FillRandomNumber(trans2Data.Km, (uint8_t)HDCP2X_EKH_KM_RD_SIZE);
        }
    }

    if (lib_handler.rsa_index == 0U) {
        set_pkcs_parameter(&pkcs_params_km.input, trans2Data.Km, HDCP2X_EKH_KM_RD_SIZE);
        set_pkcs_parameter(&pkcs_params_km.output, AkeNoStored.ekpub_km, HDCP2X_EKPUB_KM_SIZE);
        set_pkcs_parameter(&pkcs_params_km.modulus_n, cert_rx->modulus_n, HDCP2X_CERTRX_MODULUS_N_SIZE);
        set_pkcs_parameter(&pkcs_params_km.exponent_e, cert_rx->exponent_e, HDCP2X_CERTRX_EXPONENT_E_SIZE);
    }

    retVal = pkcs1_rsaes_oaep_encrypt(&pkcs_params_km);

    if (retVal == CDN_EOK) {
        /* If process was finished without errors, copy data into buffer. Used to avoid writing data to external
         * buffer during process */
        CPS_BufferCopy(&buffer[offsetof(AKE_No_Stored_km, ekpub_km)], AkeNoStored.ekpub_km, HDCP2X_EKPUB_KM_SIZE);
    }

    if (retVal != CDN_EINPROGRESS) {
        /* Cleanup state if finish or error */
        lib_handler.rsaRxstate = 0U;
    }

    return retVal;
}

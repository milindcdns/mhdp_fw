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
 * engine2T.h
 *
 ******************************************************************************
 */

#ifndef ENGINE2T_H
#define ENGINE2T_H

#include "engine.h"
#include "hdcp_tran.h"
#include "hdcp2.h"
#include "cdn_stdtypes.h"

/* Structure used to pack pairing data */
typedef struct {
    uint8_t receiverId[HDCP_REC_ID_SIZE];
    uint8_t m[HDCP2X_M_SIZE];
    uint8_t Km[HDCP2X_EKH_KM_RD_SIZE];
    uint8_t Ekh[HDCP2X_EKH_KM_RD_SIZE];
} HdcpTransactionPairingData_t;

/* Size of pairing data */
#define HDCP2X_PAIRING_DATA_SIZE 53U
/* Size of "Modulus N" public key in bytes */
#define HDCP2X_PUB_KEY_MODULUS_N_SIZE  384U
/* Size of "Exponent E" public key in bytes */
#define HDCP2X_PUB_KEY_EXPONENT_E_SIZE 3U
/* Value of internal counter when Dkey2 is computed */
#define HDCP2X_DKEY2_COUNTER_VALUE 2U

/**
 * Prepare "AKE_Init" command
 * @param[out] buffer, buffer to be filled with command data
 */
void ENG2T_set_AKE_Init(uint8_t* const buffer);

/**
 * Get data received via "AKE_Send_Cert" command
 * @param[out] buffer, buffer to be filled with command data
 */
void ENG2T_get_AKE_Send_Cert(const uint8_t* const buffer);

/**
 * Prepare "AKE_No_Stored_km" command
 * @param[out] buffer, buffer to be filled with command data
 * @return CDN_EOK if success
 * @return CDN_EINPROGRESS if operation is in progress
 * @return CDN_EINVAL if invalid parameters passed
 */
uint32_t ENG2T_set_AKE_No_Stored_km(uint8_t* const buffer);

/**
 * Prepare "AKE_Stored_km" command
 * @param[out] buffer, buffer to be filled with command data
 * @param[in] message, command data received from host
 */
void ENG2T_set_AKE_Stored_km(uint8_t* const buffer, const uint8_t* const message);

/**
 * Compare H and H' (H' received via "AKE_Send_H_prime" command)
 * @param[in] buffer, received H'
 * @return 'true' if H equals H' or 'false' if not
 */
bool ENG2T_valid_H(const uint8_t* const buffer);

/**
 * Get data received via "AKE_Send_Pairing_Info" command
 * @param[in] buffer, pairing information (E_kh_km) from receiver
 * @param[out] pairingData, buffer to storage pairingData, used to send it to the host via mailbox
 */
void ENG2T_AKE_Send_Pairing_Info(const uint8_t* const buffer,
        HdcpTransactionPairingData_t* const pairingData);

/**
 * Check receiver certificate signature
 * @return CDN_EOK if signature is valid
 * @return CDN_EINVAL if signature is not valid
 * @return CDN_EINPROGRESS if operation is processed
 */
uint32_t ENG2T_valid_cert_signature(void);

/**
 * Prepare "LC_Init" command
 * @param[out] buffer, buffer to be filled with command data
 */

void ENG2T_set_LC_Init(uint8_t* const buffer);

/**
 * Prepare "SKE_Send_Eks" command
 * @param[out] buffer, buffer to be filled with command data
 * @param[in] contentType, stream content type
 */
void ENG2T_set_SKE_Send_Eks(uint8_t* const buffer, HdcpContentStreamType_t contentType);

/**
 * Compare L (transmitter) and L' (receiver)
 * @param[in] buffer, L' received via "LC_Send_L_prime" command
 * @return 'true' if L equals L' or 'false' if not
 */
bool ENG_ENG2T_valid_L(const uint8_t* const buffer);

/**
 * Prepare "RepeaterAuth_Stream_Manage" command
 * @param[out] buffer, buffer to be filled with command data
 * @param[in] seq_num_M, current value of seq_num_M
 * @param[in] contentType, stream content type
 */
void ENG2T_RA_Stream_Manage(uint8_t* const buffer, uint32_t seq_num_M,
        HdcpContentStreamType_t contentType);

/**
 * Set HDCP 2.2 private keys (N,E)
 * @param[in] N, module parameter for TX public key
 * @param[in] E, exponent parameter for TX public key
 */
void ENG2T_SetKey(uint8_t const * const N, uint8_t const * const E);

/**
 * Set random numbers for debugging, if predefined numbers are preferred to random ones
 * @param[in] buffer, pointer to buffer with received keys
 * @param[in] onlyKm, set to true for copying only the km part of the buffer
 */
void ENG2T_SetDebugRandomNumbers(const uint8_t* const buffer, bool onlyKm);

/**
 * Get receiver ID from "sCertRx"
 * @param[out] receiverId, ID of receiver
 */
void ENG2T_GetReceiverId(uint8_t* const receiverId);

/**
 * Check if receiver ID list received via "RepeaterAuth_Send_ReceiverID_List" command is valid.
 * If it is, ackVal is filled with correct V value
 * @param[in] buffer, buffer with data from command
 * @param[out] ackVal, buffer to store V parameter value
 * @return 'true' if V equals V' or 'false' if not
 */
bool ENG2T_verify_receiverIdList(uint8_t* const buffer, uint8_t* ackVal);

/**
 * Check if M' parameter received via "RepeaterAuth_Stream_Ready" command is valid.
 * @param[in] buffer, buffer with data from command
 * @param[in] StreamID_Type, stream content type
 * @param[in] seqNumM, current value of seq_num_M
 * @return 'true' if M equals M' or 'false' if not
 */
bool ENG2T_verify_streamAuth(const uint8_t* const buffer, HdcpContentStreamType_t StreamID_Type,
        const uint8_t* const seqNumM);

/**
 * Get data received via "RepeaterAuth_Send_ReceiverID_List" message
 * @param[in] buffer, buffer with data from message
 * @param[out] rxInfo, value of rxInfo register
 * @param[out] seq_num_V, value of seq_num_V parameter
 * @param[out] ksv_list, pointer to list with IDs
 */
void ENG2T_get_receiverIdList(const uint8_t* const buffer, uint16_t* const rxInfo,
        uint32_t* const seq_num_V, const uint8_t* ksv_list);

/**
 * Return device type from RxCaps stored in TX memory
 */
HdcpDevType_t ENG2T_getDeviceTypeFromRxCaps(void);

#endif /* ENGINE2T_H */

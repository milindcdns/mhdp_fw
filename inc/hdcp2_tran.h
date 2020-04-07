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
 * hdcp2_tran.h
 *
 ******************************************************************************
 */

#ifndef HDCP_2_TRAN_H
#define HDCP_2_TRAN_H

#include "cdn_stdtypes.h"
#include "engine.h"
#include "engine2T.h"

/* Maximum number of locality check attempts ("Mapping HDCP to DisplayPort", Rev 2.2, p. 16) */
#define HDCP2X_LOCALITY_CHECK_MAX_ATTEMPS 1024U
/* Timeout of AKE_Send message reading, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 13) */
#define HDCP2X_AKE_SEND_READ_TIMEOUT_MS 110U
/* Maximum time of waiting for H'_AVAILABLE flag when AKE_No_Stored_km used,
   ("Mapping HDCP to DisplayPort", Rev 2.3, p. 14) */
#define HDCP2X_H_PRIME_AVAILABLE_NO_STORED_TIMEOUT_MS 1000U
/* Maximum time of waiting for H'_AVAILABLE flag when AKE_Stored_km used,
   ("Mapping HDCP to DisplayPort", Rev 2.3, p. 14) */
#define HDCP2X_H_PRIME_AVAILABLE_STORED_TIMEOUT_MS 200U
/* Timeout of AKE_Send_H_prime reading, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 14) */
#define HDCP2X_H_PRIME_READ_TIMEOUT_MS 7U
/* Maximum time if waiting for PAIRING_AVAILABLE flag ("Mapping HDCP to DisplayPort", Rev 2.3, p. 16) */
#define HDCP2X_PAIRING_AVAILABLE_TIMEOUT_MS 200U
/* Timeout of AKE_Send_Pairing_Info reading, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 16) */
#define HDCP2X_PAIRING_READ_TIMEOUT_MS 5U
/* Timeout of AKE_LC_Send_L_prime reading, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 17) */
#define HDCP2X_LC_SEND_L_PRIME_READ_TIMEOUT_MS 16U
/* Latency of start encryption after send content type if receiver, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 18) */
#define HDCP2X_RCV_ENCRYPTION_START_LATENCY_MS 200U
#define HDCP2X_ENCRYPTION_START_LATENCY_FAST_MS 1U
/* Maximum time of waiting for READY flag, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 21) */
#define HDCP2X_READY_TIMEOUT 3000U
/* Maximum time of waiting for RepeaterAuth_Stream_Ready message */
#define HDCP2X_AUTH_STREAM_READY_TIMEOUT_MS 100U
/* Latency of start encryption after send content type if repeater, ("Mapping HDCP to DisplayPort", Rev 2.3, p. 24) */
#define HDCP2X_REP_ENCRYPTION_START_LATENCY_MS 110U
#define HDCP2X_NO_TIMEOUT 0U

typedef struct {
    uint32_t V;
    uint32_t M;
} SeqNum_t;

typedef struct {
    /* Callback to next state function */
    StateCallback_t cb;
    /* Address of HDCP TX/RX buffer */
    uint8_t* buffer;
    /* HDCP v2.x module status */
    uint16_t status;
    /* Type of device */
    HdcpDevType_t devType;
    /* Flag indicating that pairing is needed, used only when device is receiver */
    bool isPairingNeeded;
    /* Current pairing data */
    HdcpTransactionPairingData_t pairingData;
    /* Used to store current seq_num values */
    SeqNum_t seqNum;
    /* 'true' if any Receiver ID list was already verified after AKE_INIT */
    bool receiverIdListVerif;

} Hdcp2TData_t;

 /**
 * Initialization function for HDCP v2.2
 */
void HDCP2X_TRAN_init(void);

/**
 * State machine for HDCP v2.2
 */
void HDCP2X_TRAN_handleSM(void);

/**
 * Get pairing data of HDCP v2.2
 * @param[in/out] buffer, pointer to buffer where data will be copied
 */
void HDCP2X_getPairingData(uint8_t* buffer);

#endif /* HDCP_2_TRAN_H */

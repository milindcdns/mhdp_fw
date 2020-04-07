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
 * hdcp2.h
 *
 ******************************************************************************
 */

#ifndef HDCP2_H
#define HDCP2_H

#include "mailBox.h"
#include "cdn_stdtypes.h"

/* Mask for DPCD register: RxCaps[0] - REPEATER */
#define HDCP2X_RXCAPS_REPEATER_MASK 0x01U
/* Mask for DPCD register: RxCaps[1] - HDCP_CAPABLE */
#define HDCP2X_RXCAPS_IS_CAPABLE_MASK 0x02U

/* defines for HDCP RxInfo register */
#define RX_INFO_MAX_CASCADE_EXCEEDED_MASK  0x0004U
#define RX_INFO_MAX_DEVS_EXCEEDED_MASK     0x0008U
#define RX_INFO_DEVICE_COUNT_MASK          0x01F0U
#define RX_INFO_DEVICE_COUNT_OFFSET        4U

#define HDCP2X_RTX_ADDRESS               0x69000U
#define HDCP2X_RTX_SIZE                  8U

#define HDCP2X_TX_CAPS_ADDRESS           0x69008U
#define HDCP2X_TX_CAPS_SIZE              3U

#define HDCP2X_CERTRX_ADDRESS            0x6900BU
#define HDCP2X_CERTRX_SIZE               522U

#define HDCP2X_RRX_ADDRESS               0x69215U
#define HDCP2X_RRX_SIZE                  8U

#define HDCP2X_RX_CAPS_ADDRESS           0x6921DU
#define HDCP2X_RX_CAPS_SIZE              3U

#define HDCP2X_EKPUB_KM_ADDRESS          0x69220U
#define HDCP2X_EKPUB_KM_SIZE             128U

#define HDCP2X_EKH_KM_WR_ADDRESS         0x692A0U
#define HDCP2X_EKH_KM_WR_SIZE            16U

#define HDCP2X_M_ADDRESS                 0x692B0U
#define HDCP2X_M_SIZE                    16U

#define HDCP2X_H_TAG_ADDRESS             0x692C0U
#define HDCP2X_H_TAG_SIZE                32U

#define HDCP2X_EKH_KM_RD_ADDRESS         0x692E0U
#define HDCP2X_EKH_KM_RD_SIZE            16U

#define HDCP2X_RN_ADDRESS                0x692F0U
#define HDCP2X_RN_SIZE                   8U

#define HDCP2X_L_TAG_ADDRESS             0x692F8U
#define HDCP2X_L_TAG_SIZE                32U

#define HDCP2X_EDKEY_KS_ADDRESS          0x69318U
#define HDCP2X_EDKEY_KS_SIZE             16U

#define HDCP2X_RIV_ADDRESS               0x69328U
#define HDCP2X_RIV_SIZE                  8U

#define HDCP2X_RX_INFO_ADDRESS           0x69330U
#define HDCP2X_RX_INFO_SIZE              2U

#define HDCP2X_SEQ_NUM_V_ADDRESS         0x69332U
#define HDCP2X_SEQ_NUM_V_SIZE            3U

#define HDCP2X_V_TAG_ADDRESS             0x69335U
#define HDCP2X_V_TAG_SIZE                16U

#define HDCP2X_REC_ID_LIST_ADDRESS  0x69345U
#define HDCP2X_REC_ID_LIST_SIZE     155U

#define HDCP2X_V_ADDRESS                 0x693E0U
#define HDCP2X_V_SIZE                    16U

#define HDCP2X_SEQ_NUM_M_ADDRESS         0x693F0U
#define HDCP2X_SEQ_NUM_M_SIZE            3U

#define HDCP2X_K_ADDRESS                 0x693F3U
#define HDCP2X_K_SIZE                    2U

#define HDCP2X_STREAM_ID_TYPE_ADDRESS    0x693F5U
#define HDCP2X_STREAM_ID_TYPE_SIZE       126U

#define HDCP2X_M_TAG_ADDRESS             0x69473U
#define HDCP2X_M_TAG_SIZE                32U

#define HDCP2X_RXSTATUS_ADDRESS          0x69493U
#define HDCP2X_RXSTATUS_SIZE             1U

/* Mask for DPCD register : RxStatus[0] - READY */
#define HDCP2X_RXSTATUS_READY_MASK      0x01U
/* Mask for DPCD register : RxStatus[1] - H'AVAILABLE */
#define HDCP2X_RXSTATUS_HAVAILABLE_MASK 0x02U
/* Mask for DPCD register : RxStatus[2] - PAIRING_AVAILABLE */
#define HDCP2X_RXSTATUS_PAIRING_AV_MASK 0x04U

#define HDCP2X_RXSTATUS_LINK_AUTH_MASK 0x08U
#define HDCP2X_RXSTATUS_REAUTH_MASK 0x10U

#define HDCP2X_TYPE_ADDRESS              0x69494U
#define HDCP2X_TYPE_SIZE                 1U

#define HDCP2X_RSVD_ADDRESS              0x69495U
#define HDCP2X_RSVD_SIZE                 131U

#define HDCP2X_DBG_ADDRESS               0x69518U
#define HDCP2X_DBG_SIZE                  64U

// HDCP2X commands
typedef enum {
    HDCP2X_CMD_AKE_INIT,
    HDCP2X_CMD_AKE_SEND_CERT,
    HDCP2X_CMD_AKE_NO_STORED_KM,
    HDCP2X_CMD_AKE_STORED_KM,
    HDCP2X_CMD_AKE_SEND_H_PRIME,
    HDCP2X_CMD_AKE_SEND_PAIRING_INFO,
    HDCP2X_CMD_LC_INIT,
    HDCP2X_CMD_LC_SEND_L_PRIME,
    HDCP2X_CMD_SKE_SEND_EKS,
    HDCP2X_CMD_RPTR_AUTH_SEND_ACK,
    HDCP2X_CMD_RPTR_AUTH_SEND_RECEIVER_ID_LIST,
    HDCP2X_CMD_RPTR_AUTH_STREAM_MG,
    HDCP2X_CMD_RPTR_AUTH_STREAM_READY,
} Hdcp2MsgId_t;

/* Look on: "HDCP mapping to DisplayPort", Rev 2.3, s2.1, p11 */
#define HDCP2X_CERTRX_REC_ID_SIZE 5U
#define HDCP2X_CERTRX_MODULUS_N_SIZE 128U
#define HDCP2X_CERTRX_EXPONENT_E_SIZE 3U
#define HDCP2X_CERTRX_RESERVED_SIZE 2U
#define HDCP2X_CERTRX_DCP_LLC_SIG_SIZE 384U

#define LC_128_LEN 16
extern unsigned char pHdcpLc128[LC_128_LEN];

typedef struct {
    /* Unique receiver ID, contains 20 ones and 20 zeroes */
    uint8_t receiver_id[HDCP2X_CERTRX_REC_ID_SIZE];
    /* Unique RSA public key of HDCP Receiver (modulus_n | exponent_e)*/
    uint8_t modulus_n[HDCP2X_CERTRX_MODULUS_N_SIZE];
    uint8_t exponent_e[HDCP2X_CERTRX_EXPONENT_E_SIZE];
    /* Reserved for future definition */
    uint8_t reserved[HDCP2X_CERTRX_RESERVED_SIZE];
    /* A cryptographic signature */
    uint8_t dcp_dll_signature[HDCP2X_CERTRX_DCP_LLC_SIG_SIZE];
} CertRx_t;

/* Structs for commands */

typedef struct {
    /* Pseudo-random number */
    uint8_t r_tx[HDCP2X_RTX_SIZE];
    /* Capabilities of transmitter */
    uint8_t tx_caps[HDCP2X_TX_CAPS_SIZE];
} AKE_Init;

typedef struct {
    /* Signature verification data */
    CertRx_t cert_rx;
    /* Pseudo-random number */
    uint8_t r_rx[HDCP2X_RRX_SIZE];
    /* Capabilities of receiver */
    uint8_t rx_caps[HDCP2X_RX_CAPS_SIZE];
} AKE_Send_Cert;

typedef struct {
    uint8_t    ekpub_km[HDCP2X_EKPUB_KM_SIZE];
} AKE_No_Stored_km;

typedef struct {
    uint8_t    ekh_km[HDCP2X_EKH_KM_RD_SIZE];
    uint8_t    m[HDCP2X_M_SIZE];
} AKE_Stored_km;

typedef struct {
    uint8_t    h[HDCP2X_H_TAG_SIZE];
} is_H_prime_valid;

typedef struct {
    uint8_t    ekh_km[HDCP2X_EKH_KM_RD_SIZE];
} AKE_Send_Pairing_Info;

typedef struct {
    /* Pseudo-random number */
    uint8_t    rn[HDCP2X_RN_SIZE];
} LC_Init;

typedef struct {
    uint8_t    Edkey_Ks[HDCP2X_EDKEY_KS_SIZE];
    uint8_t    Riv[HDCP2X_RIV_SIZE];
} SKE_Send_Eks;

typedef struct {
    uint8_t    RxInfo[HDCP2X_RX_INFO_SIZE];
    uint8_t    seq_num_V[HDCP2X_SEQ_NUM_V_SIZE];
    uint8_t    V[HDCP2X_V_SIZE];
    uint8_t ksv_list[HDCP2X_REC_ID_LIST_SIZE]; //max device count * 5
} RepeaterAuth_Send_ReceiverID_List;

typedef struct {
    uint8_t seq_num_M[HDCP2X_SEQ_NUM_M_SIZE];
    uint8_t k[HDCP2X_K_SIZE];
    uint8_t StreamId_Type[HDCP2X_STREAM_ID_TYPE_SIZE];
} RepeaterAuth_Stream_Manage;


typedef struct {
    uint8_t V[HDCP2X_V_SIZE];
} RepeaterAuth_Send_Ack;

typedef struct {
    uint8_t M[HDCP2X_M_TAG_SIZE];
} RepeaterAuth_Stream_Ready;

#endif /* HDCP2_H */


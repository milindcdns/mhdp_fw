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
 * engine1T.h
 *
 ******************************************************************************
 */

#ifndef ENGINE_1T_H
#define ENGINE_1T_H

#include "engine.h"
#include "cdn_stdtypes.h"

/* Number of Device Private Keys
 * "HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 1.2, p.5 */
#define HDCP1X_DEVICE_PRIVATE_KEY_NUMBER 40U
/* Size of each Device Private Key in bytes (56 bits)
 * "HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 1.2, p.5 */
#define HDCP1X_DEVICE_PRIVATE_KEY_SIZE 7U
/* Total size of all Device Private Keys in bytes */
#define HDCP1X_DEVICE_PRIVATE_KEYS_TOTAL_SIZE 280U
/* Size of one element of ksv_list in bytes */
#define HDCP1X_KSV_LIST_ELEMENT_SIZE 5U
/* Correct Bksv key have 20 zeros and 20 ones */
#define HDCP1X_BKSV_NUMBER_OF_ONES 20U
/* Mask used to check number of 0/1 in Bksv */
#define HDCP1X_BKSV_CHECKER_MASK 0x01U
/* Size of M0 value
 * "HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 2.2.1, p.10 */
#define HDCP1X_M0_SIZE 8U

/*
 * Used to check if calculating of Km is done
 * @return 'true' if it is, otherwise 'false'
 */
bool ENG1T_isKmDone(void);

/*
 * Used to check if LFSR and block output finished calculation
 * @return 'true' if it is, otherwise 'false'
 */
bool ENG1T_isPrnmDone(void);

/**
 * Used to load custom An key into transmitter
 * param[in] An, pointer to 64b An key
 */
void ENG1T_loadDebugAn(uint8_t* An);

/**
 * Returns 64b pseudo-random value (An) used to initiate authentication.
 * If "HDCP1_TX_SEND_RANDOM_AN" message was not received via mailbox before call
 * this function, An will be generated on the fly, otherwise will be as set value.
 * @param[out] An, pointer to 64b value of An key
 */
void ENG1T_getAn(uint8_t* An);

/**
 *  Returns stored value of Aksv (transmitter Ksv) key. Returned value
 *  is in little-endian order (required by HDCP1.x module).
 *  @param[out] Aksv, pointer to 40b value of Aksv key
 */
void ENG1T_getAksv(uint8_t* Aksv);

/**
 *  Used to store value of Bksv (receiver Ksv) key. Stored value is
 *  convert from little-endian (as HDCP1.x module) to big-endian order.
 *  @param[in] Bksv, pointer to 40b value of Bksv key
 */
void ENG1T_setBksv(uint8_t* Bksv);

/**
 * Used to check, if stored Bksv key is valid. Valid key should be combination
 * of 20 zeros and 20 ones.
 * @return 'CDN_EOK' if Bksv is valid, otherwise 'CDN_EINVAL'
 */
uint32_t ENG1T_verifyBksv(void);

/**
 *  Returns value of Bksv (receiver Ksv) key. Stored value is
 *  convert to little-endian order (required by HDCP1.x module)
 *  @param[in] Bksv, pointer to 40b value of Bksv key
 */
void ENG1T_getBksv(uint8_t* Bksv);

/**
 * Function used to set keys of transmitter.
 * @param[in] Aksv, pointer to 40b Ksv key of transmitter
 * @param[in] ksv, pointer to 280B set of Device Private Keys
 */
void ENG1T_loadKeys(uint8_t* Aksv, uint8_t* ksv);

/**
 * First phase of A2 state. Used to compute Km values.
 * Before call next function of A2 phase should be checked by call
 * ENG1T_isKmDone() to check if computation is finished
 */
void ENG1T_computeKm(void);

/**
 * Second phase of A2 state. Used to compute LFSR output values.
 * Before call next function of A2 phase should be checked by call
 * ENG1T_isPrnmDone() to check if calculation is finished
 * @param[in] devType, if device is repeater-receiver or only receiver
 */
void ENG1T_LFSR_calculation(HdcpDevType_t devType);

/**
 * Third phase of A2 state. Used to compute M0 and R0.
 * @param[in] devType, if device is repeater-receiver or only receiver
 */
void ENG1T_compute_M0_R0(HdcpDevType_t devType);

/**
 * Function used to check if computed R0 value is equal R0' from receiver
 * @param[in] r0, value produced by receiver
 * @return 'CDN_EOK' if R0 = R0', otherwise 'CDN_EINVAL'
 */
uint32_t ENG1T_compareR0(uint16_t r0);

/**
 * Used to get Ksv list and compute transmitter V parameter
 * @param[in] ksv_list_size, size of Ksv list
 * @param[in] binfo, value of Binfo register
 * @param[out] ksv_list, pointer to ksv list
 */
void ENG1T_getKsvListAndComputeV(uint8_t *ksv_list, uint8_t ksv_list_size, uint16_t binfo);

/**
 * Used to compare V and V'
 * @param[in] V, parameter from receiver
 * @return 'true' if V is equal V', otherwise 'false'
 */
bool ENG1T_validateV(const uint8_t* V);


#endif /* ENGINE_1T_H */

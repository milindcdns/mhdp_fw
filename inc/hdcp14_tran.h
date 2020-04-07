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
 * hdcp14_tran.h
 *
 ******************************************************************************
 */

#ifndef HDCP14_TRAN_H
#define HDCP14_TRAN_H

/* Maximum number of attempts to check if V is equal V'
 * ("HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 2.3, p.20) */
#define HDCP1X_V_PRIME_VALIDATE_MAX_ATTEMPS 3U
/* Maximum number of attempts to check if R0 is equal R0'
 * ("HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 2.2.1, p.11) */
#define HDCP1X_R0_PRIME_VALIDATE_MAX_ATTEMPS 3U
/* Maximum time in milliseconds when transmitter waits for 'READY' from receiver
 * ("HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 2.3, p.20) */
#define HDCP1X_WAIT_FOR_READY_TIMEOUT_MS 5000U
/* Maximum number of IDs which can be read from KSV FIFO in one transaction */
#define HDCP1X_RID_LIST_MAX_IDS_PER_READ 3U
/* maximum time in milliseconds when transmitter waits for 'R0'_AVAILABLE' from receiver
 * ("HDCP System v1.3, Amendment for DisplayPort", revision 1.1, s 2.2.1, p.10) */
#define HDCP1X_R0_PRIME_TIMEOUT_MS 100U

/**
 * Function used to handle current state machine operations
 */
void HDCP14_TRAN_handleSM(void);

/**
 * Used to initialize HDCP v1.x module
 */
void HDCP14_TRAN_Init(void);

#endif /* HDCP14_TRAN_H */


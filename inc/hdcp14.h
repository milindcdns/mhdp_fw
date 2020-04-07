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
 * hdcp14.h
 *
 ******************************************************************************
 */

#ifndef HDCP14_H
#define HDCP14_H

/*
 * Addresses, sizes and masks of required fields in HDCP1.x module
 */

/* Address of Bksv register */
#define HDCP1X_BKSV_ADDRESS 0x68000U
/* Size of Bskv register */
#define HDCP1X_BKSV_SIZE 5U

/* Address of R0' register */
#define HDCP1X_R0_PRIME_ADDRESS 0x68005U
/* Size of R0' register */
#define HDCP1X_R0_PRIME_SIZE 2U

/* Address of Aksv register */
#define HDCP1X_AKSV_ADDRESS 0x68007U
/* Size of Aksv register */
#define HDCP1X_AKSV_SIZE 5U

/* Address of An register */
#define HDCP1X_AN_ADDRESS 0x6800CU
/* Size of An register */
#define HDCP1X_AN_SIZE 8U

/* Address of V' register */
#define HDCP1X_V_PRIME_ADDRESS 0x68014U
/* Size of V' register */
#define HDCP1X_V_PRIME_SIZE 20U

/* Address of Bcaps register */
#define HDCP1X_BCAPS_ADDRESS 0x68028U
/* Size of Bcaps register */
#define HDCP1X_BCAPS_SIZE 1U
/* Mask for Bcaps[0] - HDCP Receiver Capability */
#define HDCP1X_BCAPS_HDCP_CAPABLE_MASK 0x01U
/* Mask for Bcaps[1] - HDCP Repeater Capability */
#define HDCP1X_BCAPS_REPEATER_MASK 0x02U
/* Offset for Bcaps[1] - HDCP Repeater Capability */
#define HDCP1X_BCAPS_REPEATER_OFFSET 2U

/* Address of Bstatus register */
#define HDCP_BSTATUS_ADDRESS 0x68029U
/* Size of Bstatus register */
#define HDCP_BSTATUS_SIZE 1U
/* Mask for Bstatus[0] - Repeater is ready */
#define HDCP1X_BSTATUS_READY_MASK 0x01U
/* Mask for Bstatus[1] - R0' avaialable */
#define HDCP1X_BSTATUS_IS_R0_AVAILABLE_MASK 0x02U
/* Mask for Bstatus[2] - Link Integrity Failure */
#define HDCP1X_BSTATUS_LINK_INTEGRITY_FAILURE_MASK 0x04U
/* Mask for Bstatus[3] - Reauthentication Request */
#define HDCP1X_BSTATUS_REAUTHENTICATION_REQ_MASK 0x08U

/* Address of Binfo register */
#define HDCP1X_BINFO_ADDRESS 0x6802AU
/* Size of Bstatus register */
#define HDCP1X_BINFO_SIZE 2U
/* Mask for Binfo[6:0] - total number of attached downstream devices */
#define HDCP1X_BINFO_DEV_COUNT_MASK 0x007FU
/* Mask for Binfo[7] - more than 127 devices were attached */
#define HDCP1X_BINFO_MAX_DEVS_EXCEEDED_MASK 0x0080U
/* Mask for Binfo[11] - more than 7 levels of repeater have been cascaded together */
#define HDCP1X_BINFO_MAX_CASCADE_EXCEEDED_MASK 0x0800U

/* Address of Ksv_Fifo register */
#define HDCP1X_KSV_FIFO_ADDRESS 0x6802CU
/* Size of Ksv_Fifo register */
#define HDCP1X_KSV_FIFO_SIZE 15U

/* Address of Ainfo register */
#define HDCP1X_AINFO_ADDRESS 0x6803BU
/* Size of Ainfo register */
#define HDCP1X_AINFO_SIZE 1U
/* Mask for Ainfo[0] */
#define HDCP1X_AINFO_REAUTHENTICATION_ENABLE_IRQ_HPD_MASK 0x01U

/* Address of reserved space of HDCP1.x registers */
#define HDCP1X_RSVD_ADDRESS 0x6803CU
/* Size of reserved space of HDCP1.x registers */
#define HDCP1X_RSVD_SIZE 132U

/* Address of debug registers for HDCP1.x */
#define HDCP1X_DBG_ADDRESS 0x680C0U
/* Address of debug registers for HDCP1.x */
#define HDCP1X_DBG_SIZE 0x680C0U

#endif /* HDCP14_H */


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
 * apbChecker.c
 *
 ******************************************************************************
 */

/**
 * ATTENTION: This file is used only to share function used to check
 *            if APB/SAPB address is available. In general, up-to FW
 *            v2.0.0 this commands (APB/SAPB R/W) are realized by
 *            GENERAL_MODULE and sharing of function is not needed.
 *            This workaround is used to sync-up new FW with older
 *            versions of driver
 */

#ifndef APB_CHECKER_H
#define APB_CHECKER_H

/**
 * Used to check if APB/SAPB address is available to R/W
 * @param[in] addr, pointer to register
 * @param[in] via_sapb, 'true' if APB used, 'false' if SAPB
 * @return 'true' if register is available, otherwise 'false'
 */
bool is_mb_access_permitted(const uint32_t *addr, bool via_sapb);

#endif /* APB_CHECKER_H */

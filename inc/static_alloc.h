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
 * static_alloc.h
 *
 * Include this header for semi-dynamic memory allocation support.
 * static_alloc allocates a static buffer with desired size and creates
 * a structure describing properties of particular max number of data blocks.
 *
 * After initialization of the structure and buffer, following functions are available:
 * - memory allocation
 * - memory deallocation
 *
 * Initialization of the buffer is automatic at first call to malloc/free.
 *
 * No support for defragmentation. No allocated blocks can be moved.
 *
 ******************************************************************************
 * Constants that need to be defined prior to init:
 *
 * MAX_NUMBER_OF_BLOCKS - max number of data chunks that can be stored in a buffer
 * BLOCK_SIZE - size of one block in bytes (divisible by 4)
 * e.g. BLOCK_SIZE 16, MAX_NUMBER_OF_BLOCKS 100 would yield 100 blocks 16 Bytes each (1600B total).
 *
 ******************************************************************************
 */

#ifndef STATIC_ALLOC_H
#define STATIC_ALLOC_H

#ifndef MAX_NUMBER_OF_BLOCKS
#define MAX_NUMBER_OF_BLOCKS 448U // max 448 data objects (max 65535-1)
#endif
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 16U // each block 16 bytes (max 65535-1, must be divisible by 4)
#endif
/* Space will be divided into MAX_NUMBER_OF_BLOCKS of BLOCK_SIZE sized blocks. */

/* Define CLEAR_ON_ALLOCATION to do a memset to 0 after allocating a new block. */

/** Try to allocate size bytes in the static buffer.
 * Return pointer to allocated data block or NULL if cannot allocate.
 */
uint32_t* malloc_static_ptr32(uint16_t size);

/** Free a memory block in a static buffer by pointer */
void free_static_ptr32(const uint32_t *ptr);

#endif // multiple include protection

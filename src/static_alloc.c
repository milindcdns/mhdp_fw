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
 * static_alloc.c
 *
 ******************************************************************************
 */

// parasoft-begin-suppress METRICS-36-3 "A function should not be called from more than 5 different functions" DRV-3823

#include "stdint.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "static_alloc.h"
#include "cdn_log.h"

// variables for monitoring memory utilization
// amount of blocks currently allocated
static uint32_t strips = 0;
// amount of bytes currently allocated (counted as strips*BLOCK_SIZE)
static uint32_t mem_allocated = 0;

static uint32_t data_for_allocation[MAX_NUMBER_OF_BLOCKS][BLOCK_SIZE/4U] __attribute__ ((aligned (4)));

typedef struct {
    bool last_allocated; // indicates this is last block allocated with previous blocks
    bool free;
} MmuBlockInfo;

static MmuBlockInfo memory_blocks_info_table[MAX_NUMBER_OF_BLOCKS];

static void init_malloc_static(void);

// First call to any public function will trigger initialization
static bool initialized = false;

/* Call init if it was not called yet. */
static void check_init(void) {
    if (!initialized) {
        init_malloc_static();
    }
}

/* Initialize static alloc buffer */
static void init_malloc_static(void) {
    uint16_t i;
    initialized = true;
    cDbgMsg(DBG_GEN_MSG, DBG_FYI, "static_alloc: init: %d blocks of %d B each\n", MAX_NUMBER_OF_BLOCKS, BLOCK_SIZE);

    for (i = 0U; i < MAX_NUMBER_OF_BLOCKS; i++) {
        /* mark every block as "last allocated"
         * (for free blocks this info is irrelevant, but on block X allocation,
         * there is a checker if previous block (X-1) is marked as ended) */
        memory_blocks_info_table[i].last_allocated = true;
        // mark every block as free
        memory_blocks_info_table[i].free = true;
    }
}

/* Detect if allocation of needed_blocks blocks is possible for a given block_id.
 * Part of malloc_static. */
static bool malloc_static_alloc_possible(uint16_t needed_blocks, uint16_t block_id) {
    bool allocation_possible = false;
    if (needed_blocks > 1U) {
        // check available continuous blocks
        uint16_t available_continuous_blocks = 1U;
        uint16_t i_copy = block_id + 1U; // start from next block
        bool can_break = false; // to avoid multiple breaks in a loop
        while (i_copy < MAX_NUMBER_OF_BLOCKS) {
            if (memory_blocks_info_table[i_copy].free) {
                available_continuous_blocks++;
            } else {
                can_break = true;
            }
            if (available_continuous_blocks >= needed_blocks) {
                allocation_possible = true;
                can_break = true;
            }
            if (can_break) {
                break;
            }
            i_copy++;
        }
    } else {
        // only one block needed, we can allocate
        allocation_possible = true;
    }
    return allocation_possible;
}

/* Try to allocate size bytes in the static buffer.
 * Return valid number of block if correctly allocated.
 * If allocation is not possible, return MAX_NUMBER_OF_BLOCKS.
 */
static uint16_t malloc_static(uint16_t size) {
    // search the first available block of desired size
    uint16_t i = 0; // block id
    bool allocation_possible = false;
    // calculate how many blocks are needed
    uint16_t needed_blocks = (size/BLOCK_SIZE) + (((size % BLOCK_SIZE) != 0U) ? 1U : 0U);

    for (; i < MAX_NUMBER_OF_BLOCKS; i++) {
        MmuBlockInfo *this_block = &memory_blocks_info_table[i];
        if (!this_block->free) {
            // if this block is not free, continue to the next block
            continue;
        } else {
            // free block found
            allocation_possible = malloc_static_alloc_possible(needed_blocks, i);
            if (allocation_possible) {
                uint16_t i_iterator = i;
                // mark needed_blocks blocks as allocated starting from i
                while (needed_blocks > 0U) {
                    memory_blocks_info_table[i_iterator].free = false;
                    memory_blocks_info_table[i_iterator].last_allocated = false;
                    needed_blocks--;
                    i_iterator++;
                }
                // re-mark last block as last allocated
                memory_blocks_info_table[i_iterator - 1U].last_allocated = true;
                break;
            } else {
                continue;
            }
        }
    }
    if (!allocation_possible) {
        i = MAX_NUMBER_OF_BLOCKS;
    }
    return i;
}

/** Try to allocate size bytes in the static buffer.
 * Return pointer to allocated data block or NULL if cannot allocate.
 */
uint32_t* malloc_static_ptr32(uint16_t size) {
    static uint32_t max_mem_allocated = 0;
    uint32_t *allocated_block_address = NULL;
    check_init();
    // Try to allocate
    cDbgMsg(DBG_GEN_MSG, DBG_FYI, "calling malloc static(size %d B), ", size);
    uint16_t block_id = malloc_static(size);
    cDbgMsg(DBG_GEN_MSG, DBG_FYI, "result %d\n", block_id);
    if (block_id != MAX_NUMBER_OF_BLOCKS) {
        allocated_block_address = data_for_allocation[block_id];
        #ifdef CLEAR_ON_ALLOCATION
            // Clear newly allocated block (only requested number of bytes)
            memset(allocated_block_address, 0x00U, size);
        #endif
        // Update usage info
        do {
            strips++;
            mem_allocated += BLOCK_SIZE;
            block_id++;
        } // last_allocated block is the last for which procedure needs to be done
        while (!memory_blocks_info_table[block_id - 1U].last_allocated);
        cDbgMsg(DBG_GEN_MSG, DBG_FYI, "Alloc %d B, now %d strips\n", size, strips);
        if (mem_allocated > max_mem_allocated) {
            max_mem_allocated = mem_allocated;
            cDbgMsg(DBG_GEN_MSG, DBG_FYI, "New max mem utilization %d B\n", max_mem_allocated);
        }
    } else {
        cDbgMsg(DBG_GEN_MSG, DBG_CRIT, "Could not allocate.\n");
    }
    return allocated_block_address;
}

/* Free a memory block in a static buffer by block number */
static void free_static(uint16_t block_number) {
    /* Blocks can be freed only when:
     * - requested block is currently allocated;
     * - requested block is first of blocks allocated at once
     *   (e.g. cannot free starting from block 3, if it's part of group 2-3-4 allocated at once)
     */
    if (memory_blocks_info_table[block_number].free) {
        cDbgMsg(DBG_GEN_MSG, DBG_CRIT, "Cannot free: already free.\n");
    } else if ((block_number != 0U) &&
            (!memory_blocks_info_table[block_number - 1U].last_allocated)) {
        cDbgMsg(DBG_GEN_MSG, DBG_CRIT, "Cannot free: block %d not first in allocated group.\n", block_number);
    } else {
        uint16_t block_iterator = block_number;
        uint16_t bytes_freed = 0;
        // mark all blocks that are allocated at once as free
        do {
            memory_blocks_info_table[block_iterator].free = true;
            strips--;
            mem_allocated -= BLOCK_SIZE;
            bytes_freed += BLOCK_SIZE;
            block_iterator++;
        } while ((!memory_blocks_info_table[block_iterator - 1U].last_allocated)
                && (block_iterator < MAX_NUMBER_OF_BLOCKS));
        /* 2nd condition prevents accessing out-of-bounds element.
        However, under normal operation, this situation is impossible, because last element
        will always have last_allocated set to true, so 1st condition will stop the while loop */

        // re-mark all freed blocks as last allocated
        while (block_iterator > block_number) {
            block_iterator--;
            memory_blocks_info_table[block_iterator].last_allocated = true;
        }
        cDbgMsg(DBG_GEN_MSG, DBG_FYI, "Free %d B, now %d strips\n", bytes_freed, strips);
    }
}

/** Free a memory block in a static buffer by pointer */
void free_static_ptr32(const uint32_t *ptr) {
    check_init();
    // find a block number
    for (uint16_t i = 0; i < MAX_NUMBER_OF_BLOCKS; i++) {
        if (ptr == data_for_allocation[i]) {
            free_static(i);
        }
    }
}

// parasoft-end-suppress METRICS-36-3 "A function should not be called from more than 5 different functions" DRV-3823

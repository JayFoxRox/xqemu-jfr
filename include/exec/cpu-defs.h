/*
 * common defines for all CPUs
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_DEFS_H
#define CPU_DEFS_H

#ifndef NEED_CPU_H
#error cpu.h included from common code
#endif

#include "config.h"
#include <setjmp.h>
#include <inttypes.h>
#include "qemu/osdep.h"
#include "qemu/queue.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

#ifndef TARGET_LONG_BITS
#error TARGET_LONG_BITS must be defined before including this header
#endif

#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)

/* target_ulong is the type of a virtual address */
#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#define TARGET_FMT_lx "%08x"
#define TARGET_FMT_ld "%d"
#define TARGET_FMT_lu "%u"
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#define TARGET_FMT_lx "%016" PRIx64
#define TARGET_FMT_ld "%" PRId64
#define TARGET_FMT_lu "%" PRIu64
#else
#error TARGET_LONG_SIZE undefined
#endif

#define EXCP_INTERRUPT 	0x10000 /* async interruption */
#define EXCP_HLT        0x10001 /* hlt instruction reached */
#define EXCP_DEBUG      0x10002 /* cpu stopped after a breakpoint or singlestep */
#define EXCP_HALTED     0x10003 /* cpu is halted (waiting for external event) */

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/* Only the bottom TB_JMP_PAGE_BITS of the jump cache hash bits vary for
   addresses on the same page.  The top bits are the same.  This allows
   TLB invalidation to quickly clear a subset of the hash table.  */
#define TB_JMP_PAGE_BITS (TB_JMP_CACHE_BITS / 2)
#define TB_JMP_PAGE_SIZE (1 << TB_JMP_PAGE_BITS)
#define TB_JMP_ADDR_MASK (TB_JMP_PAGE_SIZE - 1)
#define TB_JMP_PAGE_MASK (TB_JMP_CACHE_SIZE - TB_JMP_PAGE_SIZE)

#if !defined(CONFIG_USER_ONLY)
#define CPU_TLB_BITS 8
#define CPU_TLB_SIZE (1 << CPU_TLB_BITS)

#if HOST_LONG_BITS == 32 && TARGET_LONG_BITS == 32
#define CPU_TLB_ENTRY_BITS 4
#else
#define CPU_TLB_ENTRY_BITS 5
#endif

typedef struct CPUTLBEntry {
    /* bit TARGET_LONG_BITS to TARGET_PAGE_BITS : virtual address
       bit TARGET_PAGE_BITS-1..4  : Nonzero for accesses that should not
                                    go directly to ram.
       bit 3                      : indicates that the entry is invalid
       bit 2..0                   : zero
    */
    target_ulong addr_read;
    target_ulong addr_write;
    target_ulong addr_code;
    /* Addend to virtual address to get host address.  IO accesses
       use the corresponding iotlb value.  */
    uintptr_t addend;
    /* padding to get a power of two size */
    uint8_t dummy[(1 << CPU_TLB_ENTRY_BITS) -
                  (sizeof(target_ulong) * 3 +
                   ((-sizeof(target_ulong) * 3) & (sizeof(uintptr_t) - 1)) +
                   sizeof(uintptr_t))];
} CPUTLBEntry;

QEMU_BUILD_BUG_ON(sizeof(CPUTLBEntry) != (1 << CPU_TLB_ENTRY_BITS));

#define CPU_COMMON_TLB \
    /* The meaning of the MMU modes is defined in the target code. */   \
    CPUTLBEntry tlb_table[NB_MMU_MODES][CPU_TLB_SIZE];                  \
    hwaddr iotlb[NB_MMU_MODES][CPU_TLB_SIZE];               \
    target_ulong tlb_flush_addr;                                        \
    target_ulong tlb_flush_mask;

#else

#define CPU_COMMON_TLB

#endif


#ifdef HOST_WORDS_BIGENDIAN
typedef struct icount_decr_u16 {
    uint16_t high;
    uint16_t low;
} icount_decr_u16;
#else
typedef struct icount_decr_u16 {
    uint16_t low;
    uint16_t high;
} icount_decr_u16;
#endif

typedef struct CPUBreakpoint {
    target_ulong pc;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUBreakpoint) entry;
} CPUBreakpoint;

typedef struct CPUWatchpoint {
    target_ulong vaddr;
    target_ulong len_mask;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUWatchpoint) entry;
} CPUWatchpoint;

#define CPU_TEMP_BUF_NLONGS 128
#define CPU_COMMON                                                      \
    /* soft mmu support */                                              \
    /* in order to avoid passing too many arguments to the MMIO         \
       helpers, we store some rarely used information in the CPU        \
       context) */                                                      \
    uintptr_t mem_io_pc; /* host pc at which the memory was             \
                            accessed */                                 \
    target_ulong mem_io_vaddr; /* target virtual addr at which the      \
                                     memory was accessed */             \
    CPU_COMMON_TLB                                                      \
    struct TranslationBlock *tb_jmp_cache[TB_JMP_CACHE_SIZE];           \
                                                                        \
    int64_t icount_extra; /* Instructions until next timer event.  */   \
    /* Number of cycles left, with interrupt flag in high bit.          \
       This allows a single read-compare-cbranch-write sequence to test \
       for both decrementer underflow and exceptions.  */               \
    union {                                                             \
        uint32_t u32;                                                   \
        icount_decr_u16 u16;                                            \
    } icount_decr;                                                      \
    uint32_t can_do_io; /* nonzero if memory mapped IO is safe.  */     \
                                                                        \
    /* from this point: preserved by CPU reset */                       \
    /* ice debug support */                                             \
    QTAILQ_HEAD(breakpoints_head, CPUBreakpoint) breakpoints;            \
                                                                        \
    QTAILQ_HEAD(watchpoints_head, CPUWatchpoint) watchpoints;            \
    CPUWatchpoint *watchpoint_hit;                                      \
                                                                        \
    /* Core interrupt code */                                           \
    sigjmp_buf jmp_env;                                                 \
    int exception_index;                                                \
                                                                        \
    /* user data */                                                     \
    void *opaque;                                                       \

#endif

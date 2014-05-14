/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009-2010 Aurelien Jarno <aurelien@aurel32.net>
 * Based on i386/tcg-target.c - Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "tcg-be-null.h"

/*
 * Register definitions
 */

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
     "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
     "r8",  "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
    "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",
    "r48", "r49", "r50", "r51", "r52", "r53", "r54", "r55",
    "r56", "r57", "r58", "r59", "r60", "r61", "r62", "r63",
};
#endif

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG TCG_REG_R55
#else
#define TCG_GUEST_BASE_REG TCG_REG_R0
#endif
#ifndef GUEST_BASE
#define GUEST_BASE 0
#endif

/* Branch registers */
enum {
    TCG_REG_B0 = 0,
    TCG_REG_B1,
    TCG_REG_B2,
    TCG_REG_B3,
    TCG_REG_B4,
    TCG_REG_B5,
    TCG_REG_B6,
    TCG_REG_B7,
};

/* Floating point registers */
enum {
    TCG_REG_F0 = 0,
    TCG_REG_F1,
    TCG_REG_F2,
    TCG_REG_F3,
    TCG_REG_F4,
    TCG_REG_F5,
    TCG_REG_F6,
    TCG_REG_F7,
    TCG_REG_F8,
    TCG_REG_F9,
    TCG_REG_F10,
    TCG_REG_F11,
    TCG_REG_F12,
    TCG_REG_F13,
    TCG_REG_F14,
    TCG_REG_F15,
};

/* Predicate registers */
enum {
    TCG_REG_P0 = 0,
    TCG_REG_P1,
    TCG_REG_P2,
    TCG_REG_P3,
    TCG_REG_P4,
    TCG_REG_P5,
    TCG_REG_P6,
    TCG_REG_P7,
    TCG_REG_P8,
    TCG_REG_P9,
    TCG_REG_P10,
    TCG_REG_P11,
    TCG_REG_P12,
    TCG_REG_P13,
    TCG_REG_P14,
    TCG_REG_P15,
};

/* Application registers */
enum {
    TCG_REG_PFS = 64,
};

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R35,
    TCG_REG_R36,
    TCG_REG_R37,
    TCG_REG_R38,
    TCG_REG_R39,
    TCG_REG_R40,
    TCG_REG_R41,
    TCG_REG_R42,
    TCG_REG_R43,
    TCG_REG_R44,
    TCG_REG_R45,
    TCG_REG_R46,
    TCG_REG_R47,
    TCG_REG_R48,
    TCG_REG_R49,
    TCG_REG_R50,
    TCG_REG_R51,
    TCG_REG_R52,
    TCG_REG_R53,
    TCG_REG_R54,
    TCG_REG_R55,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
    TCG_REG_R56,
    TCG_REG_R57,
    TCG_REG_R58,
    TCG_REG_R59,
    TCG_REG_R60,
    TCG_REG_R61,
    TCG_REG_R62,
    TCG_REG_R63,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11
};

static const int tcg_target_call_iarg_regs[8] = {
    TCG_REG_R56,
    TCG_REG_R57,
    TCG_REG_R58,
    TCG_REG_R59,
    TCG_REG_R60,
    TCG_REG_R61,
    TCG_REG_R62,
    TCG_REG_R63,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R8
};

/*
 * opcode formation
 */

/* bundle templates: stops (double bar in the IA64 manual) are marked with
   an uppercase letter. */
enum {
    mii = 0x00,
    miI = 0x01,
    mIi = 0x02,
    mII = 0x03,
    mlx = 0x04,
    mLX = 0x05,
    mmi = 0x08,
    mmI = 0x09,
    Mmi = 0x0a,
    MmI = 0x0b,
    mfi = 0x0c,
    mfI = 0x0d,
    mmf = 0x0e,
    mmF = 0x0f,
    mib = 0x10,
    miB = 0x11,
    mbb = 0x12,
    mbB = 0x13,
    bbb = 0x16,
    bbB = 0x17,
    mmb = 0x18,
    mmB = 0x19,
    mfb = 0x1c,
    mfB = 0x1d,
};

enum {
    OPC_ADD_A1                = 0x10000000000ull,
    OPC_AND_A1                = 0x10060000000ull,
    OPC_AND_A3                = 0x10160000000ull,
    OPC_ANDCM_A1              = 0x10068000000ull,
    OPC_ANDCM_A3              = 0x10168000000ull,
    OPC_ADDS_A4               = 0x10800000000ull,
    OPC_ADDL_A5               = 0x12000000000ull,
    OPC_ALLOC_M34             = 0x02c00000000ull,
    OPC_BR_DPTK_FEW_B1        = 0x08400000000ull,
    OPC_BR_SPTK_MANY_B1       = 0x08000001000ull,
    OPC_BR_SPTK_MANY_B4       = 0x00100001000ull,
    OPC_BR_CALL_SPTK_MANY_B5  = 0x02100001000ull,
    OPC_BR_RET_SPTK_MANY_B4   = 0x00108001100ull,
    OPC_BRL_SPTK_MANY_X3      = 0x18000001000ull,
    OPC_BRL_CALL_SPTK_MANY_X4 = 0x1a000001000ull,
    OPC_CMP_LT_A6             = 0x18000000000ull,
    OPC_CMP_LTU_A6            = 0x1a000000000ull,
    OPC_CMP_EQ_A6             = 0x1c000000000ull,
    OPC_CMP4_LT_A6            = 0x18400000000ull,
    OPC_CMP4_LTU_A6           = 0x1a400000000ull,
    OPC_CMP4_EQ_A6            = 0x1c400000000ull,
    OPC_DEP_I14               = 0x0ae00000000ull,
    OPC_DEP_I15               = 0x08000000000ull,
    OPC_DEP_Z_I12             = 0x0a600000000ull,
    OPC_EXTR_I11              = 0x0a400002000ull,
    OPC_EXTR_U_I11            = 0x0a400000000ull,
    OPC_FCVT_FX_TRUNC_S1_F10  = 0x004d0000000ull,
    OPC_FCVT_FXU_TRUNC_S1_F10 = 0x004d8000000ull,
    OPC_FCVT_XF_F11           = 0x000e0000000ull,
    OPC_FMA_S1_F1             = 0x10400000000ull,
    OPC_FNMA_S1_F1            = 0x18400000000ull,
    OPC_FRCPA_S1_F6           = 0x00600000000ull,
    OPC_GETF_SIG_M19          = 0x08708000000ull,
    OPC_LD1_M1                = 0x08000000000ull,
    OPC_LD1_M3                = 0x0a000000000ull,
    OPC_LD2_M1                = 0x08040000000ull,
    OPC_LD2_M3                = 0x0a040000000ull,
    OPC_LD4_M1                = 0x08080000000ull,
    OPC_LD4_M3                = 0x0a080000000ull,
    OPC_LD8_M1                = 0x080c0000000ull,
    OPC_LD8_M3                = 0x0a0c0000000ull,
    OPC_MUX1_I3               = 0x0eca0000000ull,
    OPC_NOP_B9                = 0x04008000000ull,
    OPC_NOP_F16               = 0x00008000000ull,
    OPC_NOP_I18               = 0x00008000000ull,
    OPC_NOP_M48               = 0x00008000000ull,
    OPC_MOV_I21               = 0x00e00100000ull,
    OPC_MOV_RET_I21           = 0x00e00500000ull,
    OPC_MOV_I22               = 0x00188000000ull,
    OPC_MOV_I_I26             = 0x00150000000ull,
    OPC_MOVL_X2               = 0x0c000000000ull,
    OPC_OR_A1                 = 0x10070000000ull,
    OPC_OR_A3                 = 0x10170000000ull,
    OPC_SETF_EXP_M18          = 0x0c748000000ull,
    OPC_SETF_SIG_M18          = 0x0c708000000ull,
    OPC_SHL_I7                = 0x0f240000000ull,
    OPC_SHR_I5                = 0x0f220000000ull,
    OPC_SHR_U_I5              = 0x0f200000000ull,
    OPC_SHRP_I10              = 0x0ac00000000ull,
    OPC_SXT1_I29              = 0x000a0000000ull,
    OPC_SXT2_I29              = 0x000a8000000ull,
    OPC_SXT4_I29              = 0x000b0000000ull,
    OPC_ST1_M4                = 0x08c00000000ull,
    OPC_ST2_M4                = 0x08c40000000ull,
    OPC_ST4_M4                = 0x08c80000000ull,
    OPC_ST8_M4                = 0x08cc0000000ull,
    OPC_SUB_A1                = 0x10028000000ull,
    OPC_SUB_A3                = 0x10128000000ull,
    OPC_UNPACK4_L_I2          = 0x0f860000000ull,
    OPC_XMA_L_F2              = 0x1d000000000ull,
    OPC_XOR_A1                = 0x10078000000ull,
    OPC_XOR_A3                = 0x10178000000ull,
    OPC_ZXT1_I29              = 0x00080000000ull,
    OPC_ZXT2_I29              = 0x00088000000ull,
    OPC_ZXT4_I29              = 0x00090000000ull,

    INSN_NOP_M                = OPC_NOP_M48,  /* nop.m 0 */
    INSN_NOP_I                = OPC_NOP_I18,  /* nop.i 0 */
};

static inline uint64_t tcg_opc_a1(int qp, uint64_t opc, int r1,
                                  int r2, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_a3(int qp, uint64_t opc, int r1,
                                  uint64_t imm, int r3)
{
    return opc
           | ((imm & 0x80) << 29) /* s */
           | ((imm & 0x7f) << 13) /* imm7b */
           | ((r3 & 0x7f) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_a4(int qp, uint64_t opc, int r1,
                                  uint64_t imm, int r3)
{
    return opc
           | ((imm & 0x2000) << 23) /* s */
           | ((imm & 0x1f80) << 20) /* imm6d */
           | ((imm & 0x007f) << 13) /* imm7b */
           | ((r3 & 0x7f) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_a5(int qp, uint64_t opc, int r1,
                                  uint64_t imm, int r3)
{
    return opc
           | ((imm & 0x200000) << 15) /* s */
           | ((imm & 0x1f0000) <<  6) /* imm5c */
           | ((imm & 0x00ff80) << 20) /* imm9d */
           | ((imm & 0x00007f) << 13) /* imm7b */
           | ((r3 & 0x03) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_a6(int qp, uint64_t opc, int p1,
                                  int p2, int r2, int r3)
{
    return opc
           | ((p2 & 0x3f) << 27)
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((p1 & 0x3f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_b1(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x100000) << 16) /* s */
           | ((imm & 0x0fffff) << 13) /* imm20b */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_b4(int qp, uint64_t opc, int b2)
{
    return opc
           | ((b2 & 0x7) << 13)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_b5(int qp, uint64_t opc, int b1, int b2)
{
    return opc
           | ((b2 & 0x7) << 13)
           | ((b1 & 0x7) << 6)
           | (qp & 0x3f);
}


static inline uint64_t tcg_opc_b9(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x100000) << 16) /* i */
           | ((imm & 0x0fffff) << 6)  /* imm20a */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f1(int qp, uint64_t opc, int f1,
                                  int f3, int f4, int f2)
{
    return opc
           | ((f4 & 0x7f) << 27)
           | ((f3 & 0x7f) << 20)
           | ((f2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f2(int qp, uint64_t opc, int f1,
                                  int f3, int f4, int f2)
{
    return opc
           | ((f4 & 0x7f) << 27)
           | ((f3 & 0x7f) << 20)
           | ((f2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f6(int qp, uint64_t opc, int f1,
                                  int p2, int f2, int f3)
{
    return opc
           | ((p2 & 0x3f) << 27)
           | ((f3 & 0x7f) << 20)
           | ((f2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f10(int qp, uint64_t opc, int f1, int f2)
{
    return opc
           | ((f2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f11(int qp, uint64_t opc, int f1, int f2)
{
    return opc
           | ((f2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_f16(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x100000) << 16) /* i */
           | ((imm & 0x0fffff) << 6)  /* imm20a */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i2(int qp, uint64_t opc, int r1,
                                  int r2, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i3(int qp, uint64_t opc, int r1,
                                  int r2, int mbtype)
{
    return opc
           | ((mbtype & 0x0f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i5(int qp, uint64_t opc, int r1,
                                  int r3, int r2)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i7(int qp, uint64_t opc, int r1,
                                  int r2, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i10(int qp, uint64_t opc, int r1,
                                   int r2, int r3, uint64_t count)
{
    return opc
           | ((count & 0x3f) << 27)
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i11(int qp, uint64_t opc, int r1,
                                   int r3, uint64_t pos, uint64_t len)
{
    return opc
           | ((len & 0x3f) << 27)
           | ((r3 & 0x7f) << 20)
           | ((pos & 0x3f) << 14)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i12(int qp, uint64_t opc, int r1,
                                   int r2, uint64_t pos, uint64_t len)
{
    return opc
           | ((len & 0x3f) << 27)
           | ((pos & 0x3f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i14(int qp, uint64_t opc, int r1, uint64_t imm,
                                   int r3, uint64_t pos, uint64_t len)
{
    return opc
           | ((imm & 0x01) << 36)
           | ((len & 0x3f) << 27)
           | ((r3 & 0x7f) << 20)
           | ((pos & 0x3f) << 14)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i15(int qp, uint64_t opc, int r1, int r2,
                                   int r3, uint64_t pos, uint64_t len)
{
    return opc
           | ((pos & 0x3f) << 31)
           | ((len & 0x0f) << 27)
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i18(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x100000) << 16) /* i */
           | ((imm & 0x0fffff) << 6)  /* imm20a */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i21(int qp, uint64_t opc, int b1,
                                   int r2, uint64_t imm)
{
    return opc
           | ((imm & 0x1ff) << 24)
           | ((r2 & 0x7f) << 13)
           | ((b1 & 0x7) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i22(int qp, uint64_t opc, int r1, int b2)
{
    return opc
           | ((b2 & 0x7) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i26(int qp, uint64_t opc, int ar3, int r2)
{
    return opc
           | ((ar3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_i29(int qp, uint64_t opc, int r1, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_l2(uint64_t imm)
{
    return (imm & 0x7fffffffffc00000ull) >> 22;
}

static inline uint64_t tcg_opc_l3(uint64_t imm)
{
    return (imm & 0x07fffffffff00000ull) >> 18;
}

#define tcg_opc_l4  tcg_opc_l3

static inline uint64_t tcg_opc_m1(int qp, uint64_t opc, int r1, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m3(int qp, uint64_t opc, int r1,
                                  int r3, uint64_t imm)
{
    return opc
           | ((imm & 0x100) << 28) /* s */
           | ((imm & 0x080) << 20) /* i */
           | ((imm & 0x07f) << 13) /* imm7b */
           | ((r3 & 0x7f) << 20)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m4(int qp, uint64_t opc, int r2, int r3)
{
    return opc
           | ((r3 & 0x7f) << 20)
           | ((r2 & 0x7f) << 13)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m18(int qp, uint64_t opc, int f1, int r2)
{
    return opc
           | ((r2 & 0x7f) << 13)
           | ((f1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m19(int qp, uint64_t opc, int r1, int f2)
{
    return opc
           | ((f2 & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m34(int qp, uint64_t opc, int r1,
                                   int sof, int sol, int sor)
{
    return opc
           | ((sor & 0x0f) << 27)
           | ((sol & 0x7f) << 20)
           | ((sof & 0x7f) << 13)
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_m48(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x100000) << 16) /* i */
           | ((imm & 0x0fffff) << 6)  /* imm20a */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_x2(int qp, uint64_t opc,
                                  int r1, uint64_t imm)
{
    return opc
           | ((imm & 0x8000000000000000ull) >> 27) /* i */
           |  (imm & 0x0000000000200000ull)        /* ic */
           | ((imm & 0x00000000001f0000ull) << 6)  /* imm5c */
           | ((imm & 0x000000000000ff80ull) << 20) /* imm9d */
           | ((imm & 0x000000000000007full) << 13) /* imm7b */
           | ((r1 & 0x7f) << 6)
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_x3(int qp, uint64_t opc, uint64_t imm)
{
    return opc
           | ((imm & 0x0800000000000000ull) >> 23) /* i */
           | ((imm & 0x00000000000fffffull) << 13) /* imm20b */
           | (qp & 0x3f);
}

static inline uint64_t tcg_opc_x4(int qp, uint64_t opc, int b1, uint64_t imm)
{
    return opc
           | ((imm & 0x0800000000000000ull) >> 23) /* i */
           | ((imm & 0x00000000000fffffull) << 13) /* imm20b */
           | ((b1 & 0x7) << 6)
           | (qp & 0x3f);
}


/*
 * Relocations
 */

static inline void reloc_pcrel21b(void *pc, intptr_t target)
{
    uint64_t imm;
    int64_t disp;
    int slot;

    slot = (intptr_t)pc & 3;
    pc = (void *)((intptr_t)pc & ~3);

    disp = target - (intptr_t)pc;
    imm = (uint64_t) disp >> 4;

    switch(slot) {
    case 0:
        *(uint64_t *)(pc + 0) = (*(uint64_t *)(pc + 8) & 0xfffffdc00003ffffull)
                                | ((imm & 0x100000) << 21)  /* s */
                                | ((imm & 0x0fffff) << 18); /* imm20b */
        break;
    case 1:
        *(uint64_t *)(pc + 8) = (*(uint64_t *)(pc + 8) & 0xfffffffffffb8000ull)
                                | ((imm & 0x100000) >> 2)   /* s */
                                | ((imm & 0x0fffe0) >> 5);  /* imm20b */
        *(uint64_t *)(pc + 0) = (*(uint64_t *)(pc + 0) & 0x07ffffffffffffffull)
                                | ((imm & 0x00001f) << 59); /* imm20b */
        break;
    case 2:
        *(uint64_t *)(pc + 8) = (*(uint64_t *)(pc + 8) & 0xf700000fffffffffull)
                                | ((imm & 0x100000) << 39)  /* s */
                                | ((imm & 0x0fffff) << 36); /* imm20b */
        break;
    }
}

static inline uint64_t get_reloc_pcrel21b (void *pc)
{
    int64_t low, high;
    int slot;

    slot = (tcg_target_long) pc & 3;
    pc = (void *)((tcg_target_long) pc & ~3);

    low  = (*(uint64_t *)(pc + 0));
    high = (*(uint64_t *)(pc + 8));

    switch(slot) {
    case 0:
        return ((low >> 21) & 0x100000) + /* s */
               ((low >> 18) & 0x0fffff);  /* imm20b */
    case 1:
        return ((high << 2) & 0x100000) + /* s */
               ((high << 5) & 0x0fffe0) + /* imm20b */
               ((low >> 59) & 0x00001f);  /* imm20b */
    case 2:
        return ((high >> 39) & 0x100000) + /* s */
               ((high >> 36) & 0x0fffff);  /* imm20b */
    default:
        tcg_abort();
    }
}

static inline void reloc_pcrel60b(void *pc, intptr_t target)
{
    int64_t disp;
    uint64_t imm;

    disp = target - (intptr_t)pc;
    imm = (uint64_t) disp >> 4;

    *(uint64_t *)(pc + 8) = (*(uint64_t *)(pc + 8) & 0xf700000fff800000ull)
                             |  (imm & 0x0800000000000000ull)         /* s */
                             | ((imm & 0x07fffff000000000ull) >> 36)  /* imm39 */
                             | ((imm & 0x00000000000fffffull) << 36); /* imm20b */
    *(uint64_t *)(pc + 0) = (*(uint64_t *)(pc + 0) & 0x00003fffffffffffull)
                             | ((imm & 0x0000000ffff00000ull) << 28); /* imm39 */
}

static inline uint64_t get_reloc_pcrel60b (void *pc)
{
    int64_t low, high;

    low  = (*(uint64_t *)(pc + 0));
    high = (*(uint64_t *)(pc + 8));

    return ((high)       & 0x0800000000000000ull) + /* s */
           ((high >> 36) & 0x00000000000fffffull) + /* imm20b */
           ((high << 36) & 0x07fffff000000000ull) + /* imm39 */
           ((low >> 28)  & 0x0000000ffff00000ull);  /* imm39 */
}


static void patch_reloc(uint8_t *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    value += addend;
    switch (type) {
    case R_IA64_PCREL21B:
        reloc_pcrel21b(code_ptr, value);
        break;
    case R_IA64_PCREL60B:
        reloc_pcrel60b(code_ptr, value);
    default:
        tcg_abort();
    }
}

/*
 * Constraints
 */

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch(ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffffffffffffull);
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S22;
        break;
    case 'S':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set(ct->u.regs, 0xffffffffffffffffull);
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R56);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R57);
#endif
        break;
    case 'Z':
        /* We are cheating a bit here, using the fact that the register
           r0 is also the register number 0. Hence there is no need
           to check for const_args in each instruction. */
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

/* test if a constant matches the constraint */
static inline int tcg_target_const_match(tcg_target_long val,
                                         const TCGArgConstraint *arg_ct)
{
    int ct;
    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST)
        return 1;
    else if ((ct & TCG_CT_CONST_ZERO) && val == 0)
        return 1;
    else if ((ct & TCG_CT_CONST_S22) && val == ((int32_t)val << 10) >> 10)
        return 1;
    else
        return 0;
}

/*
 * Code generation
 */

static uint8_t *tb_ret_addr;

static inline void tcg_out_bundle(TCGContext *s, int template,
                                  uint64_t slot0, uint64_t slot1,
                                  uint64_t slot2)
{
    template &= 0x1f;          /* 5 bits */
    slot0 &= 0x1ffffffffffull; /* 41 bits */
    slot1 &= 0x1ffffffffffull; /* 41 bits */
    slot2 &= 0x1ffffffffffull; /* 41 bits */

    *(uint64_t *)(s->code_ptr + 0) = (slot1 << 46) | (slot0 << 5) | template;
    *(uint64_t *)(s->code_ptr + 8) = (slot2 << 23) | (slot1 >> 18);
    s->code_ptr += 16;
}

static inline uint64_t tcg_opc_mov_a(int qp, TCGReg dst, TCGReg src)
{
    return tcg_opc_a4(qp, OPC_ADDS_A4, dst, 0, src);
}

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    tcg_out_bundle(s, mmI,
                   INSN_NOP_M,
                   INSN_NOP_M,
                   tcg_opc_mov_a(TCG_REG_P0, ret, arg));
}

static inline uint64_t tcg_opc_movi_a(int qp, TCGReg dst, int64_t src)
{
    assert(src == sextract64(src, 0, 22));
    return tcg_opc_a5(qp, OPC_ADDL_A5, dst, src, TCG_REG_R0);
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                TCGReg reg, tcg_target_long arg)
{
    tcg_out_bundle(s, mLX,
                   INSN_NOP_M,
                   tcg_opc_l2 (arg),
                   tcg_opc_x2 (TCG_REG_P0, OPC_MOVL_X2, reg, arg));
}

static void tcg_out_br(TCGContext *s, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    /* We pay attention here to not modify the branch target by reading
       the existing value and using it again. This ensure that caches and
       memory are kept coherent during retranslation. */
    tcg_out_bundle(s, mmB,
                   INSN_NOP_M,
                   INSN_NOP_M,
                   tcg_opc_b1 (TCG_REG_P0, OPC_BR_SPTK_MANY_B1,
                               get_reloc_pcrel21b(s->code_ptr + 2)));

    if (l->has_value) {
        reloc_pcrel21b((s->code_ptr - 16) + 2, l->u.value);
    } else {
        tcg_out_reloc(s, (s->code_ptr - 16) + 2,
                      R_IA64_PCREL21B, label_index, 0);
    }
}

static inline void tcg_out_calli(TCGContext *s, uintptr_t addr)
{
    /* Look through the function descriptor.  */
    uintptr_t disp, *desc = (uintptr_t *)addr;
    tcg_out_bundle(s, mlx,
                   INSN_NOP_M,
                   tcg_opc_l2 (desc[1]),
                   tcg_opc_x2 (TCG_REG_P0, OPC_MOVL_X2, TCG_REG_R1, desc[1]));
    disp = (desc[0] - (uintptr_t)s->code_ptr) >> 4;
    tcg_out_bundle(s, mLX,
                   INSN_NOP_M,
                   tcg_opc_l4 (disp),
                   tcg_opc_x4 (TCG_REG_P0, OPC_BRL_CALL_SPTK_MANY_X4,
                               TCG_REG_B0, disp));
}

static inline void tcg_out_callr(TCGContext *s, TCGReg addr)
{
    tcg_out_bundle(s, MmI,
                   tcg_opc_m1 (TCG_REG_P0, OPC_LD8_M1, TCG_REG_R2, addr),
                   tcg_opc_a4 (TCG_REG_P0, OPC_ADDS_A4, TCG_REG_R3, 8, addr),
                   tcg_opc_i21(TCG_REG_P0, OPC_MOV_I21,
                               TCG_REG_B6, TCG_REG_R2, 0));
    tcg_out_bundle(s, mmB,
                   tcg_opc_m1 (TCG_REG_P0, OPC_LD8_M1, TCG_REG_R1, TCG_REG_R3),
                   INSN_NOP_M,
                   tcg_opc_b5 (TCG_REG_P0, OPC_BR_CALL_SPTK_MANY_B5,
                               TCG_REG_B0, TCG_REG_B6));
}

static void tcg_out_exit_tb(TCGContext *s, tcg_target_long arg)
{
    int64_t disp;
    uint64_t imm;

    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R8, arg);

    disp = tb_ret_addr - s->code_ptr;
    imm = (uint64_t)disp >> 4;

    tcg_out_bundle(s, mLX,
                   INSN_NOP_M,
                   tcg_opc_l3 (imm),
                   tcg_opc_x3 (TCG_REG_P0, OPC_BRL_SPTK_MANY_X3, imm));
}

static inline void tcg_out_goto_tb(TCGContext *s, TCGArg arg)
{
    if (s->tb_jmp_offset) {
        /* direct jump method */
        tcg_abort();
    } else {
        /* indirect jump method */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2,
                     (tcg_target_long)(s->tb_next + arg));
        tcg_out_bundle(s, MmI,
                       tcg_opc_m1 (TCG_REG_P0, OPC_LD8_M1,
                                   TCG_REG_R2, TCG_REG_R2),
                       INSN_NOP_M,
                       tcg_opc_i21(TCG_REG_P0, OPC_MOV_I21, TCG_REG_B6,
                                   TCG_REG_R2, 0));
        tcg_out_bundle(s, mmB,
                       INSN_NOP_M,
                       INSN_NOP_M,
                       tcg_opc_b4 (TCG_REG_P0, OPC_BR_SPTK_MANY_B4,
                                   TCG_REG_B6));
    }
    s->tb_next_offset[arg] = s->code_ptr - s->code_buf;
}

static inline void tcg_out_jmp(TCGContext *s, TCGArg addr)
{
    tcg_out_bundle(s, mmI,
                   INSN_NOP_M,
                   INSN_NOP_M,
                   tcg_opc_i21(TCG_REG_P0, OPC_MOV_I21, TCG_REG_B6, addr, 0));
    tcg_out_bundle(s, mmB,
                   INSN_NOP_M,
                   INSN_NOP_M,
                   tcg_opc_b4(TCG_REG_P0, OPC_BR_SPTK_MANY_B4, TCG_REG_B6));
}

static inline void tcg_out_ld_rel(TCGContext *s, uint64_t opc_m4, TCGArg arg,
                                  TCGArg arg1, tcg_target_long arg2)
{
    if (arg2 == ((int16_t)arg2 >> 2) << 2) {
        tcg_out_bundle(s, MmI,
                       tcg_opc_a4(TCG_REG_P0, OPC_ADDS_A4,
                                  TCG_REG_R2, arg2, arg1),
                       tcg_opc_m1 (TCG_REG_P0, opc_m4, arg, TCG_REG_R2),
                       INSN_NOP_I);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, arg2);
        tcg_out_bundle(s, MmI,
                       tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1,
                                   TCG_REG_R2, TCG_REG_R2, arg1),
                       tcg_opc_m1 (TCG_REG_P0, opc_m4, arg, TCG_REG_R2),
                       INSN_NOP_I);
    }
}

static inline void tcg_out_st_rel(TCGContext *s, uint64_t opc_m4, TCGArg arg,
                                  TCGArg arg1, tcg_target_long arg2)
{
    if (arg2 == ((int16_t)arg2 >> 2) << 2) {
        tcg_out_bundle(s, MmI,
                       tcg_opc_a4(TCG_REG_P0, OPC_ADDS_A4,
                                  TCG_REG_R2, arg2, arg1),
                       tcg_opc_m4 (TCG_REG_P0, opc_m4, arg, TCG_REG_R2),
                       INSN_NOP_I);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, arg2);
        tcg_out_bundle(s, MmI,
                       tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1,
                                   TCG_REG_R2, TCG_REG_R2, arg1),
                       tcg_opc_m4 (TCG_REG_P0, opc_m4, arg, TCG_REG_R2),
                       INSN_NOP_I);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_ld_rel(s, OPC_LD4_M1, arg, arg1, arg2);
    } else {
        tcg_out_ld_rel(s, OPC_LD8_M1, arg, arg1, arg2);
    }
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_st_rel(s, OPC_ST4_M4, arg, arg1, arg2);
    } else {
        tcg_out_st_rel(s, OPC_ST8_M4, arg, arg1, arg2);
    }
}

static inline void tcg_out_alu(TCGContext *s, uint64_t opc_a1, uint64_t opc_a3,
                               TCGReg ret, TCGArg arg1, int const_arg1,
                               TCGArg arg2, int const_arg2)
{
    uint64_t opc1 = 0, opc2 = 0, opc3 = 0;

    if (const_arg2 && arg2 != 0) {
        opc2 = tcg_opc_movi_a(TCG_REG_P0, TCG_REG_R3, arg2);
        arg2 = TCG_REG_R3;
    }
    if (const_arg1 && arg1 != 0) {
        if (opc_a3 && arg1 == (int8_t)arg1) {
            opc3 = tcg_opc_a3(TCG_REG_P0, opc_a3, ret, arg1, arg2);
        } else {
            opc1 = tcg_opc_movi_a(TCG_REG_P0, TCG_REG_R2, arg1);
            arg1 = TCG_REG_R2;
        }
    }
    if (opc3 == 0) {
        opc3 = tcg_opc_a1(TCG_REG_P0, opc_a1, ret, arg1, arg2);
    }

    tcg_out_bundle(s, (opc1 || opc2 ? mII : miI),
                   opc1 ? opc1 : INSN_NOP_M,
                   opc2 ? opc2 : INSN_NOP_I,
                   opc3);
}

static inline void tcg_out_add(TCGContext *s, TCGReg ret, TCGReg arg1,
                               TCGArg arg2, int const_arg2)
{
    if (const_arg2 && arg2 == sextract64(arg2, 0, 14)) {
        tcg_out_bundle(s, mmI,
                       INSN_NOP_M,
                       INSN_NOP_M,
                       tcg_opc_a4(TCG_REG_P0, OPC_ADDS_A4, ret, arg2, arg1));
    } else {
        tcg_out_alu(s, OPC_ADD_A1, 0, ret, arg1, 0, arg2, const_arg2);
    }
}

static inline void tcg_out_sub(TCGContext *s, TCGReg ret, TCGArg arg1,
                               int const_arg1, TCGArg arg2, int const_arg2)
{
    if (!const_arg1 && const_arg2 && -arg2 == sextract64(-arg2, 0, 14)) {
        tcg_out_bundle(s, mmI,
                       INSN_NOP_M,
                       INSN_NOP_M,
                       tcg_opc_a4(TCG_REG_P0, OPC_ADDS_A4, ret, -arg2, arg1));
    } else {
        tcg_out_alu(s, OPC_SUB_A1, OPC_SUB_A3, ret,
                    arg1, const_arg1, arg2, const_arg2);
    }
}

static inline void tcg_out_eqv(TCGContext *s, TCGArg ret,
                               TCGArg arg1, int const_arg1,
                               TCGArg arg2, int const_arg2)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_a1 (TCG_REG_P0, OPC_XOR_A1, ret, arg1, arg2),
                   tcg_opc_a3 (TCG_REG_P0, OPC_ANDCM_A3, ret, -1, ret));
}

static inline void tcg_out_nand(TCGContext *s, TCGArg ret,
                                TCGArg arg1, int const_arg1,
                                TCGArg arg2, int const_arg2)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_a1 (TCG_REG_P0, OPC_AND_A1, ret, arg1, arg2),
                   tcg_opc_a3 (TCG_REG_P0, OPC_ANDCM_A3, ret, -1, ret));
}

static inline void tcg_out_nor(TCGContext *s, TCGArg ret,
                               TCGArg arg1, int const_arg1,
                               TCGArg arg2, int const_arg2)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_a1 (TCG_REG_P0, OPC_OR_A1, ret, arg1, arg2),
                   tcg_opc_a3 (TCG_REG_P0, OPC_ANDCM_A3, ret, -1, ret));
}

static inline void tcg_out_orc(TCGContext *s, TCGArg ret,
                               TCGArg arg1, int const_arg1,
                               TCGArg arg2, int const_arg2)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_a3 (TCG_REG_P0, OPC_ANDCM_A3, TCG_REG_R2, -1, arg2),
                   tcg_opc_a1 (TCG_REG_P0, OPC_OR_A1, ret, arg1, TCG_REG_R2));
}

static inline void tcg_out_mul(TCGContext *s, TCGArg ret,
                               TCGArg arg1, TCGArg arg2)
{
    tcg_out_bundle(s, mmI,
                   tcg_opc_m18(TCG_REG_P0, OPC_SETF_SIG_M18, TCG_REG_F6, arg1),
                   tcg_opc_m18(TCG_REG_P0, OPC_SETF_SIG_M18, TCG_REG_F7, arg2),
                   INSN_NOP_I);
    tcg_out_bundle(s, mmF,
                   INSN_NOP_M,
                   INSN_NOP_M,
                   tcg_opc_f2 (TCG_REG_P0, OPC_XMA_L_F2, TCG_REG_F6, TCG_REG_F6,
                               TCG_REG_F7, TCG_REG_F0));
    tcg_out_bundle(s, miI,
                   tcg_opc_m19(TCG_REG_P0, OPC_GETF_SIG_M19, ret, TCG_REG_F6),
                   INSN_NOP_I,
                   INSN_NOP_I);
}

static inline void tcg_out_sar_i32(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_I11,
                                   ret, arg1, arg2, 31 - arg2));
    } else {
        tcg_out_bundle(s, mII,
                       tcg_opc_a3 (TCG_REG_P0, OPC_AND_A3,
                                   TCG_REG_R3, 0x1f, arg2),
                       tcg_opc_i29(TCG_REG_P0, OPC_SXT4_I29, TCG_REG_R2, arg1),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_I5, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static inline void tcg_out_sar_i64(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_I11,
                                   ret, arg1, arg2, 63 - arg2));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_I5, ret, arg1, arg2));
    }
}

static inline void tcg_out_shl_i32(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12, ret,
                                   arg1, 63 - arg2, 31 - arg2));
    } else {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_a3 (TCG_REG_P0, OPC_AND_A3, TCG_REG_R2,
                                   0x1f, arg2),
                       tcg_opc_i7 (TCG_REG_P0, OPC_SHL_I7, ret,
                                   arg1, TCG_REG_R2));
    }
}

static inline void tcg_out_shl_i64(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12, ret,
                                   arg1, 63 - arg2, 63 - arg2));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i7 (TCG_REG_P0, OPC_SHL_I7, ret,
                                   arg1, arg2));
    }
}

static inline void tcg_out_shr_i32(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_U_I11, ret,
                                   arg1, arg2, 31 - arg2));
    } else {
        tcg_out_bundle(s, mII,
                       tcg_opc_a3 (TCG_REG_P0, OPC_AND_A3, TCG_REG_R3,
                                   0x1f, arg2),
                       tcg_opc_i29(TCG_REG_P0, OPC_ZXT4_I29, TCG_REG_R2, arg1),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static inline void tcg_out_shr_i64(TCGContext *s, TCGArg ret, TCGArg arg1,
                                   TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_U_I11, ret,
                                   arg1, arg2, 63 - arg2));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, ret,
                                   arg1, arg2));
    }
}

static inline void tcg_out_rotl_i32(TCGContext *s, TCGArg ret, TCGArg arg1,
                                    TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i2 (TCG_REG_P0, OPC_UNPACK4_L_I2,
                                   TCG_REG_R2, arg1, arg1),
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_U_I11, ret,
                                   TCG_REG_R2, 32 - arg2, 31));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       tcg_opc_i2 (TCG_REG_P0, OPC_UNPACK4_L_I2,
                                   TCG_REG_R2, arg1, arg1),
                       tcg_opc_a3 (TCG_REG_P0, OPC_AND_A3, TCG_REG_R3,
                                   0x1f, arg2));
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_a3 (TCG_REG_P0, OPC_SUB_A3, TCG_REG_R3,
                                   0x20, TCG_REG_R3),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static inline void tcg_out_rotl_i64(TCGContext *s, TCGArg ret, TCGArg arg1,
                                    TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i10(TCG_REG_P0, OPC_SHRP_I10, ret, arg1,
                                   arg1, 0x40 - arg2));
    } else {
        tcg_out_bundle(s, mII,
                       tcg_opc_a3 (TCG_REG_P0, OPC_SUB_A3, TCG_REG_R2,
                                   0x40, arg2),
                       tcg_opc_i7 (TCG_REG_P0, OPC_SHL_I7, TCG_REG_R3,
                                   arg1, arg2),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, TCG_REG_R2,
                                   arg1, TCG_REG_R2));
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_a1 (TCG_REG_P0, OPC_OR_A1, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static inline void tcg_out_rotr_i32(TCGContext *s, TCGArg ret, TCGArg arg1,
                                    TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i2 (TCG_REG_P0, OPC_UNPACK4_L_I2,
                                   TCG_REG_R2, arg1, arg1),
                       tcg_opc_i11(TCG_REG_P0, OPC_EXTR_U_I11, ret,
                                   TCG_REG_R2, arg2, 31));
    } else {
        tcg_out_bundle(s, mII,
                       tcg_opc_a3 (TCG_REG_P0, OPC_AND_A3, TCG_REG_R3,
                                   0x1f, arg2),
                       tcg_opc_i2 (TCG_REG_P0, OPC_UNPACK4_L_I2,
                                   TCG_REG_R2, arg1, arg1),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static inline void tcg_out_rotr_i64(TCGContext *s, TCGArg ret, TCGArg arg1,
                                    TCGArg arg2, int const_arg2)
{
    if (const_arg2) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_i10(TCG_REG_P0, OPC_SHRP_I10, ret, arg1,
                                   arg1, arg2));
    } else {
        tcg_out_bundle(s, mII,
                       tcg_opc_a3 (TCG_REG_P0, OPC_SUB_A3, TCG_REG_R2,
                                   0x40, arg2),
                       tcg_opc_i5 (TCG_REG_P0, OPC_SHR_U_I5, TCG_REG_R3,
                                   arg1, arg2),
                       tcg_opc_i7 (TCG_REG_P0, OPC_SHL_I7, TCG_REG_R2,
                                   arg1, TCG_REG_R2));
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_a1 (TCG_REG_P0, OPC_OR_A1, ret,
                                   TCG_REG_R2, TCG_REG_R3));
    }
}

static const uint64_t opc_ext_i29[8] = {
    OPC_ZXT1_I29, OPC_ZXT2_I29, OPC_ZXT4_I29, 0,
    OPC_SXT1_I29, OPC_SXT2_I29, OPC_SXT4_I29, 0
};

static inline uint64_t tcg_opc_ext_i(int qp, TCGMemOp opc, TCGReg d, TCGReg s)
{
    if ((opc & MO_SIZE) == MO_64) {
        return tcg_opc_mov_a(qp, d, s);
    } else {
        return tcg_opc_i29(qp, opc_ext_i29[opc & MO_SSIZE], d, s);
    }
}

static inline void tcg_out_ext(TCGContext *s, uint64_t opc_i29,
                               TCGArg ret, TCGArg arg)
{
    tcg_out_bundle(s, miI,
                   INSN_NOP_M,
                   INSN_NOP_I,
                   tcg_opc_i29(TCG_REG_P0, opc_i29, ret, arg));
}

static inline uint64_t tcg_opc_bswap64_i(int qp, TCGReg d, TCGReg s)
{
    return tcg_opc_i3(qp, OPC_MUX1_I3, d, s, 0xb);
}

static inline void tcg_out_bswap16(TCGContext *s, TCGArg ret, TCGArg arg)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12, ret, arg, 15, 15),
                   tcg_opc_bswap64_i(TCG_REG_P0, ret, ret));
}

static inline void tcg_out_bswap32(TCGContext *s, TCGArg ret, TCGArg arg)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12, ret, arg, 31, 31),
                   tcg_opc_bswap64_i(TCG_REG_P0, ret, ret));
}

static inline void tcg_out_bswap64(TCGContext *s, TCGArg ret, TCGArg arg)
{
    tcg_out_bundle(s, miI,
                   INSN_NOP_M,
                   INSN_NOP_I,
                   tcg_opc_bswap64_i(TCG_REG_P0, ret, arg));
}

static inline void tcg_out_deposit(TCGContext *s, TCGArg ret, TCGArg a1,
                                   TCGArg a2, int const_a2, int pos, int len)
{
    uint64_t i1 = 0, i2 = 0;
    int cpos = 63 - pos, lm1 = len - 1;

    if (const_a2) {
        /* Truncate the value of a constant a2 to the width of the field.  */
        int mask = (1u << len) - 1;
        a2 &= mask;

        if (a2 == 0 || a2 == mask) {
            /* 1-bit signed constant inserted into register.  */
            i2 = tcg_opc_i14(TCG_REG_P0, OPC_DEP_I14, ret, a2, a1, cpos, lm1);
        } else {
            /* Otherwise, load any constant into a temporary.  Do this into
               the first I slot to help out with cross-unit delays.  */
            i1 = tcg_opc_movi_a(TCG_REG_P0, TCG_REG_R2, a2);
            a2 = TCG_REG_R2;
        }
    }
    if (i2 == 0) {
        i2 = tcg_opc_i15(TCG_REG_P0, OPC_DEP_I15, ret, a2, a1, cpos, lm1);
    }
    tcg_out_bundle(s, (i1 ? mII : miI),
                   INSN_NOP_M,
                   i1 ? i1 : INSN_NOP_I,
                   i2);
}

static inline uint64_t tcg_opc_cmp_a(int qp, TCGCond cond, TCGArg arg1,
                                     TCGArg arg2, int cmp4)
{
    uint64_t opc_eq_a6, opc_lt_a6, opc_ltu_a6;

    if (cmp4) {
        opc_eq_a6 = OPC_CMP4_EQ_A6;
        opc_lt_a6 = OPC_CMP4_LT_A6;
        opc_ltu_a6 = OPC_CMP4_LTU_A6;
    } else {
        opc_eq_a6 = OPC_CMP_EQ_A6;
        opc_lt_a6 = OPC_CMP_LT_A6;
        opc_ltu_a6 = OPC_CMP_LTU_A6;
    }

    switch (cond) {
    case TCG_COND_EQ:
        return tcg_opc_a6 (qp, opc_eq_a6,  TCG_REG_P6, TCG_REG_P7, arg1, arg2);
    case TCG_COND_NE:
        return tcg_opc_a6 (qp, opc_eq_a6,  TCG_REG_P7, TCG_REG_P6, arg1, arg2);
    case TCG_COND_LT:
        return tcg_opc_a6 (qp, opc_lt_a6,  TCG_REG_P6, TCG_REG_P7, arg1, arg2);
    case TCG_COND_LTU:
        return tcg_opc_a6 (qp, opc_ltu_a6, TCG_REG_P6, TCG_REG_P7, arg1, arg2);
    case TCG_COND_GE:
        return tcg_opc_a6 (qp, opc_lt_a6,  TCG_REG_P7, TCG_REG_P6, arg1, arg2);
    case TCG_COND_GEU:
        return tcg_opc_a6 (qp, opc_ltu_a6, TCG_REG_P7, TCG_REG_P6, arg1, arg2);
    case TCG_COND_LE:
        return tcg_opc_a6 (qp, opc_lt_a6,  TCG_REG_P7, TCG_REG_P6, arg2, arg1);
    case TCG_COND_LEU:
        return tcg_opc_a6 (qp, opc_ltu_a6, TCG_REG_P7, TCG_REG_P6, arg2, arg1);
    case TCG_COND_GT:
        return tcg_opc_a6 (qp, opc_lt_a6,  TCG_REG_P6, TCG_REG_P7, arg2, arg1);
    case TCG_COND_GTU:
        return tcg_opc_a6 (qp, opc_ltu_a6, TCG_REG_P6, TCG_REG_P7, arg2, arg1);
    default:
        tcg_abort();
        break;
    }
}

static inline void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                                  TCGReg arg2, int label_index, int cmp4)
{
    TCGLabel *l = &s->labels[label_index];

    tcg_out_bundle(s, miB,
                   INSN_NOP_M,
                   tcg_opc_cmp_a(TCG_REG_P0, cond, arg1, arg2, cmp4),
                   tcg_opc_b1(TCG_REG_P6, OPC_BR_DPTK_FEW_B1,
                              get_reloc_pcrel21b(s->code_ptr + 2)));

    if (l->has_value) {
        reloc_pcrel21b((s->code_ptr - 16) + 2, l->u.value);
    } else {
        tcg_out_reloc(s, (s->code_ptr - 16) + 2,
                      R_IA64_PCREL21B, label_index, 0);
    }
}

static inline void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGArg ret,
                                   TCGArg arg1, TCGArg arg2, int cmp4)
{
    tcg_out_bundle(s, MmI,
                   tcg_opc_cmp_a(TCG_REG_P0, cond, arg1, arg2, cmp4),
                   tcg_opc_movi_a(TCG_REG_P6, ret, 1),
                   tcg_opc_movi_a(TCG_REG_P7, ret, 0));
}

static inline void tcg_out_movcond(TCGContext *s, TCGCond cond, TCGArg ret,
                                   TCGArg c1, TCGArg c2,
                                   TCGArg v1, int const_v1,
                                   TCGArg v2, int const_v2, int cmp4)
{
    uint64_t opc1, opc2;

    if (const_v1) {
        opc1 = tcg_opc_movi_a(TCG_REG_P6, ret, v1);
    } else if (ret == v1) {
        opc1 = INSN_NOP_M;
    } else {
        opc1 = tcg_opc_mov_a(TCG_REG_P6, ret, v1);
    }
    if (const_v2) {
        opc2 = tcg_opc_movi_a(TCG_REG_P7, ret, v2);
    } else if (ret == v2) {
        opc2 = INSN_NOP_I;
    } else {
        opc2 = tcg_opc_mov_a(TCG_REG_P7, ret, v2);
    }

    tcg_out_bundle(s, MmI,
                   tcg_opc_cmp_a(TCG_REG_P0, cond, c1, c2, cmp4),
                   opc1,
                   opc2);
}

#if defined(CONFIG_SOFTMMU)
/* Load and compare a TLB entry, and return the result in (p6, p7).
   R2 is loaded with the address of the addend TLB entry.
   R57 is loaded with the address, zero extented on 32-bit targets. */
static inline void tcg_out_qemu_tlb(TCGContext *s, TCGArg addr_reg,
                                    TCGMemOp s_bits, uint64_t offset_rw,
                                    uint64_t offset_addend)
{
    tcg_out_bundle(s, mII,
                   INSN_NOP_M,
                   tcg_opc_i11(TCG_REG_P0, OPC_EXTR_U_I11, TCG_REG_R2,
                               addr_reg, TARGET_PAGE_BITS, CPU_TLB_BITS - 1),
                   tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12, TCG_REG_R2,
                               TCG_REG_R2, 63 - CPU_TLB_ENTRY_BITS,
                               63 - CPU_TLB_ENTRY_BITS));
    tcg_out_bundle(s, mII,
                   tcg_opc_a5 (TCG_REG_P0, OPC_ADDL_A5, TCG_REG_R2,
                               offset_rw, TCG_REG_R2),
                   tcg_opc_ext_i(TCG_REG_P0,
                                 TARGET_LONG_BITS == 32 ? MO_UL : MO_Q,
                                 TCG_REG_R57, addr_reg),
                   tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1, TCG_REG_R2,
                               TCG_REG_R2, TCG_AREG0));
    tcg_out_bundle(s, mII,
                   tcg_opc_m3 (TCG_REG_P0,
                               (TARGET_LONG_BITS == 32
                                ? OPC_LD4_M3 : OPC_LD8_M3), TCG_REG_R56,
                               TCG_REG_R2, offset_addend - offset_rw),
                   tcg_opc_i14(TCG_REG_P0, OPC_DEP_I14, TCG_REG_R3, 0,
                               TCG_REG_R57, 63 - s_bits,
                               TARGET_PAGE_BITS - s_bits - 1),
                   tcg_opc_a6 (TCG_REG_P0, OPC_CMP_EQ_A6, TCG_REG_P6,
                               TCG_REG_P7, TCG_REG_R3, TCG_REG_R56));
}

/* helper signature: helper_ld_mmu(CPUState *env, target_ulong addr,
   int mmu_idx) */
static const void * const qemu_ld_helpers[4] = {
    helper_ldb_mmu,
    helper_ldw_mmu,
    helper_ldl_mmu,
    helper_ldq_mmu,
};

static inline void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                                   TCGMemOp opc)
{
    static const uint64_t opc_ld_m1[4] = {
        OPC_LD1_M1, OPC_LD2_M1, OPC_LD4_M1, OPC_LD8_M1
    };
    int addr_reg, data_reg, mem_index;
    TCGMemOp s_bits, bswap;

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    s_bits = opc & MO_SIZE;
    bswap = opc & MO_BSWAP;

    /* Read the TLB entry */
    tcg_out_qemu_tlb(s, addr_reg, s_bits,
                     offsetof(CPUArchState, tlb_table[mem_index][0].addr_read),
                     offsetof(CPUArchState, tlb_table[mem_index][0].addend));

    /* P6 is the fast path, and P7 the slow path */
    tcg_out_bundle(s, mLX,
                   tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R56, TCG_AREG0),
                   tcg_opc_l2 ((tcg_target_long) qemu_ld_helpers[s_bits]),
                   tcg_opc_x2 (TCG_REG_P7, OPC_MOVL_X2, TCG_REG_R2,
                               (tcg_target_long) qemu_ld_helpers[s_bits]));
    tcg_out_bundle(s, MmI,
                   tcg_opc_m3 (TCG_REG_P0, OPC_LD8_M3, TCG_REG_R3,
                               TCG_REG_R2, 8),
                   tcg_opc_a1 (TCG_REG_P6, OPC_ADD_A1, TCG_REG_R3,
                               TCG_REG_R3, TCG_REG_R57),
                   tcg_opc_i21(TCG_REG_P7, OPC_MOV_I21, TCG_REG_B6,
                               TCG_REG_R3, 0));
    if (bswap && s_bits == MO_16) {
        tcg_out_bundle(s, MmI,
                       tcg_opc_m1 (TCG_REG_P6, opc_ld_m1[s_bits],
                                   TCG_REG_R8, TCG_REG_R3),
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1, TCG_REG_R1, TCG_REG_R2),
                       tcg_opc_i12(TCG_REG_P6, OPC_DEP_Z_I12,
                                   TCG_REG_R8, TCG_REG_R8, 15, 15));
    } else if (bswap && s_bits == MO_32) {
        tcg_out_bundle(s, MmI,
                       tcg_opc_m1 (TCG_REG_P6, opc_ld_m1[s_bits],
                                   TCG_REG_R8, TCG_REG_R3),
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1, TCG_REG_R1, TCG_REG_R2),
                       tcg_opc_i12(TCG_REG_P6, OPC_DEP_Z_I12,
                                   TCG_REG_R8, TCG_REG_R8, 31, 31));
    } else {
        tcg_out_bundle(s, mmI,
                       tcg_opc_m1 (TCG_REG_P6, opc_ld_m1[s_bits],
                                   TCG_REG_R8, TCG_REG_R3),
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1, TCG_REG_R1, TCG_REG_R2),
                       INSN_NOP_I);
    }
    if (!bswap) {
        tcg_out_bundle(s, miB,
                       tcg_opc_movi_a(TCG_REG_P7, TCG_REG_R58, mem_index),
                       INSN_NOP_I,
                       tcg_opc_b5 (TCG_REG_P7, OPC_BR_CALL_SPTK_MANY_B5,
                                   TCG_REG_B0, TCG_REG_B6));
    } else {
        tcg_out_bundle(s, miB,
                       tcg_opc_movi_a(TCG_REG_P7, TCG_REG_R58, mem_index),
                       tcg_opc_bswap64_i(TCG_REG_P6, TCG_REG_R8, TCG_REG_R8),
                       tcg_opc_b5 (TCG_REG_P7, OPC_BR_CALL_SPTK_MANY_B5,
                                   TCG_REG_B0, TCG_REG_B6));
    }

    tcg_out_bundle(s, miI,
                   INSN_NOP_M,
                   INSN_NOP_I,
                   tcg_opc_ext_i(TCG_REG_P0, opc, data_reg, TCG_REG_R8));
}

/* helper signature: helper_st_mmu(CPUState *env, target_ulong addr,
   uintxx_t val, int mmu_idx) */
static const void * const qemu_st_helpers[4] = {
    helper_stb_mmu,
    helper_stw_mmu,
    helper_stl_mmu,
    helper_stq_mmu,
};

static inline void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                                   TCGMemOp opc)
{
    static const uint64_t opc_st_m4[4] = {
        OPC_ST1_M4, OPC_ST2_M4, OPC_ST4_M4, OPC_ST8_M4
    };
    int addr_reg, data_reg, mem_index;
    TCGMemOp s_bits;

    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    s_bits = opc & MO_SIZE;

    tcg_out_qemu_tlb(s, addr_reg, s_bits,
                     offsetof(CPUArchState, tlb_table[mem_index][0].addr_write),
                     offsetof(CPUArchState, tlb_table[mem_index][0].addend));

    /* P6 is the fast path, and P7 the slow path */
    tcg_out_bundle(s, mLX,
                   tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R56, TCG_AREG0),
                   tcg_opc_l2 ((tcg_target_long) qemu_st_helpers[s_bits]),
                   tcg_opc_x2 (TCG_REG_P7, OPC_MOVL_X2, TCG_REG_R2,
                               (tcg_target_long) qemu_st_helpers[s_bits]));
    tcg_out_bundle(s, MmI,
                   tcg_opc_m3 (TCG_REG_P0, OPC_LD8_M3, TCG_REG_R3,
                               TCG_REG_R2, 8),
                   tcg_opc_a1 (TCG_REG_P6, OPC_ADD_A1, TCG_REG_R3,
                               TCG_REG_R3, TCG_REG_R57),
                   tcg_opc_i21(TCG_REG_P7, OPC_MOV_I21, TCG_REG_B6,
                               TCG_REG_R3, 0));

    switch (opc) {
    case MO_8:
    case MO_16:
    case MO_32:
    case MO_64:
        tcg_out_bundle(s, mii,
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1,
                                   TCG_REG_R1, TCG_REG_R2),
                       tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R58, data_reg),
                       INSN_NOP_I);
        break;

    case MO_16 | MO_BSWAP:
        tcg_out_bundle(s, miI,
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1,
                                   TCG_REG_R1, TCG_REG_R2),
                       INSN_NOP_I,
                       tcg_opc_i12(TCG_REG_P6, OPC_DEP_Z_I12,
                                   TCG_REG_R2, data_reg, 15, 15));
        tcg_out_bundle(s, miI,
                       tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R58, data_reg),
                       INSN_NOP_I,
                       tcg_opc_bswap64_i(TCG_REG_P6, TCG_REG_R2, TCG_REG_R2));
        data_reg = TCG_REG_R2;
        break;

    case MO_32 | MO_BSWAP:
        tcg_out_bundle(s, miI,
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1,
                                   TCG_REG_R1, TCG_REG_R2),
                       INSN_NOP_I,
                       tcg_opc_i12(TCG_REG_P6, OPC_DEP_Z_I12,
                                   TCG_REG_R2, data_reg, 31, 31));
        tcg_out_bundle(s, miI,
                       tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R58, data_reg),
                       INSN_NOP_I,
                       tcg_opc_bswap64_i(TCG_REG_P6, TCG_REG_R2, TCG_REG_R2));
        data_reg = TCG_REG_R2;
        break;

    case MO_64 | MO_BSWAP:
        tcg_out_bundle(s, miI,
                       tcg_opc_m1 (TCG_REG_P7, OPC_LD8_M1,
                                   TCG_REG_R1, TCG_REG_R2),
                       tcg_opc_mov_a(TCG_REG_P7, TCG_REG_R58, data_reg),
                       tcg_opc_bswap64_i(TCG_REG_P6, TCG_REG_R2, data_reg));
        data_reg = TCG_REG_R2;
        break;

    default:
        tcg_abort();
    }

    tcg_out_bundle(s, miB,
                   tcg_opc_m4 (TCG_REG_P6, opc_st_m4[s_bits],
                               data_reg, TCG_REG_R3),
                   tcg_opc_movi_a(TCG_REG_P7, TCG_REG_R59, mem_index),
                   tcg_opc_b5 (TCG_REG_P7, OPC_BR_CALL_SPTK_MANY_B5,
                               TCG_REG_B0, TCG_REG_B6));
}

#else /* !CONFIG_SOFTMMU */

static inline void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args,
                                   TCGMemOp opc)
{
    static uint64_t const opc_ld_m1[4] = {
        OPC_LD1_M1, OPC_LD2_M1, OPC_LD4_M1, OPC_LD8_M1
    };
    int addr_reg, data_reg;
    TCGMemOp s_bits, bswap;

    data_reg = *args++;
    addr_reg = *args++;
    s_bits = opc & MO_SIZE;
    bswap = opc & MO_BSWAP;

#if TARGET_LONG_BITS == 32
    if (GUEST_BASE != 0) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i29(TCG_REG_P0, OPC_ZXT4_I29,
                                   TCG_REG_R3, addr_reg),
                       tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1, TCG_REG_R2,
                                   TCG_GUEST_BASE_REG, TCG_REG_R3));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       tcg_opc_i29(TCG_REG_P0, OPC_ZXT4_I29,
                                   TCG_REG_R2, addr_reg),
                       INSN_NOP_I);
    }

    if (!bswap) {
        if (!(opc & MO_SIGN)) {
            tcg_out_bundle(s, miI,
                           tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                       data_reg, TCG_REG_R2),
                           INSN_NOP_I,
                           INSN_NOP_I);
        } else {
            tcg_out_bundle(s, mII,
                           tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                       data_reg, TCG_REG_R2),
                           INSN_NOP_I,
                           tcg_opc_ext_i(TCG_REG_P0, opc, data_reg, data_reg));
        }
    } else if (s_bits == MO_64) {
            tcg_out_bundle(s, mII,
                           tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                       data_reg, TCG_REG_R2),
                           INSN_NOP_I,
                           tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg));
    } else {
        if (s_bits == MO_16) {
            tcg_out_bundle(s, mII,
                           tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                       data_reg, TCG_REG_R2),
                           INSN_NOP_I,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                      data_reg, data_reg, 15, 15));
        } else {
            tcg_out_bundle(s, mII,
                           tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                       data_reg, TCG_REG_R2),
                           INSN_NOP_I,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                      data_reg, data_reg, 31, 31));
        }
        if (!(opc & MO_SIGN)) {
            tcg_out_bundle(s, miI,
                           INSN_NOP_M,
                           INSN_NOP_I,
                           tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg));
        } else {
            tcg_out_bundle(s, mII,
                           INSN_NOP_M,
                           tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg),
                           tcg_opc_ext_i(TCG_REG_P0, opc, data_reg, data_reg));
        }
    }
#else
    if (GUEST_BASE != 0) {
        tcg_out_bundle(s, MmI,
                       tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1, TCG_REG_R2,
                                   TCG_GUEST_BASE_REG, addr_reg),
                       tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                   data_reg, TCG_REG_R2),
                       INSN_NOP_I);
    } else {
        tcg_out_bundle(s, mmI,
                       INSN_NOP_M,
                       tcg_opc_m1 (TCG_REG_P0, opc_ld_m1[s_bits],
                                   data_reg, addr_reg),
                       INSN_NOP_I);
    }

    if (bswap && s_bits == MO_16) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                   data_reg, data_reg, 15, 15),
                       tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg));
    } else if (bswap && s_bits == MO_32) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                   data_reg, data_reg, 31, 31),
                       tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg));
    } else if (bswap && s_bits == MO_64) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_bswap64_i(TCG_REG_P0, data_reg, data_reg));
    }
    if (opc & MO_SIGN) {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       INSN_NOP_I,
                       tcg_opc_ext_i(TCG_REG_P0, opc, data_reg, data_reg));
    }
#endif
}

static inline void tcg_out_qemu_st(TCGContext *s, const TCGArg *args,
                                   TCGMemOp opc)
{
    static uint64_t const opc_st_m4[4] = {
        OPC_ST1_M4, OPC_ST2_M4, OPC_ST4_M4, OPC_ST8_M4
    };
    int addr_reg, data_reg;
#if TARGET_LONG_BITS == 64
    uint64_t add_guest_base;
#endif
    TCGMemOp s_bits, bswap;

    data_reg = *args++;
    addr_reg = *args++;
    s_bits = opc & MO_SIZE;
    bswap = opc & MO_BSWAP;

#if TARGET_LONG_BITS == 32
    if (GUEST_BASE != 0) {
        tcg_out_bundle(s, mII,
                       INSN_NOP_M,
                       tcg_opc_i29(TCG_REG_P0, OPC_ZXT4_I29,
                                   TCG_REG_R3, addr_reg),
                       tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1, TCG_REG_R2,
                                   TCG_GUEST_BASE_REG, TCG_REG_R3));
    } else {
        tcg_out_bundle(s, miI,
                       INSN_NOP_M,
                       tcg_opc_i29(TCG_REG_P0, OPC_ZXT4_I29,
                                   TCG_REG_R2, addr_reg),
                       INSN_NOP_I);
    }

    if (bswap) {
        if (s_bits == MO_16) {
            tcg_out_bundle(s, mII,
                           INSN_NOP_M,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                       TCG_REG_R3, data_reg, 15, 15),
                           tcg_opc_bswap64_i(TCG_REG_P0,
                                             TCG_REG_R3, TCG_REG_R3));
            data_reg = TCG_REG_R3;
        } else if (s_bits == MO_32) {
            tcg_out_bundle(s, mII,
                           INSN_NOP_M,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                       TCG_REG_R3, data_reg, 31, 31),
                           tcg_opc_bswap64_i(TCG_REG_P0,
                                             TCG_REG_R3, TCG_REG_R3));
            data_reg = TCG_REG_R3;
        } else if (s_bits == MO_64) {
            tcg_out_bundle(s, miI,
                           INSN_NOP_M,
                           INSN_NOP_I,
                           tcg_opc_bswap64_i(TCG_REG_P0, TCG_REG_R3, data_reg));
            data_reg = TCG_REG_R3;
        }
    }
    tcg_out_bundle(s, mmI,
                   tcg_opc_m4 (TCG_REG_P0, opc_st_m4[s_bits],
                               data_reg, TCG_REG_R2),
                   INSN_NOP_M,
                   INSN_NOP_I);
#else
    if (GUEST_BASE != 0) {
        add_guest_base = tcg_opc_a1 (TCG_REG_P0, OPC_ADD_A1, TCG_REG_R2,
                                     TCG_GUEST_BASE_REG, addr_reg);
        addr_reg = TCG_REG_R2;
    } else {
        add_guest_base = INSN_NOP_M;
    }

    if (!bswap) {
        tcg_out_bundle(s, (GUEST_BASE ? MmI : mmI),
                       add_guest_base,
                       tcg_opc_m4 (TCG_REG_P0, opc_st_m4[s_bits],
                                   data_reg, addr_reg),
                       INSN_NOP_I);
    } else {
        if (s_bits == MO_16) {
            tcg_out_bundle(s, mII,
                           add_guest_base,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                       TCG_REG_R3, data_reg, 15, 15),
                           tcg_opc_bswap64_i(TCG_REG_P0,
                                             TCG_REG_R3, TCG_REG_R3));
            data_reg = TCG_REG_R3;
        } else if (s_bits == MO_32) {
            tcg_out_bundle(s, mII,
                           add_guest_base,
                           tcg_opc_i12(TCG_REG_P0, OPC_DEP_Z_I12,
                                       TCG_REG_R3, data_reg, 31, 31),
                           tcg_opc_bswap64_i(TCG_REG_P0,
                                             TCG_REG_R3, TCG_REG_R3));
            data_reg = TCG_REG_R3;
        } else if (s_bits == MO_64) {
            tcg_out_bundle(s, miI,
                           add_guest_base,
                           INSN_NOP_I,
                           tcg_opc_bswap64_i(TCG_REG_P0, TCG_REG_R3, data_reg));
            data_reg = TCG_REG_R3;
        }
        tcg_out_bundle(s, miI,
                       tcg_opc_m4 (TCG_REG_P0, opc_st_m4[s_bits],
                                   data_reg, addr_reg),
                       INSN_NOP_I,
                       INSN_NOP_I);
    }
#endif
}

#endif

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg *args, const int *const_args)
{
    switch(opc) {
    case INDEX_op_exit_tb:
        tcg_out_exit_tb(s, args[0]);
        break;
    case INDEX_op_br:
        tcg_out_br(s, args[0]);
        break;
    case INDEX_op_call:
        if (likely(const_args[0])) {
            tcg_out_calli(s, args[0]);
        } else {
            tcg_out_callr(s, args[0]);
        }
        break;
    case INDEX_op_goto_tb:
        tcg_out_goto_tb(s, args[0]);
        break;

    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ld_rel(s, OPC_LD1_M1, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        tcg_out_ld_rel(s, OPC_LD1_M1, args[0], args[1], args[2]);
        tcg_out_ext(s, OPC_SXT1_I29, args[0], args[0]);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ld_rel(s, OPC_LD2_M1, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        tcg_out_ld_rel(s, OPC_LD2_M1, args[0], args[1], args[2]);
        tcg_out_ext(s, OPC_SXT2_I29, args[0], args[0]);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_ld_rel(s, OPC_LD4_M1, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ld_rel(s, OPC_LD4_M1, args[0], args[1], args[2]);
        tcg_out_ext(s, OPC_SXT4_I29, args[0], args[0]);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ld_rel(s, OPC_LD8_M1, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_st_rel(s, OPC_ST1_M4, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_st_rel(s, OPC_ST2_M4, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_st_rel(s, OPC_ST4_M4, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i64:
        tcg_out_st_rel(s, OPC_ST8_M4, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
        tcg_out_add(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        tcg_out_sub(s, args[0], args[1], const_args[1], args[2], const_args[2]);
        break;

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        /* TCG expects arg2 constant; A3 expects arg1 constant.  Swap.  */
        tcg_out_alu(s, OPC_AND_A1, OPC_AND_A3, args[0],
                    args[2], const_args[2], args[1], const_args[1]);
        break;
    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
        tcg_out_alu(s, OPC_ANDCM_A1, OPC_ANDCM_A3, args[0],
                    args[1], const_args[1], args[2], const_args[2]);
        break;
    case INDEX_op_eqv_i32:
    case INDEX_op_eqv_i64:
        tcg_out_eqv(s, args[0], args[1], const_args[1],
                    args[2], const_args[2]);
        break;
    case INDEX_op_nand_i32:
    case INDEX_op_nand_i64:
        tcg_out_nand(s, args[0], args[1], const_args[1],
                     args[2], const_args[2]);
        break;
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
        tcg_out_nor(s, args[0], args[1], const_args[1],
                    args[2], const_args[2]);
        break;
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
        /* TCG expects arg2 constant; A3 expects arg1 constant.  Swap.  */
        tcg_out_alu(s, OPC_OR_A1, OPC_OR_A3, args[0],
                    args[2], const_args[2], args[1], const_args[1]);
        break;
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
        tcg_out_orc(s, args[0], args[1], const_args[1],
                    args[2], const_args[2]);
        break;
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        /* TCG expects arg2 constant; A3 expects arg1 constant.  Swap.  */
        tcg_out_alu(s, OPC_XOR_A1, OPC_XOR_A3, args[0],
                    args[2], const_args[2], args[1], const_args[1]);
        break;

    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
        tcg_out_mul(s, args[0], args[1], args[2]);
        break;

    case INDEX_op_sar_i32:
        tcg_out_sar_i32(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_sar_i64:
        tcg_out_sar_i64(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_shl_i32:
        tcg_out_shl_i32(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_shl_i64:
        tcg_out_shl_i64(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_shr_i32:
        tcg_out_shr_i32(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_shr_i64:
        tcg_out_shr_i64(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_rotl_i32:
        tcg_out_rotl_i32(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_rotl_i64:
        tcg_out_rotl_i64(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_rotr_i32:
        tcg_out_rotr_i32(s, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_rotr_i64:
        tcg_out_rotr_i64(s, args[0], args[1], args[2], const_args[2]);
        break;

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        tcg_out_ext(s, OPC_SXT1_I29, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
        tcg_out_ext(s, OPC_ZXT1_I29, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        tcg_out_ext(s, OPC_SXT2_I29, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
        tcg_out_ext(s, OPC_ZXT2_I29, args[0], args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tcg_out_ext(s, OPC_SXT4_I29, args[0], args[1]);
        break;
    case INDEX_op_ext32u_i64:
        tcg_out_ext(s, OPC_ZXT4_I29, args[0], args[1]);
        break;

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        tcg_out_bswap16(s, args[0], args[1]);
        break;
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
        tcg_out_bswap32(s, args[0], args[1]);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_bswap64(s, args[0], args[1]);
        break;

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        tcg_out_deposit(s, args[0], args[1], args[2], const_args[2],
                        args[3], args[4]);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], args[3], 1);
        break;
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, args[2], args[0], args[1], args[3], 0);
        break;
    case INDEX_op_setcond_i32:
        tcg_out_setcond(s, args[3], args[0], args[1], args[2], 1);
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond(s, args[3], args[0], args[1], args[2], 0);
        break;
    case INDEX_op_movcond_i32:
        tcg_out_movcond(s, args[5], args[0], args[1], args[2],
                        args[3], const_args[3], args[4], const_args[4], 1);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond(s, args[5], args[0], args[1], args[2],
                        args[3], const_args[3], args[4], const_args[4], 0);
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, MO_UB);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, MO_SB);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, MO_TEUW);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, MO_TESW);
        break;
    case INDEX_op_qemu_ld32:
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, args, MO_TEUL);
        break;
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, MO_TESL);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, MO_TEQ);
        break;

    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, args, MO_UB);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, args, MO_TEUW);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, args, MO_TEUL);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, MO_TEQ);
        break;

    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef ia64_op_defs[] = {
    { INDEX_op_br, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "rZ", "r" } },
    { INDEX_op_st16_i32, { "rZ", "r" } },
    { INDEX_op_st_i32, { "rZ", "r" } },

    { INDEX_op_add_i32, { "r", "rZ", "rI" } },
    { INDEX_op_sub_i32, { "r", "rI", "rI" } },

    { INDEX_op_and_i32, { "r", "rI", "rI" } },
    { INDEX_op_andc_i32, { "r", "rI", "rI" } },
    { INDEX_op_eqv_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_nand_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_nor_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_or_i32, { "r", "rI", "rI" } },
    { INDEX_op_orc_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_xor_i32, { "r", "rI", "rI" } },

    { INDEX_op_mul_i32, { "r", "rZ", "rZ" } },

    { INDEX_op_sar_i32, { "r", "rZ", "ri" } },
    { INDEX_op_shl_i32, { "r", "rZ", "ri" } },
    { INDEX_op_shr_i32, { "r", "rZ", "ri" } },
    { INDEX_op_rotl_i32, { "r", "rZ", "ri" } },
    { INDEX_op_rotr_i32, { "r", "rZ", "ri" } },

    { INDEX_op_ext8s_i32, { "r", "rZ"} },
    { INDEX_op_ext8u_i32, { "r", "rZ"} },
    { INDEX_op_ext16s_i32, { "r", "rZ"} },
    { INDEX_op_ext16u_i32, { "r", "rZ"} },

    { INDEX_op_bswap16_i32, { "r", "rZ" } },
    { INDEX_op_bswap32_i32, { "r", "rZ" } },

    { INDEX_op_brcond_i32, { "rZ", "rZ" } },
    { INDEX_op_setcond_i32, { "r", "rZ", "rZ" } },
    { INDEX_op_movcond_i32, { "r", "rZ", "rZ", "rI", "rI" } },

    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },
    { INDEX_op_st8_i64, { "rZ", "r" } },
    { INDEX_op_st16_i64, { "rZ", "r" } },
    { INDEX_op_st32_i64, { "rZ", "r" } },
    { INDEX_op_st_i64, { "rZ", "r" } },

    { INDEX_op_add_i64, { "r", "rZ", "rI" } },
    { INDEX_op_sub_i64, { "r", "rI", "rI" } },

    { INDEX_op_and_i64, { "r", "rI", "rI" } },
    { INDEX_op_andc_i64, { "r", "rI", "rI" } },
    { INDEX_op_eqv_i64, { "r", "rZ", "rZ" } },
    { INDEX_op_nand_i64, { "r", "rZ", "rZ" } },
    { INDEX_op_nor_i64, { "r", "rZ", "rZ" } },
    { INDEX_op_or_i64, { "r", "rI", "rI" } },
    { INDEX_op_orc_i64, { "r", "rZ", "rZ" } },
    { INDEX_op_xor_i64, { "r", "rI", "rI" } },

    { INDEX_op_mul_i64, { "r", "rZ", "rZ" } },

    { INDEX_op_sar_i64, { "r", "rZ", "ri" } },
    { INDEX_op_shl_i64, { "r", "rZ", "ri" } },
    { INDEX_op_shr_i64, { "r", "rZ", "ri" } },
    { INDEX_op_rotl_i64, { "r", "rZ", "ri" } },
    { INDEX_op_rotr_i64, { "r", "rZ", "ri" } },

    { INDEX_op_ext8s_i64, { "r", "rZ"} },
    { INDEX_op_ext8u_i64, { "r", "rZ"} },
    { INDEX_op_ext16s_i64, { "r", "rZ"} },
    { INDEX_op_ext16u_i64, { "r", "rZ"} },
    { INDEX_op_ext32s_i64, { "r", "rZ"} },
    { INDEX_op_ext32u_i64, { "r", "rZ"} },

    { INDEX_op_bswap16_i64, { "r", "rZ" } },
    { INDEX_op_bswap32_i64, { "r", "rZ" } },
    { INDEX_op_bswap64_i64, { "r", "rZ" } },

    { INDEX_op_brcond_i64, { "rZ", "rZ" } },
    { INDEX_op_setcond_i64, { "r", "rZ", "rZ" } },
    { INDEX_op_movcond_i64, { "r", "rZ", "rZ", "rI", "rI" } },

    { INDEX_op_deposit_i32, { "r", "rZ", "ri" } },
    { INDEX_op_deposit_i64, { "r", "rZ", "ri" } },

    { INDEX_op_qemu_ld8u, { "r", "r" } },
    { INDEX_op_qemu_ld8s, { "r", "r" } },
    { INDEX_op_qemu_ld16u, { "r", "r" } },
    { INDEX_op_qemu_ld16s, { "r", "r" } },
    { INDEX_op_qemu_ld32, { "r", "r" } },
    { INDEX_op_qemu_ld32u, { "r", "r" } },
    { INDEX_op_qemu_ld32s, { "r", "r" } },
    { INDEX_op_qemu_ld64, { "r", "r" } },

    { INDEX_op_qemu_st8, { "SZ", "r" } },
    { INDEX_op_qemu_st16, { "SZ", "r" } },
    { INDEX_op_qemu_st32, { "SZ", "r" } },
    { INDEX_op_qemu_st64, { "SZ", "r" } },

    { -1 },
};

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int frame_size;

    /* reserve some stack space */
    frame_size = TCG_STATIC_CALL_ARGS_SIZE +
                 CPU_TEMP_BUF_NLONGS * sizeof(long);
    frame_size = (frame_size + TCG_TARGET_STACK_ALIGN - 1) &
                 ~(TCG_TARGET_STACK_ALIGN - 1);
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    /* First emit adhoc function descriptor */
    *(uint64_t *)(s->code_ptr) = (uint64_t)s->code_ptr + 16; /* entry point */
    s->code_ptr += 16; /* skip GP */

    /* prologue */
    tcg_out_bundle(s, miI,
                   tcg_opc_m34(TCG_REG_P0, OPC_ALLOC_M34,
                               TCG_REG_R34, 32, 24, 0),
                   INSN_NOP_I,
                   tcg_opc_i21(TCG_REG_P0, OPC_MOV_I21,
                               TCG_REG_B6, TCG_REG_R33, 0));

    /* ??? If GUEST_BASE < 0x200000, we could load the register via
       an ADDL in the M slot of the next bundle.  */
    if (GUEST_BASE != 0) {
        tcg_out_bundle(s, mlx,
                       INSN_NOP_M,
                       tcg_opc_l2 (GUEST_BASE),
                       tcg_opc_x2 (TCG_REG_P0, OPC_MOVL_X2,
                                   TCG_GUEST_BASE_REG, GUEST_BASE));
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }

    tcg_out_bundle(s, miB,
                   tcg_opc_a4 (TCG_REG_P0, OPC_ADDS_A4,
                               TCG_REG_R12, -frame_size, TCG_REG_R12),
                   tcg_opc_i22(TCG_REG_P0, OPC_MOV_I22,
                               TCG_REG_R33, TCG_REG_B0),
                   tcg_opc_b4 (TCG_REG_P0, OPC_BR_SPTK_MANY_B4, TCG_REG_B6));

    /* epilogue */
    tb_ret_addr = s->code_ptr;
    tcg_out_bundle(s, miI,
                   INSN_NOP_M,
                   tcg_opc_i21(TCG_REG_P0, OPC_MOV_I21,
                               TCG_REG_B0, TCG_REG_R33, 0),
                   tcg_opc_a4 (TCG_REG_P0, OPC_ADDS_A4,
                               TCG_REG_R12, frame_size, TCG_REG_R12));
    tcg_out_bundle(s, miB,
                   INSN_NOP_M,
                   tcg_opc_i26(TCG_REG_P0, OPC_MOV_I_I26,
                               TCG_REG_PFS, TCG_REG_R34),
                   tcg_opc_b4 (TCG_REG_P0, OPC_BR_RET_SPTK_MANY_B4,
                               TCG_REG_B0));
}

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set(tcg_target_available_regs[TCG_TYPE_I32],
                   0xffffffffffffffffull);
    tcg_regset_set(tcg_target_available_regs[TCG_TYPE_I64],
                   0xffffffffffffffffull);

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R8);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R9);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R10);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R11);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R15);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R16);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R17);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R18);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R19);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R20);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R21);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R22);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R23);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R24);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R25);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R26);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R27);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R28);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R29);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R30);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R31);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R56);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R57);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R58);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R59);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R60);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R61);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R62);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R63);

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);   /* zero register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1);   /* global pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R2);   /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R3);   /* internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R12);  /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R13);  /* thread pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R33);  /* return address */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R34);  /* PFS */

    /* The following 4 are not in use, are call-saved, but *not* saved
       by the prologue.  Therefore we cannot use them without modifying
       the prologue.  There doesn't seem to be any good reason to use
       these as opposed to the windowed registers.  */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R4);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R5);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R6);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R7);

    tcg_add_target_add_op_defs(ia64_op_defs);
}

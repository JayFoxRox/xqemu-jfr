/*
 * QEMU Geforce NV2A GPU register defines
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#define NV_PMC_BOOT_0                                    0x00000000
#define NV_PMC_INTR_0                                    0x00000100
#   define NV_PMC_INTR_0_PFIFO                                 (1 << 8)
#   define NV_PMC_INTR_0_PGRAPH                               (1 << 12)
#   define NV_PMC_INTR_0_PCRTC                                (1 << 24)
#   define NV_PMC_INTR_0_PBUS                                 (1 << 28)
#   define NV_PMC_INTR_0_SOFTWARE                             (1 << 31)
#define NV_PMC_INTR_EN_0                                 0x00000140
#   define NV_PMC_INTR_EN_0_HARDWARE                            1
#   define NV_PMC_INTR_EN_0_SOFTWARE                            2
#define NV_PMC_ENABLE                                    0x00000200
#   define NV_PMC_ENABLE_PFIFO                                 (1 << 8)
#   define NV_PMC_ENABLE_PGRAPH                               (1 << 12)


/* These map approximately to the pci registers */
#define NV_PBUS_PCI_NV_0                                 0x00000800
#   define NV_PBUS_PCI_NV_0_VENDOR_ID                         0x0000FFFF
#   define NV_CONFIG_PCI_NV_0_DEVICE_ID                       0xFFFF0000
#define NV_PBUS_PCI_NV_1                                 0x00000804
#define NV_PBUS_PCI_NV_2                                 0x00000808
#   define NV_PBUS_PCI_NV_2_REVISION_ID                       0x000000FF
#   define NV_PBUS_PCI_NV_2_CLASS_CODE                        0xFFFFFF00
#define NV_PBUS_PCI_NV_3                                 0x0000080C
#   define NV_PBUS_PCI_NV_3_LATENCY_TIMER                     0x0000F800
#   define NV_PBUS_PCI_NV_3_HEADER_TYPE                       0x00FF0000
#define NV_PBUS_PCI_NV_4                                 0x00000810
#   define NV_PBUS_PCI_NV_4_SPACE_TYPE                        0x00000001
#   define NV_PBUS_PCI_NV_4_ADDRESS_TYPE                      0x00000006
#   define NV_PBUS_PCI_NV_4_PREFETCHABLE                      0x00000008
#   define NV_PBUS_PCI_NV_4_BASE_ADDRESS                      0xFF000000
#define NV_PBUS_PCI_NV_5                                 0x00000814
#   define NV_PBUS_PCI_NV_5_SPACE_TYPE                        0x00000001
#   define NV_PBUS_PCI_NV_5_ADDRESS_TYPE                      0x00000006
#   define NV_PBUS_PCI_NV_5_PREFETCHABLE                      0x00000008
#   define NV_PBUS_PCI_NV_5_BASE_ADDRESS                      0xFF000000
#define NV_PBUS_PCI_NV_6                                 0x00000818
#   define NV_PBUS_PCI_NV_6_SPACE_TYPE                        0x00000001
#   define NV_PBUS_PCI_NV_6_ADDRESS_TYPE                      0x00000006
#   define NV_PBUS_PCI_NV_6_PREFETCHABLE                      0x00000008
#   define NV_PBUS_PCI_NV_6_BASE_ADDRESS                      0xFFF80000
#define NV_PBUS_PCI_NV_11                                0x0000082C
#   define NV_PBUS_PCI_NV_11_SUBSYSTEM_VENDOR_ID             0x0000FFFF
#   define NV_PBUS_PCI_NV_11_SUBSYSTEM_ID                    0xFFFF0000
#define NV_PBUS_PCI_NV_12                                0x00000830
#   define NV_PBUS_PCI_NV_12_ROM_DECODE                      0x00000001
#   define NV_PBUS_PCI_NV_12_ROM_BASE                        0xFFFF0000
#define NV_PBUS_PCI_NV_13                                0x00000834
#   define NV_PBUS_PCI_NV_13_CAP_PTR                         0x000000FF
#define NV_PBUS_PCI_NV_15                                0x0000083C
#   define NV_PBUS_PCI_NV_15_INTR_LINE                       0x000000FF
#   define NV_PBUS_PCI_NV_15_INTR_PIN                        0x0000FF00
#   define NV_PBUS_PCI_NV_15_MIN_GNT                         0x00FF0000
#   define NV_PBUS_PCI_NV_15_MAX_LAT                         0xFF000000


#define NV_PFIFO_INTR_0                                  0x00000100
#   define NV_PFIFO_INTR_0_CACHE_ERROR                          (1 << 0)
#   define NV_PFIFO_INTR_0_RUNOUT                               (1 << 4)
#   define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW                      (1 << 8)
#   define NV_PFIFO_INTR_0_DMA_PUSHER                          (1 << 12)
#   define NV_PFIFO_INTR_0_DMA_PT                              (1 << 16)
#   define NV_PFIFO_INTR_0_SEMAPHORE                           (1 << 20)
#   define NV_PFIFO_INTR_0_ACQUIRE_TIMEOUT                     (1 << 24)
#define NV_PFIFO_INTR_EN_0                               0x00000140
#   define NV_PFIFO_INTR_EN_0_CACHE_ERROR                       (1 << 0)
#   define NV_PFIFO_INTR_EN_0_RUNOUT                            (1 << 4)
#   define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW                   (1 << 8)
#   define NV_PFIFO_INTR_EN_0_DMA_PUSHER                       (1 << 12)
#   define NV_PFIFO_INTR_EN_0_DMA_PT                           (1 << 16)
#   define NV_PFIFO_INTR_EN_0_SEMAPHORE                        (1 << 20)
#   define NV_PFIFO_INTR_EN_0_ACQUIRE_TIMEOUT                  (1 << 24)
#define NV_PFIFO_RAMHT                                   0x00000210
#   define NV_PFIFO_RAMHT_BASE_ADDRESS                        0x000001F0
#   define NV_PFIFO_RAMHT_SIZE                                0x00030000
#       define NV_PFIFO_RAMHT_SIZE_4K                             0
#       define NV_PFIFO_RAMHT_SIZE_8K                             1
#       define NV_PFIFO_RAMHT_SIZE_16K                            2
#       define NV_PFIFO_RAMHT_SIZE_32K                            3
#   define NV_PFIFO_RAMHT_SEARCH                              0x03000000
#       define NV_PFIFO_RAMHT_SEARCH_16                           0
#       define NV_PFIFO_RAMHT_SEARCH_32                           1
#       define NV_PFIFO_RAMHT_SEARCH_64                           2
#       define NV_PFIFO_RAMHT_SEARCH_128                          3
#define NV_PFIFO_RAMFC                                   0x00000214
#   define NV_PFIFO_RAMFC_BASE_ADDRESS1                       0x000001FC
#   define NV_PFIFO_RAMFC_SIZE                                0x00010000
#   define NV_PFIFO_RAMFC_BASE_ADDRESS2                       0x00FE0000
#define NV_PFIFO_RAMRO                                   0x00000218
#   define NV_PFIFO_RAMRO_BASE_ADDRESS                        0x000001FE
#   define NV_PFIFO_RAMRO_SIZE                                0x00010000
#define NV_PFIFO_RUNOUT_STATUS                           0x00000400
#   define NV_PFIFO_RUNOUT_STATUS_RANOUT                       (1 << 0)
#   define NV_PFIFO_RUNOUT_STATUS_LOW_MARK                     (1 << 4)
#   define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK                    (1 << 8)
#define NV_PFIFO_MODE                                    0x00000504
#define NV_PFIFO_DMA                                     0x00000508
#define NV_PFIFO_CACHE1_PUSH0                            0x00001200
#   define NV_PFIFO_CACHE1_PUSH0_ACCESS                         (1 << 0)
#define NV_PFIFO_CACHE1_PUSH1                            0x00001204
#   define NV_PFIFO_CACHE1_PUSH1_CHID                         0x0000001F
#   define NV_PFIFO_CACHE1_PUSH1_MODE                         0x00000100
#define NV_PFIFO_CACHE1_STATUS                           0x00001214
#   define NV_PFIFO_CACHE1_STATUS_LOW_MARK                      (1 << 4)
#   define NV_PFIFO_CACHE1_STATUS_HIGH_MARK                     (1 << 8)
#define NV_PFIFO_CACHE1_DMA_PUSH                         0x00001220
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS                      (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATE                       (1 << 4)
#   define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER                      (1 << 8)
#   define NV_PFIFO_CACHE1_DMA_PUSH_STATUS                     (1 << 12)
#   define NV_PFIFO_CACHE1_DMA_PUSH_ACQUIRE                    (1 << 16)
#define NV_PFIFO_CACHE1_DMA_FETCH                        0x00001224
#   define NV_PFIFO_CACHE1_DMA_FETCH_TRIG                     0x000000F8
#   define NV_PFIFO_CACHE1_DMA_FETCH_SIZE                     0x0000E000
#   define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS                 0x001F0000
#define NV_PFIFO_CACHE1_DMA_STATE                        0x00001228
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE                (1 << 0)
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD                   0x00001FFC
#   define NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL               0x0000E000
#   define NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT             0x1FFC0000
#   define NV_PFIFO_CACHE1_DMA_STATE_ERROR                    0xE0000000
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE               0
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL               1
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NON_CACHE          2
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN             3
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD       4
#       define NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION         6
#define NV_PFIFO_CACHE1_DMA_INSTANCE                     0x0000122C
#   define NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS               0x0000FFFF
#define NV_PFIFO_CACHE1_DMA_PUT                          0x00001240
#define NV_PFIFO_CACHE1_DMA_GET                          0x00001244
#define NV_PFIFO_CACHE1_DMA_SUBROUTINE                   0x0000124C
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET       0x1FFFFFFC
#   define NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE                (1 << 0)
#define NV_PFIFO_CACHE1_PULL0                            0x00001250
#   define NV_PFIFO_CACHE1_PULL0_ACCESS                        (1 << 0)
#define NV_PFIFO_CACHE1_ENGINE                           0x00001280
#define NV_PFIFO_CACHE1_DMA_DCOUNT                       0x000012A0
#   define NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE                   0x00001FFC
#define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW               0x000012A4
#   define NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET          0x1FFFFFFC
#define NV_PFIFO_CACHE1_DMA_RSVD_SHADOW                  0x000012A8
#define NV_PFIFO_CACHE1_DMA_DATA_SHADOW                  0x000012AC


#define NV_PGRAPH_INTR                                   0x00000100
#   define NV_PGRAPH_INTR_NOTIFY                              (1 << 0)
#   define NV_PGRAPH_INTR_MISSING_HW                          (1 << 4)
#   define NV_PGRAPH_INTR_TLB_PRESENT_DMA_R                   (1 << 6)
#   define NV_PGRAPH_INTR_TLB_PRESENT_DMA_W                   (1 << 7)
#   define NV_PGRAPH_INTR_TLB_PRESENT_TEX_A                   (1 << 8)
#   define NV_PGRAPH_INTR_TLB_PRESENT_TEX_B                   (1 << 9)
#   define NV_PGRAPH_INTR_TLB_PRESENT_VTX                    (1 << 10)
#   define NV_PGRAPH_INTR_CONTEXT_SWITCH                     (1 << 12)
#   define NV_PGRAPH_INTR_STATE3D                            (1 << 13)
#   define NV_PGRAPH_INTR_BUFFER_NOTIFY                      (1 << 16)
#   define NV_PGRAPH_INTR_ERROR                              (1 << 20)
#   define NV_PGRAPH_INTR_SINGLE_STEP                        (1 << 24)
#define NV_PGRAPH_NSOURCE                                0x00000108
#   define NV_PGRAPH_NSOURCE_NOTIFICATION                     (1 << 0) 
#define NV_PGRAPH_INTR_EN                                0x00000140
#   define NV_PGRAPH_INTR_EN_NOTIFY                           (1 << 0)
#   define NV_PGRAPH_INTR_EN_MISSING_HW                       (1 << 4)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_DMA_R                (1 << 6)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_DMA_W                (1 << 7)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_TEX_A                (1 << 8)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_TEX_B                (1 << 9)
#   define NV_PGRAPH_INTR_EN_TLB_PRESENT_VTX                 (1 << 10)
#   define NV_PGRAPH_INTR_EN_CONTEXT_SWITCH                  (1 << 12)
#   define NV_PGRAPH_INTR_EN_STATE3D                         (1 << 13)
#   define NV_PGRAPH_INTR_EN_BUFFER_NOTIFY                   (1 << 16)
#   define NV_PGRAPH_INTR_EN_ERROR                           (1 << 20)
#   define NV_PGRAPH_INTR_EN_SINGLE_STEP                     (1 << 24)
#define NV_PGRAPH_CTX_CONTROL                            0x00000144
#   define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME                 0x00000003
#   define NV_PGRAPH_CTX_CONTROL_TIME                           (1 << 8)
#   define NV_PGRAPH_CTX_CONTROL_CHID                          (1 << 16)
#   define NV_PGRAPH_CTX_CONTROL_CHANGE                        (1 << 20)
#   define NV_PGRAPH_CTX_CONTROL_SWITCHING                     (1 << 24)
#   define NV_PGRAPH_CTX_CONTROL_DEVICE                        (1 << 28)
#define NV_PGRAPH_CTX_USER                               0x00000148
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D                        (1 << 0)
#   define NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID                  (1 << 4)
#   define NV_PGRAPH_CTX_USER_SUBCH                           0x0000E000
#   define NV_PGRAPH_CTX_USER_CHID                            0x1F000000
#   define NV_PGRAPH_CTX_USER_SINGLE_STEP                      (1 << 31)
#define NV_PGRAPH_CTX_SWITCH1                            0x0000014C
#   define NV_PGRAPH_CTX_SWITCH1_GRCLASS                      0x000000FF
#   define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY                    (1 << 12)
#   define NV_PGRAPH_CTX_SWITCH1_SWIZZLE                       (1 << 14)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG                 0x00038000
#   define NV_PGRAPH_CTX_SWITCH1_SYNCHRONIZE                   (1 << 18)
#   define NV_PGRAPH_CTX_SWITCH1_ENDIAN_MODE                   (1 << 19)
#   define NV_PGRAPH_CTX_SWITCH1_CLASS_TYPE                    (1 << 22)
#   define NV_PGRAPH_CTX_SWITCH1_SINGLE_STEP                   (1 << 23)
#   define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS                  (1 << 24)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE0              (1 << 25)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE1              (1 << 26)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_PATTERN               (1 << 27)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_ROP                   (1 << 28)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA1                 (1 << 29)
#   define NV_PGRAPH_CTX_SWITCH1_CONTEXT_BETA4                 (1 << 30)
#   define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET                (1 << 31)
#define NV_PGRAPH_TRAPPED_ADDR                           0x00000704
#   define NV_PGRAPH_TRAPPED_ADDR_MTHD                        0x00001FFF
#   define NV_PGRAPH_TRAPPED_ADDR_SUBCH                       0x00070000
#   define NV_PGRAPH_TRAPPED_ADDR_CHID                        0x01F00000
#   define NV_PGRAPH_TRAPPED_ADDR_DHV                         0x10000000
#define NV_PGRAPH_TRAPPED_DATA_LOW                       0x00000708
#define NV_PGRAPH_INCREMENT                              0x0000071C
#   define NV_PGRAPH_INCREMENT_READ_BLIT                        (1 << 0)
#   define NV_PGRAPH_INCREMENT_READ_3D                          (1 << 1)
#define NV_PGRAPH_FIFO                                   0x00000720
#   define NV_PGRAPH_FIFO_ACCESS                                (1 << 0)
#define NV_PGRAPH_CHANNEL_CTX_TABLE                      0x00000780
#   define NV_PGRAPH_CHANNEL_CTX_TABLE_INST                   0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_POINTER                    0x00000784
#   define NV_PGRAPH_CHANNEL_CTX_POINTER_INST                 0x0000FFFF
#define NV_PGRAPH_CHANNEL_CTX_TRIGGER                    0x00000788
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN                (1 << 0)
#   define NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT              (1 << 1)
#define NV_PGRAPH_CSV0_D                                 0x00000FB4
#   define NV_PGRAPH_CSV0_D_MODE                                0xC0000000
#   define NV_PGRAPH_CSV0_D_RANGE_MODE                          (1 << 18)
#define NV_PGRAPH_CSV0_C                                 0x00000FB8
#define NV_PGRAPH_CSV1_B                                 0x00000FBC
#define NV_PGRAPH_CSV1_A                                 0x00000FC0
#define NV_PGRAPH_CLEARRECTX                             0x00001864
#       define NV_PGRAPH_CLEARRECTX_XMIN                          0x00000FFF
#       define NV_PGRAPH_CLEARRECTX_XMAX                          0x0FFF0000
#define NV_PGRAPH_CLEARRECTY                             0x00001868
#       define NV_PGRAPH_CLEARRECTY_YMIN                          0x00000FFF
#       define NV_PGRAPH_CLEARRECTY_YMAX                          0x0FFF0000
#define NV_PGRAPH_COLORCLEARVALUE                        0x0000186C
#define NV_PGRAPH_COMBINEFACTOR0                         0x00001880
#define NV_PGRAPH_COMBINEFACTOR1                         0x000018A0
#define NV_PGRAPH_COMBINEALPHAI0                         0x000018C0
#define NV_PGRAPH_COMBINEALPHAO0                         0x000018E0
#define NV_PGRAPH_COMBINECOLORI0                         0x00001900
#define NV_PGRAPH_COMBINECOLORO0                         0x00001920
#define NV_PGRAPH_COMBINECTL                             0x00001940
#define NV_PGRAPH_COMBINESPECFOG0                        0x00001944
#define NV_PGRAPH_COMBINESPECFOG1                        0x00001948
#define NV_PGRAPH_CONTROL_1                              0x00001950
#       define NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE            0x00000001
#       define NV_PGRAPH_CONTROL_1_STENCIL_FUNC                   0x000000F0
#       define NV_PGRAPH_CONTROL_1_STENCIL_REF                    0x0000FF00
#       define NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ              0x00FF0000
#       define NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE             0xFF000000

// Used by NV_PGRAPH_CONTROL_1_STENCIL_FUNC
#           define NV_PGRAPH_FUNC_NEVER           0x0
#           define NV_PGRAPH_FUNC_LESS            0x1
#           define NV_PGRAPH_FUNC_EQUAL           0x2
#           define NV_PGRAPH_FUNC_LEQUAL          0x3
#           define NV_PGRAPH_FUNC_GREATER         0x4
#           define NV_PGRAPH_FUNC_NOTEQUAL        0x5
#           define NV_PGRAPH_FUNC_GEQUAL          0x6
#           define NV_PGRAPH_FUNC_ALWAYS          0x7

#define NV_PGRAPH_CONTROL_2                              0x00001954
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL                0x0000000F
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL               0x000000F0
#       define NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS               0x00000F00

// Used by NV_PGRAPH_STENCIL_OP_FAIL, NV_PGRAPH_STENCIL_OP_ZFAIL, NV_PGRAPH_STENCIL_OP_ZPASS
#           define NV_PGRAPH_STENCIL_OP_KEEP      0x1
#           define NV_PGRAPH_STENCIL_OP_ZERO      0x2
#           define NV_PGRAPH_STENCIL_OP_REPLACE   0x3
#           define NV_PGRAPH_STENCIL_OP_INCRSAT   0x4
#           define NV_PGRAPH_STENCIL_OP_DECRSAT   0x5
#           define NV_PGRAPH_STENCIL_OP_INVERT    0x6
#           define NV_PGRAPH_STENCIL_OP_INCR      0x7
#           define NV_PGRAPH_STENCIL_OP_DECR      0x8

#define NV_PGRAPH_SHADERCTL                              0x00001998
#define NV_PGRAPH_SHADERPROG                             0x0000199C
#define NV_PGRAPH_SPECFOGFACTOR0                         0x000019AC
#define NV_PGRAPH_SPECFOGFACTOR1                         0x000019B0
#define NV_PGRAPH_ZSTENCILCLEARVALUE                     0x00001A88
#define NV_PGRAPH_ZCLIPMAX                               0x00001ABC
#define NV_PGRAPH_ZCLIPMIN                               0x00001A90

#define NV_PCRTC_INTR_0                                  0x00000100
#   define NV_PCRTC_INTR_0_VBLANK                               (1 << 0)
#define NV_PCRTC_INTR_EN_0                               0x00000140
#   define NV_PCRTC_INTR_EN_0_VBLANK                            (1 << 0)
#define NV_PCRTC_START                                   0x00000800
#define NV_PCRTC_CONFIG                                  0x00000804


#define NV_PVIDEO_INTR                                   0x00000100
#   define NV_PVIDEO_INTR_BUFFER_0                              (1 << 0)
#   define NV_PVIDEO_INTR_BUFFER_1                              (1 << 4)
#define NV_PVIDEO_INTR_EN                                0x00000140
#   define NV_PVIDEO_INTR_EN_BUFFER_0                           (1 << 0)
#   define NV_PVIDEO_INTR_EN_BUFFER_1                           (1 << 4)
#define NV_PVIDEO_BUFFER                                 0x00000700
#   define NV_PVIDEO_BUFFER_0_USE                               (1 << 0)
#   define NV_PVIDEO_BUFFER_1_USE                               (1 << 4)
#define NV_PVIDEO_STOP                                   0x00000704
#define NV_PVIDEO_BASE                                   0x00000900
#define NV_PVIDEO_LIMIT                                  0x00000908
#define NV_PVIDEO_LUMINANCE                              0x00000910
#define NV_PVIDEO_CHROMINANCE                            0x00000918
#define NV_PVIDEO_OFFSET                                 0x00000920
#define NV_PVIDEO_SIZE_IN                                0x00000928
#   define NV_PVIDEO_SIZE_IN_WIDTH                            0x000007FF
#   define NV_PVIDEO_SIZE_IN_HEIGHT                           0x07FF0000
#define NV_PVIDEO_POINT_IN                               0x00000930
#   define NV_PVIDEO_POINT_IN_S                               0x00007FFF
#   define NV_PVIDEO_POINT_IN_T                               0xFFFE0000
#define NV_PVIDEO_DS_DX                                  0x00000938
#define NV_PVIDEO_DT_DY                                  0x00000940
#define NV_PVIDEO_POINT_OUT                              0x00000948
#   define NV_PVIDEO_POINT_OUT_X                              0x00000FFF
#   define NV_PVIDEO_POINT_OUT_Y                              0x0FFF0000
#define NV_PVIDEO_SIZE_OUT                               0x00000950
#   define NV_PVIDEO_SIZE_OUT_WIDTH                           0x00000FFF
#   define NV_PVIDEO_SIZE_OUT_HEIGHT                          0x0FFF0000
#define NV_PVIDEO_FORMAT                                 0x00000958
#   define NV_PVIDEO_FORMAT_PITCH                             0x00001FFF
#   define NV_PVIDEO_FORMAT_COLOR                             0x00030000
#       define NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8             1
#   define NV_PVIDEO_FORMAT_DISPLAY                            (1 << 20)


#define NV_PTIMER_INTR_0                                 0x00000100
#   define NV_PTIMER_INTR_0_ALARM                               (1 << 0)
#define NV_PTIMER_INTR_EN_0                              0x00000140
#   define NV_PTIMER_INTR_EN_0_ALARM                            (1 << 0)
#define NV_PTIMER_NUMERATOR                              0x00000200
#define NV_PTIMER_DENOMINATOR                            0x00000210
#define NV_PTIMER_TIME_0                                 0x00000400
#define NV_PTIMER_TIME_1                                 0x00000410
#define NV_PTIMER_ALARM_0                                0x00000420


#define NV_PFB_CFG0                                      0x00000200
#   define NV_PFB_CFG0_PART                                   0x00000003
#define NV_PFB_CSTATUS                                   0x0000020C
#define NV_PFB_WBC                                       0x00000410
#   define NV_PFB_WBC_FLUSH                                     (1 << 16)


#define NV_PRAMDAC_NVPLL_COEFF                           0x00000500
#   define NV_PRAMDAC_NVPLL_COEFF_MDIV                        0x000000FF
#   define NV_PRAMDAC_NVPLL_COEFF_NDIV                        0x0000FF00
#   define NV_PRAMDAC_NVPLL_COEFF_PDIV                        0x00070000
#define NV_PRAMDAC_MPLL_COEFF                            0x00000504
#   define NV_PRAMDAC_MPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_MPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_MPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_VPLL_COEFF                            0x00000508
#   define NV_PRAMDAC_VPLL_COEFF_MDIV                         0x000000FF
#   define NV_PRAMDAC_VPLL_COEFF_NDIV                         0x0000FF00
#   define NV_PRAMDAC_VPLL_COEFF_PDIV                         0x00070000
#define NV_PRAMDAC_PLL_TEST_COUNTER                      0x00000514
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NOOFIPCLKS             0x000003FF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VALUE                  0x0000FFFF
#   define NV_PRAMDAC_PLL_TEST_COUNTER_ENABLE                  (1 << 16)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_RESET                   (1 << 20)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_SOURCE                 0x03000000
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK              (1 << 27)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_PDIV_RST                (1 << 28)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK              (1 << 29)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK               (1 << 30)
#   define NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK               (1 << 31)


#define NV_USER_DMA_PUT                                  0x40
#define NV_USER_DMA_GET                                  0x44
#define NV_USER_REF                                      0x48



/* DMA objects */
#define NV_DMA_FROM_MEMORY_CLASS                         0x02
#define NV_DMA_TO_MEMORY_CLASS                           0x03
#define NV_DMA_IN_MEMORY_CLASS                           0x3d

#define NV_DMA_CLASS                                          0x00000FFF
#define NV_DMA_PAGE_TABLE                                      (1 << 12)
#define NV_DMA_PAGE_ENTRY                                      (1 << 13)
#define NV_DMA_FLAGS_ACCESS                                    (1 << 14)
#define NV_DMA_FLAGS_MAPPING_COHERENCY                         (1 << 15)
#define NV_DMA_TARGET                                         0x00030000
#   define NV_DMA_TARGET_NVM                                      0x00000000
#   define NV_DMA_TARGET_NVM_TILED                                0x00010000
#   define NV_DMA_TARGET_PCI                                      0x00020000
#   define NV_DMA_TARGET_AGP                                      0x00030000
#define NV_DMA_ADJUST                                         0xFFF00000

#define NV_DMA_ADDRESS                                        0xFFFFF000


#define NV_RAMHT_HANDLE                                       0xFFFFFFFF
#define NV_RAMHT_INSTANCE                                     0x0000FFFF
#define NV_RAMHT_ENGINE                                       0x00030000
#   define NV_RAMHT_ENGINE_SW                                     0x00000000
#   define NV_RAMHT_ENGINE_GRAPHICS                               0x00010000
#   define NV_RAMHT_ENGINE_DVD                                    0x00020000
#define NV_RAMHT_CHID                                         0x1F000000
#define NV_RAMHT_STATUS                                       0x80000000




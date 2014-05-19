/*
 * QEMU Geforce NV2A GPU implementation
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

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "ui/console.h"
#include "hw/pci/pci.h"
#include "ui/console.h"
#include "hw/display/vga.h"
#include "hw/display/vga_int.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qapi/qmp/qstring.h"
#include "gl/gloffscreen.h"

#include "hw/xbox/u_format_r11g11b10f.h"
#include "hw/xbox/nv2a_gpu_vsh.h"
#include "hw/xbox/nv2a_gpu_psh.h"

#include "hw/xbox/nv2a_gpu.h"

#define DEBUG_NV2A_GPU
#ifdef DEBUG_NV2A_GPU
# define NV2A_GPU_DPRINTF(format, ...)       printf("nv2a: " format, ## __VA_ARGS__)
#else
# define NV2A_GPU_DPRINTF(format, ...)       do { } while (0)
#endif


#define NV_NUM_BLOCKS 21
#define NV_PMC          0   /* card master control */
#define NV_PBUS         1   /* bus control */
#define NV_PFIFO        2   /* MMIO and DMA FIFO submission to PGRAPH and VPE */
#define NV_PFIFO_CACHE  3
#define NV_PRMA         4   /* access to BAR0/BAR1 from real mode */
#define NV_PVIDEO       5   /* video overlay */
#define NV_PTIMER       6   /* time measurement and time-based alarms */
#define NV_PCOUNTER     7   /* performance monitoring counters */
#define NV_PVPE         8   /* MPEG2 decoding engine */
#define NV_PTV          9   /* TV encoder */
#define NV_PRMFB        10  /* aliases VGA memory window */
#define NV_PRMVIO       11  /* aliases VGA sequencer and graphics controller registers */
#define NV_PFB          12  /* memory interface */
#define NV_PSTRAPS      13  /* straps readout / override */
#define NV_PGRAPH       14  /* accelerated 2d/3d drawing engine */
#define NV_PCRTC        15  /* more CRTC controls */
#define NV_PRMCIO       16  /* aliases VGA CRTC and attribute controller registers */
#define NV_PRAMDAC      17  /* RAMDAC, cursor, and PLL control */
#define NV_PRMDIO       18  /* aliases VGA palette registers */
#define NV_PRAMIN       19  /* RAMIN access */
#define NV_USER         20  /* PFIFO MMIO and DMA submission area */



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
#       define NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE             0xFF000000
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



/* graphic classes and methods */
#define NV_SET_OBJECT                                        0x00000000


#define NV_CONTEXT_SURFACES_2D                           0x0062
#   define NV062_SET_CONTEXT_DMA_IMAGE_SOURCE                 0x00620184
#   define NV062_SET_CONTEXT_DMA_IMAGE_DESTIN                 0x00620188
#   define NV062_SET_COLOR_FORMAT                             0x00620300
#       define NV062_SET_COLOR_FORMAT_LE_Y8                    0x01
#       define NV062_SET_COLOR_FORMAT_LE_A8R8G8B8              0x0A
#   define NV062_SET_PITCH                                    0x00620304
#   define NV062_SET_OFFSET_SOURCE                            0x00620308
#   define NV062_SET_OFFSET_DESTIN                            0x0062030C

#define NV_IMAGE_BLIT                                    0x009F
#   define NV09F_SET_CONTEXT_SURFACES                         0x009F019C
#   define NV09F_SET_OPERATION                                0x009F02FC
#       define NV09F_SET_OPERATION_SRCCOPY                        3
#   define NV09F_CONTROL_POINT_IN                             0x009F0300
#   define NV09F_CONTROL_POINT_OUT                            0x009F0304
#   define NV09F_SIZE                                         0x009F0308


#define NV_KELVIN_PRIMITIVE                              0x0097
#   define NV097_NO_OPERATION                                 0x00970100
#   define NV097_WAIT_FOR_IDLE                                0x00970110
#   define NV097_FLIP_STALL                                   0x00970130
#   define NV097_SET_CONTEXT_DMA_NOTIFIES                     0x00970180
#   define NV097_SET_CONTEXT_DMA_A                            0x00970184
#   define NV097_SET_CONTEXT_DMA_B                            0x00970188
#   define NV097_SET_CONTEXT_DMA_STATE                        0x00970190
#   define NV097_SET_CONTEXT_DMA_COLOR                        0x00970194
#   define NV097_SET_CONTEXT_DMA_ZETA                         0x00970198
#   define NV097_SET_CONTEXT_DMA_VERTEX_A                     0x0097019C
#   define NV097_SET_CONTEXT_DMA_VERTEX_B                     0x009701A0
#   define NV097_SET_CONTEXT_DMA_SEMAPHORE                    0x009701A4
#   define NV097_SET_SURFACE_CLIP_HORIZONTAL                  0x00970200
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_X                0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH            0xFFFF0000
#   define NV097_SET_SURFACE_CLIP_VERTICAL                    0x00970204
#       define NV097_SET_SURFACE_CLIP_VERTICAL_Y                  0x0000FFFF
#       define NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT             0xFFFF0000
#   define NV097_SET_SURFACE_FORMAT                           0x00970208
#       define NV097_SET_SURFACE_FORMAT_COLOR                     0x0000000F
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5     0x01
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5     0x02
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5                0x03
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8     0x04
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8     0x05
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8 0x06
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8 0x07
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8              0x08
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_B8                    0x09
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8                  0x0A
#       define NV097_SET_SURFACE_FORMAT_ZETA                      0x000000F0
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z16                       0x01
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z24S8                     0x02
#   define NV097_SET_SURFACE_PITCH                            0x0097020C
#       define NV097_SET_SURFACE_PITCH_COLOR                      0x0000FFFF
#       define NV097_SET_SURFACE_PITCH_ZETA                       0xFFFF0000
#   define NV097_SET_SURFACE_COLOR_OFFSET                     0x00970210
#   define NV097_SET_SURFACE_ZETA_OFFSET                      0x00970214
#   define NV097_SET_COMBINER_ALPHA_ICW                       0x00970260
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW0                0x00970288
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW1                0x0097028C
#   define NV097_SET_COLOR_MASK                               0x00970358
#       define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE            0xFF000000
#           define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE_FALSE           0x00
#           define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE_TRUE            0x01
#       define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE              0x00FF0000
#           define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE_FALSE             0x00
#           define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE_TRUE              0x01
#       define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE            0x0000FF00
#           define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE_FALSE           0x00
#           define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE_TRUE            0x01
#       define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE             0x000000FF
#           define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE_FALSE            0x00
#           define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE_TRUE             0x01
#   define NV097_SET_STENCIL_MASK                             0x00970360
#   define NV097_SET_CLIP_MIN                                 0x00970394
#   define NV097_SET_CLIP_MAX                                 0x00970398
#   define NV097_SET_COMPOSITE_MATRIX                         0x00970680
#   define NV097_SET_TEXTURE_MATRIX0                          0x009706c0
#   define NV097_SET_TEXTURE_MATRIX1                          0x00970700
#   define NV097_SET_TEXTURE_MATRIX2                          0x00970740
#   define NV097_SET_TEXTURE_MATRIX3                          0x00970780
#   define NV097_SET_VIEWPORT_OFFSET                          0x00970A20
#   define NV097_SET_COMBINER_FACTOR0                         0x00970A60
#   define NV097_SET_COMBINER_FACTOR1                         0x00970A80
#   define NV097_SET_COMBINER_ALPHA_OCW                       0x00970AA0
#   define NV097_SET_COMBINER_COLOR_ICW                       0x00970AC0
#   define NV097_SET_VIEWPORT_SCALE                           0x00970AF0
#   define NV097_SET_TRANSFORM_PROGRAM                        0x00970B00
#   define NV097_SET_TRANSFORM_CONSTANT                       0x00970B80
#   define NV097_SET_VERTEX4F                                 0x00971518
#   define NV097_SET_VERTEX_DATA_ARRAY_OFFSET                 0x00971720
#   define NV097_SET_VERTEX_DATA_ARRAY_FORMAT                 0x00971760
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE            0x0000000F
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D     0
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1         1
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F          2
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL     3
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K       5
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP        6
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE            0x000000F0
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE          0xFFFFFF00
#   define NV097_SET_BEGIN_END                                0x009717fC
#       define NV097_SET_BEGIN_END_OP_END                         0x00
#       define NV097_SET_BEGIN_END_OP_POINTS                      0x01
#       define NV097_SET_BEGIN_END_OP_LINES                       0x02
#       define NV097_SET_BEGIN_END_OP_LINE_LOOP                   0x03
#       define NV097_SET_BEGIN_END_OP_LINE_STRIP                  0x04
#       define NV097_SET_BEGIN_END_OP_TRIANGLES                   0x05
#       define NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP              0x06
#       define NV097_SET_BEGIN_END_OP_TRIANGLE_FAN                0x07
#       define NV097_SET_BEGIN_END_OP_QUADS                       0x08
#       define NV097_SET_BEGIN_END_OP_QUAD_STRIP                  0x09
#       define NV097_SET_BEGIN_END_OP_POLYGON                     0x0A
#   define NV097_ARRAY_ELEMENT16                              0x00971800
#   define NV097_ARRAY_ELEMENT32                              0x00971808
#   define NV097_DRAW_ARRAYS                                  0x00971810
#       define NV097_DRAW_ARRAYS_COUNT                            0xFF000000
#       define NV097_DRAW_ARRAYS_START_INDEX                      0x00FFFFFF
#   define NV097_INLINE_ARRAY                                 0x00971818
#   define NV097_SET_VERTEX_DATA4UB                           0x00971940
#   define NV097_SET_TEXTURE_OFFSET                           0x00971B00
#   define NV097_SET_TEXTURE_FORMAT                           0x00971B04
#       define NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA               0x00000003
#       define NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY            0x000000F0
#       define NV097_SET_TEXTURE_FORMAT_COLOR                     0x0000FF00
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5       0x02
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5       0x03
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4       0x04
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5         0x05
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8       0x06
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8       0x07
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8    0x0B
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5   0x0C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8  0x0E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8  0x0F
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5   0x11
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8 0x12
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8             0x19
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 0x1E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 0x24
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED 0x30
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8       0x3A
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8 0x3F
#       define NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS             0x000F0000
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U               0x00F00000
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V               0x0F000000
#       define NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P               0xF0000000
#   define NV097_SET_TEXTURE_ADDRESS                          0x00971B08
#       define NV097_SET_TEXTURE_ADDRESS_U                        0x0000000F
#       define NV097_SET_TEXTURE_ADDRESS_CYLWRAP_U                0x000000F0 /* Bool */
#       define NV097_SET_TEXTURE_ADDRESS_V                        0x00000F00 /* Bool */
#       define NV097_SET_TEXTURE_ADDRESS_CYLWRAP_V                0x0000F000
#       define NV097_SET_TEXTURE_ADDRESS_P                        0x000F0000
#       define NV097_SET_TEXTURE_ADDRESS_CYLWRAP_P                0x00F00000 /* Bool */
#       define NV097_SET_TEXTURE_ADDRESS_CYLWRAP_Q                0xFF000000 /* Bool */
            /* Used in NV097_SET_TEXTURE_ADDRESS_{ U, V, P } */
#           define NV097_SET_TEXTURE_ADDRESS_WRAP_WRAP              0x1
#           define NV097_SET_TEXTURE_ADDRESS_WRAP_MIRROR            0x2
#           define NV097_SET_TEXTURE_ADDRESS_WRAP_CLAMP_TO_EDGE     0x3
#           define NV097_SET_TEXTURE_ADDRESS_WRAP_BORDER            0x4
#           define NV097_SET_TEXTURE_ADDRESS_WRAP_CLAMP_OGL         0x5
#   define NV097_SET_TEXTURE_CONTROL0                         0x00971B0C
#       define NV097_SET_TEXTURE_CONTROL0_ENABLE                 (1 << 30)
#       define NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP           0x3FFC0000
#       define NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP           0x0003FFC0
#   define NV097_SET_TEXTURE_CONTROL1                         0x00971B10
#       define NV097_SET_TEXTURE_CONTROL1_IMAGE_PITCH             0xFFFF0000
#   define NV097_SET_TEXTURE_FILTER                           0x00971B14
#       define NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS           0x00001FFF
#       define NV097_SET_TEXTURE_FILTER_MIN                       0x00FF0000
#       define NV097_SET_TEXTURE_FILTER_MAG                       0x0F000000
#   define NV097_SET_TEXTURE_IMAGE_RECT                       0x00971B1C
#       define NV097_SET_TEXTURE_IMAGE_RECT_WIDTH                 0xFFFF0000
#       define NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT                0x0000FFFF
#   define NV097_SET_SEMAPHORE_OFFSET                         0x00971D6C
#   define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE             0x00971D70
#   define NV097_SET_ZSTENCIL_CLEAR_VALUE                     0x00971D8C
#   define NV097_SET_COLOR_CLEAR_VALUE                        0x00971D90
#   define NV097_CLEAR_SURFACE                                0x00971D94
#       define NV097_CLEAR_SURFACE_ZETA                           0x00000003
#       define NV097_CLEAR_SURFACE_Z                              (1 << 0)
#       define NV097_CLEAR_SURFACE_STENCIL                        (1 << 1)
#       define NV097_CLEAR_SURFACE_COLOR                          0x000000F0
#       define NV097_CLEAR_SURFACE_R                              (1 << 4)
#       define NV097_CLEAR_SURFACE_G                              (1 << 5)
#       define NV097_CLEAR_SURFACE_B                              (1 << 6)
#       define NV097_CLEAR_SURFACE_A                              (1 << 7)
#   define NV097_SET_CLEAR_RECT_HORIZONTAL                    0x00971D98
#   define NV097_SET_CLEAR_RECT_VERTICAL                      0x00971D9C
#   define NV097_SET_SPECULAR_FOG_FACTOR                      0x00971E20
#   define NV097_SET_COMBINER_COLOR_OCW                       0x00971E40
#   define NV097_SET_COMBINER_CONTROL                         0x00971E60
#   define NV097_SET_SHADER_STAGE_PROGRAM                     0x00971E70
#   define NV097_SET_SHADER_OTHER_STAGE_INPUT                 0x00971E78
#   define NV097_SET_TRANSFORM_EXECUTION_MODE                 0x00971E94
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_MODE            0x00000003
#       define NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE      0xFFFFFFFC
#   define NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN           0x00971E98
#   define NV097_SET_TRANSFORM_PROGRAM_LOAD                   0x00971E9C
#   define NV097_SET_TRANSFORM_PROGRAM_START                  0x00971EA0
#   define NV097_SET_TRANSFORM_CONSTANT_LOAD                  0x00971EA4

#   define NV097_SET_SHADE_MODE                               0x0097037c
#       define NV097_SET_SHADE_MODE_FLAT                          0x00001D00
#       define NV097_SET_SHADE_MODE_SMOOTH                        0x00001D01

#   define NV097_SET_DEPTH_MASK                               0x0097035c
#       define NV097_SET_DEPTH_MASK_FALSE                         0x00000000
#       define NV097_SET_DEPTH_MASK_TRUE                          0x00000001

#   define NV097_SET_ALPHA_TEST_ENABLE                        0x00970300
#       define NV097_SET_ALPHA_TEST_ENABLE_FALSE                  0x00000000
#       define NV097_SET_ALPHA_TEST_ENABLE_TRUE                   0x00000001
#   define NV097_SET_BLEND_ENABLE                             0x00970304
#       define NV097_SET_BLEND_ENABLE_FALSE                       0x00000000
#       define NV097_SET_BLEND_ENABLE_TRUE                        0x00000001
#   define NV097_SET_CULL_FACE_ENABLE                         0x00970308
#       define NV097_SET_CULL_FACE_ENABLE_FALSE                   0x00000000
#       define NV097_SET_CULL_FACE_ENABLE_TRUE                    0x00000001
#   define NV097_SET_DEPTH_TEST_ENABLE                        0x0097030c
#       define NV097_SET_DEPTH_TEST_ENABLE_FALSE                  0x00000000
#       define NV097_SET_DEPTH_TEST_ENABLE_TRUE                   0x00000001

#   define NV097_SET_ALPHA_REF                               0x00970340

// These are comparision functions and use map_gl_compare_func
#   define NV097_SET_DEPTH_FUNC                              0x00970354
#   define NV097_SET_ALPHA_FUNC                              0x0097033c

#   define NV097_SET_BLEND_FUNC_SFACTOR                      0x00970344
#       define NV097_SET_BLEND_FUNC_SFACTOR_ZERO                  0x00000000
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE                   0x00000001
#       define NV097_SET_BLEND_FUNC_SFACTOR_SRC_COLOR             0x00000300
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_SRC_COLOR   0x00000301
#       define NV097_SET_BLEND_FUNC_SFACTOR_SRC_ALPHA             0x00000302
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_SRC_ALPHA   0x00000303
#       define NV097_SET_BLEND_FUNC_SFACTOR_DST_ALPHA             0x00000304
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_DST_ALPHA   0x00000305
#       define NV097_SET_BLEND_FUNC_SFACTOR_DST_COLOR             0x00000306
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_DST_COLOR   0x00000307
#       define NV097_SET_BLEND_FUNC_SFACTOR_SRC_ALPHA_SATURATE    0x00000308
#       define NV097_SET_BLEND_FUNC_SFACTOR_CONSTANT_COLOR        0x00008001
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_CONSTANT_COLOR 0x00008002
#       define NV097_SET_BLEND_FUNC_SFACTOR_CONSTANT_ALPHA        0x00008003
#       define NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_CONSTANT_ALPHA 0x00008004
#   define NV097_SET_BLEND_FUNC_DFACTOR                      0x00970348
#       define NV097_SET_BLEND_FUNC_DFACTOR_ZERO                  0x00000000
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE                   0x00000001
#       define NV097_SET_BLEND_FUNC_DFACTOR_SRC_COLOR             0x00000300
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_SRC_COLOR   0x00000301
#       define NV097_SET_BLEND_FUNC_DFACTOR_SRC_ALPHA             0x00000302
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_SRC_ALPHA   0x00000303
#       define NV097_SET_BLEND_FUNC_DFACTOR_DST_ALPHA             0x00000304
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_DST_ALPHA   0x00000305
#       define NV097_SET_BLEND_FUNC_DFACTOR_DST_COLOR             0x00000306
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_DST_COLOR   0x00000307
#       define NV097_SET_BLEND_FUNC_DFACTOR_SRC_ALPHA_SATURATE    0x00000308
#       define NV097_SET_BLEND_FUNC_DFACTOR_CONSTANT_COLOR        0x00008001
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_CONSTANT_COLOR 0x00008002
#       define NV097_SET_BLEND_FUNC_DFACTOR_CONSTANT_ALPHA        0x00008003
#       define NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_CONSTANT_ALPHA 0x00008004

#   define NV097_SET_CULL_FACE                               0x0097039c
#       define NV097_SET_CULL_FACE_FRONT                          0x00000404
#       define NV097_SET_CULL_FACE_BACK                           0x00000405
#       define NV097_SET_CULL_FACE_FRONT_AND_BACK                 0x00000408
#   define NV097_SET_FRONT_FACE                              0x009703a0
#       define NV097_SET_FRONT_FACE_CW                            0x00000900
#       define NV097_SET_FRONT_FACE_CCW                           0x00000901

#   define NV097_SET_EDGE_FLAG                               0x009716bc
#       define NV097_SET_EDGE_FLAG_FALSE                          0x00000000
#       define NV097_SET_EDGE_FLAG_TRUE                           0x00000001


static const GLenum kelvin_primitive_map[] = {
    0,
    GL_POINTS,
    GL_LINES,
    GL_LINE_LOOP,
    GL_LINE_STRIP,
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_QUADS,
    GL_QUAD_STRIP,
    GL_POLYGON,
};

static const GLenum kelvin_texture_min_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_LINEAR,
    GL_LINEAR, /* TODO: Convolution filter... */
};

static const GLenum kelvin_texture_mag_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    0,
    GL_LINEAR /* TODO: Convolution filter... */
};

static inline void* convert_a8r8g8b8_to_a8r8g8b8(unsigned int w, unsigned int h, unsigned int pitch, unsigned int levels, const void* in)
{
printf("%i levels\n",levels);
    assert(w*4 < pitch);
    assert((levels == 1) || (pitch == w*4));
    void* out = g_malloc(pitch*h*levels); //FIXME: use proper formula to allocate just enough bytes
    uint8_t* in_ptr = in;
    uint8_t* out_ptr = out;
    unsigned int level;
    for(level = 0; level < levels; level++) {
        size_t size = w*h;
        memcpy(out_ptr,in_ptr,size);
        // Next level..
        out_ptr += size;
        in_ptr += size;
        w /= 2;
        h /= 2;
    }
    return out;
}

static inline void* convert_cr8yb8cb8ya8_to_a8r8g8b8(unsigned int w, unsigned int h, unsigned int pitch, unsigned int levels, const void* in)
{
    //FIXME: Do the actual conversion..
    return convert_a8r8g8b8_to_a8r8g8b8(w,h,pitch,levels,in);
}

typedef struct ColorFormatInfo {
    unsigned int bytes_per_pixel;
    bool linear;
    GLint gl_internal_format;
    GLenum gl_format;
    GLenum gl_type;
    void*(*converter)(unsigned int w, unsigned int h, unsigned int pitch, unsigned int levels, const void*);
} ColorFormatInfo;

static const ColorFormatInfo kelvin_color_format_map[66] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5] =
        {2, false, GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5] =
        {2, false, GL_RGB,  GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4] =
        {2, false, GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5] =
        {2, false, GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] =
        {4, false, GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] =
        {4, false, GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},

    [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8] =
        {4, false, GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, convert_cr8yb8cb8ya8_to_a8r8g8b8},

    /* TODO: 8-bit palettized textures */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8] =
        {1, false, GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE },

    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, GL_RGBA},

    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5] =
        {2, true, GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] =
        {4, true, GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    /* TODO: how do opengl alpha textures work? */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] =
        {2, false, GL_RED,  GL_RED,  GL_UNSIGNED_BYTE},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] =
        {4, true, GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] =
        {2, true, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_SHORT},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8] =
        {4, false, GL_RGBA, GL_ABGR_EXT, GL_UNSIGNED_BYTE },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8] =
        {4, true, GL_RGBA, GL_ABGR_EXT, GL_UNSIGNED_BYTE }
};


#define NV2A_GPU_VERTEX_ATTR_POSITION       0
#define NV2A_GPU_VERTEX_ATTR_WEIGHT         1
#define NV2A_GPU_VERTEX_ATTR_NORMAL         2
#define NV2A_GPU_VERTEX_ATTR_DIFFUSE        3
#define NV2A_GPU_VERTEX_ATTR_SPECULAR       4
#define NV2A_GPU_VERTEX_ATTR_FOG            5
#define NV2A_GPU_VERTEX_ATTR_POINT_SIZE     6
#define NV2A_GPU_VERTEX_ATTR_BACK_DIFFUSE   7
#define NV2A_GPU_VERTEX_ATTR_BACK_SPECULAR  8
#define NV2A_GPU_VERTEX_ATTR_TEXTURE0       9
#define NV2A_GPU_VERTEX_ATTR_TEXTURE1       10
#define NV2A_GPU_VERTEX_ATTR_TEXTURE2       11
#define NV2A_GPU_VERTEX_ATTR_TEXTURE3       12
#define NV2A_GPU_VERTEX_ATTR_RESERVED1      13
#define NV2A_GPU_VERTEX_ATTR_RESERVED2      14
#define NV2A_GPU_VERTEX_ATTR_RESERVED3      15


#define NV2A_GPU_CRYSTAL_FREQ 13500000
#define NV2A_GPU_NUM_CHANNELS 32
#define NV2A_GPU_NUM_SUBCHANNELS 8

#define NV2A_GPU_MAX_BATCH_LENGTH 0xFFFF
#define NV2A_GPU_VERTEXSHADER_SLOTS  32 /*???*/
#define NV2A_GPU_MAX_VERTEXSHADER_LENGTH 136
#define NV2A_GPU_VERTEXSHADER_CONSTANTS 192
#define NV2A_GPU_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_GPU_MAX_TEXTURES 4

#define GET_MASK(v, mask) (((v) & (mask)) >> (ffs(mask)-1))

#define SET_MASK(v, mask, val)                                       \
    do {                                                             \
        (v) &= ~(mask);                                              \
        (v) |= ((val) << (ffs(mask)-1)) & (mask);                    \
    } while (0)

#define CASE_4(v, step)                                              \
    case (v):                                                        \
    case (v)+(step):                                                 \
    case (v)+(step)*2:                                               \
    case (v)+(step)*3


enum FifoMode {
    FIFO_PIO = 0,
    FIFO_DMA = 1,
};

enum FIFOEngine {
    ENGINE_SOFTWARE = 0,
    ENGINE_GRAPHICS = 1,
    ENGINE_DVD = 2,
};



typedef struct RAMHTEntry {
    uint32_t handle;
    hwaddr instance;
    enum FIFOEngine engine;
    unsigned int channel_id : 5;
    bool valid;
} RAMHTEntry;

typedef struct DMAObject {
    unsigned int dma_class;
    unsigned int dma_target;
    hwaddr address;
    hwaddr limit;
} DMAObject;

typedef struct VertexAttribute {
    bool dma_select;
    hwaddr offset;

    /* inline arrays are packed in order?
     * Need to pass the offset to converted attributes */
    unsigned int inline_array_offset;

    uint32_t inline_value;

    unsigned int format;
    unsigned int size; /* size of the data type */
    unsigned int count; /* number of components */
    uint32_t stride;

    bool needs_conversion;
    uint8_t *converted_buffer;
    unsigned int converted_elements;
    unsigned int converted_size;
    unsigned int converted_count;

    GLenum gl_type;
    GLboolean gl_normalize;
} VertexAttribute;

typedef struct VertexShaderConstant {
    bool dirty;
    uint32 data[4];
} VertexShaderConstant;

typedef struct VertexShader {
    bool dirty;
    unsigned int program_length;
    uint32_t program_data[NV2A_GPU_MAX_VERTEXSHADER_LENGTH];

    GLuint gl_program;
} VertexShader;

typedef struct Texture {
    bool dirty;
    bool enabled;

    unsigned int dimensionality;
    unsigned int color_format;
    unsigned int levels;
    unsigned int log_width, log_height;

    unsigned int rect_width, rect_height;

    unsigned int min_mipmap_level, max_mipmap_level;
    unsigned int pitch;

    unsigned int lod_bias;
    unsigned int min_filter, mag_filter;

    /* Texture address settings, FIXME: also available in pgraph regs?! */
    uint8_t wrap_u;
    uint8_t wrap_v;
    uint8_t wrap_p;
    bool cylwrap_u;
    bool cylwrap_v;
    bool cylwrap_p;
    bool cylwrap_q;

    bool dma_select;
    hwaddr offset;

    GLuint gl_texture;
    /* once bound as GL_TEXTURE_RECTANGLE_ARB, it seems textures
     * can't be rebound as GL_TEXTURE_*D... */
    GLuint gl_texture_rect;
} Texture;

typedef struct ShaderState {
    /* fragment shader - register combiner stuff */
    uint32_t combiner_control;
    uint32_t shader_stage_program;
    uint32_t other_stage_input;
    uint32_t final_inputs_0;
    uint32_t final_inputs_1;

    uint32_t rgb_inputs[8], rgb_outputs[8];
    uint32_t alpha_inputs[8], alpha_outputs[8];

    bool rect_tex[4];

    /* vertex shader */
    bool fixed_function;

} ShaderState;

typedef struct Surface {
    bool draw_dirty;
    unsigned int pitch;
    unsigned int format;

    hwaddr offset;
} Surface;

typedef struct InlineVertexBufferEntry {
    uint32_t position[4];
    uint32_t diffuse;
} InlineVertexBufferEntry;

typedef struct KelvinState {
    hwaddr dma_notifies;
    hwaddr dma_state;
    hwaddr dma_vertex_a, dma_vertex_b;
    hwaddr dma_semaphore;
    unsigned int semaphore_offset;

    GLenum gl_primitive_mode;

    bool enable_vertex_program_write;

    unsigned int vertexshader_start_slot;
    unsigned int vertexshader_load_slot;
    VertexShader vertexshaders[NV2A_GPU_VERTEXSHADER_SLOTS];

    unsigned int constant_load_slot;
    VertexShaderConstant constants[NV2A_GPU_VERTEXSHADER_CONSTANTS];

    VertexAttribute vertex_attributes[NV2A_GPU_VERTEXSHADER_ATTRIBUTES];

    unsigned int inline_array_length;
    uint32_t inline_array[NV2A_GPU_MAX_BATCH_LENGTH];

    unsigned int inline_elements_length;
    uint32_t inline_elements[NV2A_GPU_MAX_BATCH_LENGTH];

    unsigned int inline_buffer_length;
    InlineVertexBufferEntry inline_buffer[NV2A_GPU_MAX_BATCH_LENGTH];

    uint32_t shade_mode;

    bool alpha_test_enable;
    bool blend_enable;
    bool cull_face_enable;
    bool depth_test_enable;

    uint32_t depth_func;
    uint32_t alpha_func;
    float alpha_ref;

    uint32_t blend_func_sfactor;
    uint32_t blend_func_dfactor;

    uint32_t cull_face;
    uint32_t front_face;

    bool edge_flag;

} KelvinState;

typedef struct ContextSurfaces2DState {
    hwaddr dma_image_source;
    hwaddr dma_image_dest;
    unsigned int color_format;
    unsigned int source_pitch, dest_pitch;
    hwaddr source_offset, dest_offset;

} ContextSurfaces2DState;

typedef struct ImageBlitState {
    hwaddr context_surfaces;
    unsigned int operation;
    unsigned int in_x, in_y;
    unsigned int out_x, out_y;
    unsigned int width, height;

} ImageBlitState;

typedef struct GraphicsObject {
    uint8_t graphics_class;
    union {
        ContextSurfaces2DState context_surfaces_2d;
        
        ImageBlitState image_blit;

        KelvinState kelvin;
    } data;
} GraphicsObject;

typedef struct GraphicsSubchannel {
    hwaddr object_instance;
    GraphicsObject object;
    uint32_t object_cache[5];
} GraphicsSubchannel;

typedef struct GraphicsContext {
    bool channel_3d;
    unsigned int subchannel;
} GraphicsContext;


typedef struct PGRAPHState {
    QemuMutex lock;

    uint32_t pending_interrupts;
    uint32_t enabled_interrupts;
    QemuCond interrupt_cond;

    hwaddr context_table;
    hwaddr context_address;


    unsigned int trapped_method;
    unsigned int trapped_subchannel;
    unsigned int trapped_channel_id;
    uint32_t trapped_data[2];
    uint32_t notify_source;

    bool fifo_access;
    QemuCond fifo_access_cond;

    QemuSemaphore read_3d;

    unsigned int channel_id;
    bool channel_valid;
    GraphicsContext context[NV2A_GPU_NUM_CHANNELS];


    hwaddr dma_color, dma_zeta;
    Surface surface_color, surface_zeta;
    unsigned int surface_x, surface_y;
    unsigned int surface_width, surface_height;
    uint32_t color_mask;
    bool depth_mask;

    hwaddr dma_a, dma_b;
    Texture textures[NV2A_GPU_MAX_TEXTURES];

    bool shaders_dirty;
    GHashTable *shader_cache;
    GLuint gl_program;

    float composite_matrix[16];

    float texture_matrix0[16];
    float texture_matrix1[16];
    float texture_matrix2[16];
    float texture_matrix3[16];

    GloContext *gl_context;
    GLuint gl_framebuffer;
    GLuint gl_renderbuffer;
    GLuint gl_depth_renderbuffer;
    GraphicsSubchannel subchannel_data[NV2A_GPU_NUM_SUBCHANNELS];


    uint32_t regs[0x2000];
} PGRAPHState;


typedef struct CacheEntry {
    QSIMPLEQ_ENTRY(CacheEntry) entry;

    unsigned int method : 14;
    unsigned int subchannel : 3;
    bool nonincreasing;
    uint32_t parameter;
} CacheEntry;

typedef struct Cache1State {
    unsigned int channel_id;
    enum FifoMode mode;

    /* Pusher state */
    bool push_enabled;
    bool dma_push_enabled;
    bool dma_push_suspended;
    hwaddr dma_instance;

    bool method_nonincreasing;
    unsigned int method : 14;
    unsigned int subchannel : 3;
    unsigned int method_count : 24;
    uint32_t dcount;
    bool subroutine_active;
    hwaddr subroutine_return;
    hwaddr get_jmp_shadow;
    uint32_t rsvd_shadow;
    uint32_t data_shadow;
    uint32_t error;


    /* Puller state */
    QemuMutex pull_lock;

    bool pull_enabled;
    enum FIFOEngine bound_engines[NV2A_GPU_NUM_SUBCHANNELS];
    enum FIFOEngine last_engine;

    /* The actual command queue */
    QemuMutex cache_lock;
    QemuCond cache_cond;
    int cache_size;
    QSIMPLEQ_HEAD(, CacheEntry) cache;
} Cache1State;

typedef struct ChannelControl {
    hwaddr dma_put;
    hwaddr dma_get;
    uint32_t ref;
} ChannelControl;



typedef struct NV2A_GPUState {
    PCIDevice dev;

    VGACommonState vga;
    GraphicHwOps hw_ops;

    MemoryRegion *vram;
    MemoryRegion vram_pci;
    uint8_t *vram_ptr;
    MemoryRegion ramin;
    uint8_t *ramin_ptr;

    MemoryRegion mmio;

    MemoryRegion block_mmio[NV_NUM_BLOCKS];

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;
    } pmc;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr ramht_address;
        unsigned int ramht_size;
        uint32_t ramht_search;

        hwaddr ramfc_address1;
        hwaddr ramfc_address2;
        unsigned int ramfc_size;

        QemuThread puller_thread;

        /* Weather the fifo chanels are PIO or DMA */
        uint32_t channel_modes;

        uint32_t channels_pending_push;

        Cache1State cache1;
    } pfifo;

    struct {
        uint32_t regs[0x1000];
    } pvideo;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        uint32_t numerator;
        uint32_t denominator;

        uint32_t alarm_time;
    } ptimer;

    struct {
        uint32_t regs[0x1000];
    } pfb;

    struct PGRAPHState pgraph;

    struct {
        uint32_t pending_interrupts;
        uint32_t enabled_interrupts;

        hwaddr start;
    } pcrtc;

    struct {
        uint32_t core_clock_coeff;
        uint64_t core_clock_freq;
        uint32_t memory_clock_coeff;
        uint32_t video_clock_coeff;
    } pramdac;

    struct {
        ChannelControl channel_control[NV2A_GPU_NUM_CHANNELS];
    } user;

} NV2A_GPUState;


#define NV2A_GPU_DEVICE(obj) \
    OBJECT_CHECK(NV2A_GPUState, (obj), "nv2a")

/* new style (work in function) so we can easily restore the state anytime */

void update_gl_color_mask(PGRAPHState* pg) {
    uint8_t mask_alpha = GET_MASK(pg->color_mask, NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE);
    uint8_t mask_red = GET_MASK(pg->color_mask, NV097_SET_COLOR_MASK_RED_WRITE_ENABLE);
    uint8_t mask_green = GET_MASK(pg->color_mask, NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE);
    uint8_t mask_blue = GET_MASK(pg->color_mask, NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE);
    /* XXX: What happens if mask_* is not 0x00 or 0x01? */
    glColorMask(mask_red==0x00?GL_FALSE:GL_TRUE,
                mask_green==0x00?GL_FALSE:GL_TRUE,
                mask_blue==0x00?GL_FALSE:GL_TRUE,
                mask_alpha==0x00?GL_FALSE:GL_TRUE);
}

void update_gl_stencil_mask(PGRAPHState* pg) {
    GLuint gl_mask = GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                              NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE);
    glStencilMask(gl_mask);
}

void update_gl_depth_mask(PGRAPHState* pg) {
    glDepthMask(pg->depth_mask?GL_TRUE:GL_FALSE);
}

/* old style (work in parameter) */

static inline void set_gl_state(GLenum cap, bool state)
{
    if (state) {
        glEnable(cap);
    } else {
        glDisable(cap);
    }
}

static inline void set_gl_front_face(uint32_t mode) {
    GLenum gl_mode;
    switch(mode) {
        case NV097_SET_FRONT_FACE_CW: gl_mode = GL_CW; break;
        case NV097_SET_FRONT_FACE_CCW: gl_mode = GL_CCW; break;
        default:
            assert(0);
    }
    glFrontFace(gl_mode);
}

static inline void set_gl_cull_face(uint32_t mode) {
    GLenum gl_mode;
    switch(mode) {
        case NV097_SET_CULL_FACE_FRONT: gl_mode = GL_FRONT; break;
        case NV097_SET_CULL_FACE_BACK: gl_mode = GL_BACK; break;
        case NV097_SET_CULL_FACE_FRONT_AND_BACK:
          gl_mode = GL_FRONT_AND_BACK;
          break;
        default:
            assert(0);
    }
    glCullFace(gl_mode);
}

static inline GLenum map_gl_wrap_mode(uint32_t mode) {
    GLenum gl_mode;
    switch(mode) {
    case NV097_SET_TEXTURE_ADDRESS_WRAP_WRAP:
        gl_mode = GL_REPEAT;
        break;
    case NV097_SET_TEXTURE_ADDRESS_WRAP_MIRROR:
        gl_mode = GL_MIRRORED_REPEAT;
        break;
    case NV097_SET_TEXTURE_ADDRESS_WRAP_BORDER:
        gl_mode = GL_CLAMP_TO_BORDER;
        break;
    /* FIXME: What's the difference between these 2? */
    case NV097_SET_TEXTURE_ADDRESS_WRAP_CLAMP_TO_EDGE:
    case NV097_SET_TEXTURE_ADDRESS_WRAP_CLAMP_OGL:
        gl_mode = GL_CLAMP_TO_EDGE;
        break;
    default:
        assert(0);
    }
    return gl_mode;
}

static inline GLenum map_gl_compare_func(uint32_t func)
{
    GLenum gl_func;
    switch(func) {
        case 0x200: gl_func = GL_NEVER;    break;
        case 0x201: gl_func = GL_LESS;     break;
        case 0x202: gl_func = GL_EQUAL;    break;
        case 0x203: gl_func = GL_LEQUAL;   break;
        case 0x204: gl_func = GL_GREATER;  break;
        case 0x205: gl_func = GL_NOTEQUAL; break;
        case 0x206: gl_func = GL_GEQUAL;   break;
        case 0x207: gl_func = GL_ALWAYS;   break;
        default:
            assert(0);
    }
    return gl_func;
}

static inline void set_gl_blend_func(uint32_t sfactor, uint32_t dfactor) {
    GLenum gl_sfactor, gl_dfactor;
    switch(sfactor) {
        case NV097_SET_BLEND_FUNC_SFACTOR_ZERO:
            gl_sfactor = GL_ZERO;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE:
            gl_sfactor = GL_ONE;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_SRC_COLOR:
            gl_sfactor = GL_SRC_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_SRC_COLOR:
            gl_sfactor = GL_ONE_MINUS_SRC_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_SRC_ALPHA:
            gl_sfactor = GL_SRC_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_SRC_ALPHA:
            gl_sfactor = GL_ONE_MINUS_SRC_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_DST_ALPHA:
            gl_sfactor = GL_DST_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_DST_ALPHA:
            gl_sfactor = GL_ONE_MINUS_DST_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_DST_COLOR:
            gl_sfactor = GL_DST_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_DST_COLOR:
            gl_sfactor = GL_ONE_MINUS_DST_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_SRC_ALPHA_SATURATE:
            gl_sfactor = GL_SRC_ALPHA_SATURATE;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_CONSTANT_COLOR:
            gl_sfactor = GL_CONSTANT;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_CONSTANT_COLOR:
            gl_sfactor = GL_ONE_MINUS_CONSTANT_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_CONSTANT_ALPHA:
            gl_sfactor = GL_CONSTANT_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_SFACTOR_ONE_MINUS_CONSTANT_ALPHA:
            gl_sfactor = GL_ONE_MINUS_CONSTANT_ALPHA;
            break;
        default:
            assert(0);
    }
    switch(dfactor) {
        case NV097_SET_BLEND_FUNC_DFACTOR_ZERO:
            gl_dfactor = GL_ZERO;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE:
            gl_dfactor = GL_ONE;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_SRC_COLOR:
            gl_dfactor = GL_SRC_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_SRC_COLOR:
            gl_dfactor = GL_ONE_MINUS_SRC_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_SRC_ALPHA:
            gl_dfactor = GL_SRC_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_SRC_ALPHA:
            gl_dfactor = GL_ONE_MINUS_SRC_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_DST_ALPHA:
            gl_dfactor = GL_DST_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_DST_ALPHA:
            gl_dfactor = GL_ONE_MINUS_DST_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_DST_COLOR:
            gl_dfactor = GL_DST_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_DST_COLOR:
            gl_dfactor = GL_ONE_MINUS_DST_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_SRC_ALPHA_SATURATE:
            gl_dfactor = GL_SRC_ALPHA_SATURATE;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_CONSTANT_COLOR:
            gl_dfactor = GL_CONSTANT_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_CONSTANT_COLOR:
            gl_dfactor = GL_ONE_MINUS_CONSTANT_COLOR;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_CONSTANT_ALPHA:
            gl_dfactor = GL_CONSTANT_ALPHA;
            break;
        case NV097_SET_BLEND_FUNC_DFACTOR_ONE_MINUS_CONSTANT_ALPHA:
            gl_dfactor = GL_ONE_MINUS_CONSTANT_ALPHA;
            break;
        default:
            assert(0);
    }
    glBlendFunc(gl_sfactor,gl_dfactor);
}


static void reg_log_read(int block, hwaddr addr, uint64_t val);
static void reg_log_write(int block, hwaddr addr, uint64_t val);
static void pgraph_method_log(unsigned int subchannel,
                              unsigned int graphics_class,
                              unsigned int method, uint32_t parameter);

static void update_irq(NV2A_GPUState *d)
{
    /* PFIFO */
    if (d->pfifo.pending_interrupts & d->pfifo.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PFIFO;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PFIFO;
    }

    /* PCRTC */
    if (d->pcrtc.pending_interrupts & d->pcrtc.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PCRTC;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PCRTC;
    }

    /* PGRAPH */
    if (d->pgraph.pending_interrupts & d->pgraph.enabled_interrupts) {
        d->pmc.pending_interrupts |= NV_PMC_INTR_0_PGRAPH;
    } else {
        d->pmc.pending_interrupts &= ~NV_PMC_INTR_0_PGRAPH;
    }

    if (d->pmc.pending_interrupts && d->pmc.enabled_interrupts) {
        pci_irq_assert(&d->dev);
    } else {
        pci_irq_deassert(&d->dev);
    }
}

static uint32_t ramht_hash(NV2A_GPUState *d, uint32_t handle)
{
    uint32_t hash = 0;
    /* XXX: Think this is different to what nouveau calculates... */
    uint32_t bits = ffs(d->pfifo.ramht_size)-2;

    while (handle) {
        hash ^= (handle & ((1 << bits) - 1));
        handle >>= bits;
    }
    hash ^= d->pfifo.cache1.channel_id << (bits - 4);

    return hash;
}


static RAMHTEntry ramht_lookup(NV2A_GPUState *d, uint32_t handle)
{
    uint32_t hash;
    uint8_t *entry_ptr;
    uint32_t entry_handle;
    uint32_t entry_context;


    hash = ramht_hash(d, handle);
    assert(hash * 8 < d->pfifo.ramht_size);

    entry_ptr = d->ramin_ptr + d->pfifo.ramht_address + hash * 8;

    entry_handle = ldl_le_p(entry_ptr);
    entry_context = ldl_le_p(entry_ptr + 4);

    return (RAMHTEntry){
        .handle = entry_handle,
        .instance = (entry_context & NV_RAMHT_INSTANCE) << 4,
        .engine = (entry_context & NV_RAMHT_ENGINE) >> 16,
        .channel_id = (entry_context & NV_RAMHT_CHID) >> 24,
        .valid = entry_context & NV_RAMHT_STATUS,
    };
}

static DMAObject nv_dma_load(NV2A_GPUState *d, hwaddr dma_obj_address)
{
    assert(dma_obj_address < memory_region_size(&d->ramin));

    uint32_t *dma_obj = (uint32_t*)(d->ramin_ptr + dma_obj_address);
    uint32_t flags = ldl_le_p(dma_obj);
    uint32_t limit = ldl_le_p(dma_obj + 1);
    uint32_t frame = ldl_le_p(dma_obj + 2);

    return (DMAObject){
        .dma_class = GET_MASK(flags, NV_DMA_CLASS),
        .dma_target = GET_MASK(flags, NV_DMA_TARGET),
        .address = (frame & NV_DMA_ADDRESS) | GET_MASK(flags, NV_DMA_ADJUST),
        .limit = limit,
    };
}

static void *nv_dma_map(NV2A_GPUState *d, DMAObject* dma, hwaddr *len)
{
    /* TODO: Handle targets and classes properly */
    //FIXME: only commented out for debug builds! assert(dma.address + dma.limit < memory_region_size(d->vram));
    *len = dma->limit;
    return d->vram_ptr + dma->address;
}

static void *nv_dma_load_and_map(NV2A_GPUState *d, hwaddr dma_obj_address, hwaddr *len)
{
    assert(dma_obj_address < memory_region_size(&d->ramin));
    DMAObject dma = nv_dma_load(d, dma_obj_address);
    return nv_dma_map(d, &dma, len);
}

static void load_graphics_object(NV2A_GPUState *d, hwaddr instance_address,
                                 GraphicsObject *obj)
{
    int i;
    uint8_t *obj_ptr;
    uint32_t switch1, switch2, switch3;

    assert(instance_address < memory_region_size(&d->ramin));

    obj_ptr = d->ramin_ptr + instance_address;

    switch1 = ldl_le_p(obj_ptr);
    switch2 = ldl_le_p(obj_ptr+4);
    switch3 = ldl_le_p(obj_ptr+8);

    obj->graphics_class = switch1 & NV_PGRAPH_CTX_SWITCH1_GRCLASS;

    /* init graphics object */
    KelvinState *kelvin;
    switch (obj->graphics_class) {
    case NV_KELVIN_PRIMITIVE:
        kelvin = &obj->data.kelvin;

        /* generate vertex programs */
        for (i = 0; i < NV2A_GPU_VERTEXSHADER_SLOTS; i++) {
            VertexShader *shader = &kelvin->vertexshaders[i];
            glGenProgramsARB(1, &shader->gl_program);
        }
        assert(glGetError() == GL_NO_ERROR);

        /* temp hack? */
        kelvin->vertex_attributes[NV2A_GPU_VERTEX_ATTR_DIFFUSE].inline_value = 0xFFFFFFF;

        break;
    default:
        break;
    }
}

static GraphicsObject* lookup_graphics_object(PGRAPHState *s,
                                              hwaddr instance_address)
{
    int i;
    for (i=0; i<NV2A_GPU_NUM_SUBCHANNELS; i++) {
        if (s->subchannel_data[i].object_instance == instance_address) {
            return &s->subchannel_data[i].object;
        }
    }
    return NULL;
}


static void kelvin_bind_converted_vertex_attributes(NV2A_GPUState *d,
                                                    KelvinState *kelvin,
                                                    bool inline_data,
                                                    unsigned int num_elements)
{
    int i, j;
    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count && attribute->needs_conversion) {

            uint8_t *data;
            if (inline_data) {
                data = (uint8_t*)kelvin->inline_array
                        + attribute->inline_array_offset;
            } else {
                hwaddr dma_len;
                if (attribute->dma_select) {
                    data = nv_dma_load_and_map(d, kelvin->dma_vertex_b, &dma_len);
                } else {
                    data = nv_dma_load_and_map(d, kelvin->dma_vertex_a, &dma_len);
                }

                assert(attribute->offset < dma_len);
                data += attribute->offset;
            }

            unsigned int stride = attribute->converted_size
                                    * attribute->converted_count;
            
            if (num_elements > attribute->converted_elements) {
                attribute->converted_buffer = realloc(
                    attribute->converted_buffer,
                    num_elements * stride);
            }

            for (j=attribute->converted_elements; j<num_elements; j++) {
                uint8_t *in = data + j * attribute->stride;
                uint8_t *out = attribute->converted_buffer + j * stride;

                switch (attribute->format) {
                case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
                    r11g11b10f_to_float3(ldl_le_p(in),
                                         (float*)out);
                    break;
                default:
                    assert(false);
                }
            }

            attribute->converted_elements = num_elements;

            glVertexAttribPointer(i,
                attribute->converted_count,
                attribute->gl_type,
                attribute->gl_normalize,
                stride,
                data);

        }
    }
}

static unsigned int kelvin_bind_inline_array(KelvinState *kelvin)
{
    int i;
    unsigned int offset = 0;
    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count) {

            glEnableVertexAttribArray(i);

            attribute->inline_array_offset = offset;

            if (!attribute->needs_conversion) {
                glVertexAttribPointer(i,
                    attribute->count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    (uint8_t*)kelvin->inline_array + offset);
            }

            offset += attribute->size * attribute->count;
        }
    }
    return offset;
}

static void kelvin_bind_vertex_attributes(NV2A_GPUState *d,
                                                 KelvinState *kelvin)
{
    int i;

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: kelvin_bind_vertex_attributes");

    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &kelvin->vertex_attributes[i];
        if (attribute->count) {
            glEnableVertexAttribArray(i);

            if (!attribute->needs_conversion) {
                hwaddr dma_len;
                uint8_t *vertex_data;

                /* TODO: cache coherence */
                if (attribute->dma_select) {
                    vertex_data = nv_dma_load_and_map(d, kelvin->dma_vertex_b, &dma_len);
                } else {
                    vertex_data = nv_dma_load_and_map(d, kelvin->dma_vertex_a, &dma_len);
                }
                assert(attribute->offset < dma_len);
                vertex_data += attribute->offset;

                glVertexAttribPointer(i,
                    attribute->count,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    vertex_data);
            }
        } else {
            glDisableVertexAttribArray(i);

            glVertexAttrib4ubv(i, (GLubyte *)&attribute->inline_value);
        }
    }

    glPopDebugGroup();

}

static void kelvin_bind_vertex_program(KelvinState *kelvin)
{
    int i;
    VertexShader *shader;

    shader = &kelvin->vertexshaders[kelvin->vertexshader_start_slot];

    glBindProgramARB(GL_VERTEX_PROGRAM_ARB, shader->gl_program);

    if (shader->dirty) {
        QString *program_code = vsh_translate(VSH_VERSION_XVS,
                                             shader->program_data,
                                             shader->program_length);
        const char* program_code_str = qstring_get_str(program_code);

        NV2A_GPU_DPRINTF("bind vertex program %d, code:\n%s\n",
                     kelvin->vertexshader_start_slot,
                     program_code_str);

        glProgramStringARB(GL_VERTEX_PROGRAM_ARB,
                           GL_PROGRAM_FORMAT_ASCII_ARB,
                           strlen(program_code_str),
                           program_code_str);

        /* Check it compiled */
        GLint pos;
        glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        if (pos != -1) {
            fprintf(stderr, "nv2a: vertex program compilation failed:\n"
                            "      pos %d, %s\n",
                    pos, glGetString(GL_PROGRAM_ERROR_STRING_ARB));
            fprintf(stderr, "ucode:\n");
            for (i=0; i<shader->program_length; i++) {
                fprintf(stderr, "    0x%08x,\n", shader->program_data[i]);
            }
            abort();
        }

        /* Check we're within resource limits */
        GLint native;
        glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,
                          GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,
                          &native);
        assert(native);

        assert(glGetError() == GL_NO_ERROR);

        QDECREF(program_code);
        shader->dirty = false;
    }

    /* load constants */
    for (i=0; i<NV2A_GPU_VERTEXSHADER_CONSTANTS; i++) {
        VertexShaderConstant *constant = &kelvin->constants[i];
        if (!constant->dirty) continue;

        glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB,
                                    i,
                                    (const GLfloat*)constant->data);
        constant->dirty = false;
    }

    assert(glGetError() == GL_NO_ERROR);
}


static void unswizzle_rect(
    uint8_t *src_buf,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    uint8_t *dst_buf,
    unsigned int pitch,
    unsigned int bytes_per_pixel)
{
    unsigned int offset_u = 0, offset_v = 0, offset_w = 0;
    uint32_t mask_u = 0, mask_v = 0, mask_w = 0;

    unsigned int i = 1, j = 1;

    while( (i <= width) || (i <= height) || (i <= depth) ) {
        if(i < width) {
            mask_u |= j;
            j<<=1;
        }
        if(i < height) {
            mask_v |= j;
            j<<=1;
        }
        if(i < depth) {
            mask_w |= j;
            j<<=1;
        }
        i<<=1;
    }

    uint32_t start_u = 0;
    uint32_t start_v = 0;
    uint32_t start_w = 0;
    uint32_t mask_max = 0;

    // get the biggest mask
    if(mask_u > mask_v)
        mask_max = mask_u;
    else
        mask_max = mask_v;
    if(mask_w > mask_max)
        mask_max = mask_w;

    for(i = 1; i <= mask_max; i<<=1) {
        if(i<=mask_u) {
            if(mask_u & i) start_u |= (offset_u & i);
            else offset_u <<= 1;
        }

        if(i <= mask_v) {
            if(mask_v & i) start_v |= (offset_v & i);
            else offset_v<<=1;
        }

        if(i <= mask_w) {
            if(mask_w & i) start_w |= (offset_w & i);
            else offset_w <<= 1;
        }
    }

    uint32_t w = start_w;
    unsigned int z;
    for(z=0; z<depth; z++) {
        uint32_t v = start_v;

        unsigned int y;
        for(y=0; y<height; y++) {
            uint32_t u = start_u;

            unsigned int x;
            for (x=0; x<width; x++) {
                memcpy(dst_buf,
                       src_buf + ( (u|v|w)*bytes_per_pixel ),
                       bytes_per_pixel);
                dst_buf += bytes_per_pixel;

                u = (u - mask_u) & mask_u;
            }
            dst_buf += pitch - width * bytes_per_pixel;

            v = (v - mask_v) & mask_v;
        }
        w = (w - mask_w) & mask_w;
    }
}

static void pgraph_bind_textures(NV2A_GPUState *d)
{
    int i;

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: pgraph_bind_textures");

    for (i=0; i<NV2A_GPU_MAX_TEXTURES; i++) {

        Texture *texture = &d->pgraph.textures[i];

        if (texture->dimensionality != 2) continue;
        
        glActiveTexture(GL_TEXTURE0_ARB + i);
        if (texture->enabled) {
            
            assert(texture->color_format
                    < sizeof(kelvin_color_format_map)/sizeof(ColorFormatInfo));

            ColorFormatInfo f = kelvin_color_format_map[texture->color_format];
            if (f.bytes_per_pixel == 0) {

                char buffer[128];
                sprintf(buffer,"NV2A: unhandled texture->color_format 0x%0x",
                        texture->color_format);
                glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION,
                                     GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, 
                                     0, GL_DEBUG_SEVERITY_MEDIUM, -1, 
                                     buffer);

                printf("%s\n",buffer);
#if 0
                assert(0);
#endif
                continue;
            }

            GLenum gl_target;
            GLuint gl_texture;
            unsigned int width, height;
            if (f.linear) {
                /* linear textures use unnormalised texcoords.
                 * GL_TEXTURE_RECTANGLE_ARB conveniently also does, but
                 * does not allow repeat and mirror wrap modes.
                 *  (or mipmapping, but xbox d3d says 'Non swizzled and non
                 *   compressed textures cannot be mip mapped.')
                 * Not sure if that'll be an issue. */
                gl_target = GL_TEXTURE_RECTANGLE_ARB;
                gl_texture = texture->gl_texture_rect;
                
                width = texture->rect_width;
                height = texture->rect_height;
            } else {
                gl_target = GL_TEXTURE_2D;
                gl_texture = texture->gl_texture;

                width = 1 << texture->log_width;
                height = 1 << texture->log_height;
            }

            /* Label the object so we can find it later in the debugger */
            if (1) {
                static bool initialized = false;
                static void(*imported_glObjectLabelKHR)(GLenum identifier, GLuint name, GLsizei length, const char *label) = NULL;
                if (!initialized) {
                    const GLubyte *extensions = glGetString(GL_EXTENSIONS);
                    if (glo_check_extension((const GLubyte *)"GL_KHR_debug",extensions)) {
                        imported_glObjectLabelKHR = glo_get_extension_proc((const GLubyte *)"glObjectLabel");
                    }
                    initialized = true;
                }
                if (imported_glObjectLabelKHR) {
                    char buffer[256];
                    sprintf(buffer,"NV2A: %s.0x%X+0x%X: { dirty: %i; "
                                   "color_format: 0x%X; "
                                   "pitch: %i }",
                                   texture->dma_select?"B":"A",
                                   texture->dma_select?d->pgraph.dma_b:
                                                       d->pgraph.dma_a,
                                   texture->offset, texture->dirty,
                                   texture->color_format,
                                   texture->pitch);
                    assert(glGetError()==GL_NO_ERROR);
                    imported_glObjectLabelKHR(GL_TEXTURE,gl_texture,-1,buffer);
                    while(glGetError()!=GL_NO_ERROR); //FIXME: This is necessary because GLX does always return a proc currently..
                }
            }

            glBindTexture(gl_target, gl_texture);

            if (!texture->dirty) continue;

            /* load texture data*/

            hwaddr dma_len;
            uint8_t *texture_data;
            if (texture->dma_select) {
                texture_data = nv_dma_load_and_map(d, d->pgraph.dma_b, &dma_len);
            } else {
                texture_data = nv_dma_load_and_map(d, d->pgraph.dma_a, &dma_len);
            }
            assert(texture->offset < dma_len);
            texture_data += texture->offset;

            /* convert texture formats the host can't handle natively */
            uint8_t* converted_texture_data = NULL;
            if (f.converter != NULL) {
                /* FIXME: Unswizzle before? */
                /* FIXME: Handle multiple levels etc. */
                converted_texture_data = f.converter(width,height,
                                                   texture->pitch,
                                                   f.linear?1:texture->levels,
                                                   texture_data);
                texture_data = converted_texture_data;
                assert(texture_data != NULL);
            }

            NV2A_GPU_DPRINTF(" texture %d is format 0x%x, (%d, %d; %d),"
                            " filter %x %x, levels %d-%d %d bias %d\n",
                         i, texture->color_format,
                         width, height, texture->pitch,
                         texture->min_filter, texture->mag_filter,
                         texture->min_mipmap_level, texture->max_mipmap_level, texture->levels,
                         texture->lod_bias);

            if (f.linear) {
                /* Can't handle retarded strides */
                assert(texture->pitch % f.bytes_per_pixel == 0);
                glPixelStorei(GL_UNPACK_ROW_LENGTH,
                              texture->pitch / f.bytes_per_pixel);

                glTexImage2D(gl_target, 0, f.gl_internal_format,
                             width, height, 0,
                             f.gl_format, f.gl_type,
                             texture_data);

                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            } else {
                unsigned int levels = texture->levels;
                if (texture->max_mipmap_level < levels) {
                    levels = texture->max_mipmap_level;
                }

                glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
                    texture->min_mipmap_level);
                glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
                    levels-1);


                int level;
                for (level = 0; level < levels; level++) {
                    if (f.gl_format == 0) { /* retarded way of indicating compressed */
                        unsigned int block_size;
                        if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                            block_size = 8;
                        } else {
                            block_size = 16;
                        }

                        if (width < 4) width = 4;
                        if (height < 4) height = 4;

                        glCompressedTexImage2D(gl_target, level, f.gl_internal_format,
                                               width, height, 0,
                                               width/4 * height/4 * block_size,
                                               texture_data);
                    } else {
                        unsigned int pitch = width * f.bytes_per_pixel;
                        uint8_t *unswizzled = g_malloc(height * pitch);
                        unswizzle_rect(texture_data, width, height, 1,
                                       unswizzled, pitch, f.bytes_per_pixel);

                        glTexImage2D(gl_target, level, f.gl_internal_format,
                                     width, height, 0,
                                     f.gl_format, f.gl_type,
                                     unswizzled);

                        g_free(unswizzled);
                    }

                    texture_data += width * height * f.bytes_per_pixel;
                    width /= 2;
                    height /= 2;
                }

            }

            /* Free the buffer if this texture had to be converted */
            if (converted_texture_data != NULL) {
                g_free(converted_texture_data);
            }

            glTexParameteri(gl_target, GL_TEXTURE_MIN_FILTER,
                kelvin_texture_min_filter_map[texture->min_filter]);
            glTexParameteri(gl_target, GL_TEXTURE_MAG_FILTER,
                kelvin_texture_mag_filter_map[texture->mag_filter]);

            glTexParameteri(gl_target, GL_TEXTURE_WRAP_S,
                map_gl_wrap_mode(texture->wrap_u));
            glTexParameteri(gl_target, GL_TEXTURE_WRAP_T,
                map_gl_wrap_mode(texture->wrap_v));
            /* FIXME: P and Q wrapping unhandled! */

            texture->dirty = false;
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
        }

    }

    glPopDebugGroup();

}

static guint shader_hash(gconstpointer key)
{
    /* 64 bit Fowler/Noll/Vo FNV-1a hash code */
    uint64_t hval = 0xcbf29ce484222325ULL;
    const uint8_t *bp = key;
    const uint8_t *be = key + sizeof(ShaderState);
    while (bp < be) {
        hval ^= (uint64_t) *bp++;
        hval += (hval << 1) + (hval << 4) + (hval << 5) +
            (hval << 7) + (hval << 8) + (hval << 40);
    }

    return (guint)hval;
}

static gboolean shader_equal(gconstpointer a, gconstpointer b)
{
    const ShaderState *as = a, *bs = b;
    return memcmp(as, bs, sizeof(ShaderState)) == 0;
}

static GLuint generate_shaders(ShaderState state)
{
    int i;

    GLuint program = glCreateProgram();

    if (state.fixed_function) {
        /* generate vertex shader mimicking fixed function */
        GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glAttachShader(program, vertex_shader);

        const char *vertex_shader_code =
"attribute vec4 position;\n"
"attribute vec3 normal;\n"
"attribute vec4 diffuse;\n"
"attribute vec4 specular;\n"
"attribute float fogCoord;\n"
"attribute vec4 multiTexCoord0;\n"
"attribute vec4 multiTexCoord1;\n"
"attribute vec4 multiTexCoord2;\n"
"attribute vec4 multiTexCoord3;\n"

"uniform mat4 composite;\n"
"uniform mat4 textureMatrix0;\n"
"uniform mat4 textureMatrix1;\n"
"uniform mat4 textureMatrix2;\n"
"uniform mat4 textureMatrix3;\n"
"uniform mat4 invViewport;\n"
"void main() {\n"
"   gl_Position = invViewport * (position * composite);\n"
/* temp hack: the composite matrix includes the view transform... */
//"   gl_Position = position * composite;\n"
//"   gl_Position.x = (gl_Position.x - 320.0) / 320.0;\n"
//"   gl_Position.y = -(gl_Position.y - 240.0) / 240.0;\n"
"   gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n"
"   gl_FrontColor = diffuse;\n"
"   gl_TexCoord[0] = textureMatrix0*multiTexCoord0;\n"
"   gl_TexCoord[1] = textureMatrix1*multiTexCoord1;\n"
"   gl_TexCoord[2] = textureMatrix2*multiTexCoord2;\n"
"   gl_TexCoord[3] = textureMatrix3*multiTexCoord3;\n"
"}\n";

        glShaderSource(vertex_shader, 1, &vertex_shader_code, 0);
        glCompileShader(vertex_shader);

        NV2A_GPU_DPRINTF("bind new vertex shader, code:\n%s\n", vertex_shader_code);

        /* Check it compiled */
        GLint compiled = 0;
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLchar log[1024];
            glGetShaderInfoLog(vertex_shader, 1024, NULL, log);
            fprintf(stderr, "nv2a: vertex shader compilation failed: %s\n", log);
            abort();
        }

        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_POSITION, "position");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_DIFFUSE, "diffuse");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_SPECULAR, "specular");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_FOG, "fog");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE0, "multiTexCoord0");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE1, "multiTexCoord1");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE2, "multiTexCoord2");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE3, "multiTexCoord3");
    }


    /* generate a fragment hader from register combiners */
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glAttachShader(program, fragment_shader);

    QString *fragment_shader_code = psh_translate(state.combiner_control,
                   state.shader_stage_program,
                   state.other_stage_input,
                   state.rgb_inputs, state.rgb_outputs,
                   state.alpha_inputs, state.alpha_outputs,
                   /* constant_0, constant_1, */
                   state.final_inputs_0, state.final_inputs_1,
                   /* final_constant_0, final_constant_1, */
                   state.rect_tex);

    const char *fragment_shader_code_str = qstring_get_str(fragment_shader_code);

    NV2A_GPU_DPRINTF("bind new fragment shader, code:\n%s\n", fragment_shader_code_str);

    glShaderSource(fragment_shader, 1, &fragment_shader_code_str, 0);
    glCompileShader(fragment_shader);

    /* Check it compiled */
    GLint compiled = 0;
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[1024];
        glGetShaderInfoLog(fragment_shader, 1024, NULL, log);
        fprintf(stderr, "nv2a: fragment shader compilation failed: %s\n", log);
        abort();
    }

    QDECREF(fragment_shader_code);



    /* link the program */
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLchar log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        fprintf(stderr, "nv2a: shader linking failed: %s\n", log);
        abort();
    }

    glUseProgram(program);

    /* set texture samplers */
    for (i = 0; i < NV2A_GPU_MAX_TEXTURES; i++) {
        char samplerName[16];
        snprintf(samplerName, sizeof(samplerName), "texSamp%d", i);
        GLint texSampLoc = glGetUniformLocation(program, samplerName);
        if (texSampLoc >= 0) {
            glUniform1i(texSampLoc, i);
        }
    }

    glValidateProgram(program);
    GLint valid = 0;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
    if (!valid) {
        GLchar log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        fprintf(stderr, "nv2a: shader validation failed: %s\n", log);
        abort();
    }

    return program;
}

static void pgraph_bind_shaders(PGRAPHState *pg)
{
    int i;

    bool fixed_function = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                   NV_PGRAPH_CSV0_D_MODE) == 0;

    if (pg->shaders_dirty) {
        ShaderState state = {
            /* register combier stuff */
            .combiner_control = pg->regs[NV_PGRAPH_COMBINECTL],
            .shader_stage_program = pg->regs[NV_PGRAPH_SHADERPROG],
            .other_stage_input = pg->regs[NV_PGRAPH_SHADERCTL],
            .final_inputs_0 = pg->regs[NV_PGRAPH_COMBINESPECFOG0],
            .final_inputs_1 = pg->regs[NV_PGRAPH_COMBINESPECFOG1],

            /* fixed function stuff */
            .fixed_function = fixed_function,
        };


        for (i = 0; i < 8; i++) {
            state.rgb_inputs[i] = pg->regs[NV_PGRAPH_COMBINECOLORI0 + i * 4];
            state.rgb_outputs[i] = pg->regs[NV_PGRAPH_COMBINECOLORO0 + i * 4];
            state.alpha_inputs[i] = pg->regs[NV_PGRAPH_COMBINEALPHAI0 + i * 4];
            state.alpha_outputs[i] = pg->regs[NV_PGRAPH_COMBINEALPHAO0 + i * 4];
            //constant_0[i] = pg->regs[NV_PGRAPH_COMBINEFACTOR0 + i * 4];
            //constant_1[i] = pg->regs[NV_PGRAPH_COMBINEFACTOR1 + i * 4];
        }

        for (i = 0; i < 4; i++) {
            state.rect_tex[i] = false;
            if (pg->textures[i].enabled
                && kelvin_color_format_map[
                        pg->textures[i].color_format].linear) {
                state.rect_tex[i] = true;
            }
        }

        gpointer cached_shader = g_hash_table_lookup(pg->shader_cache, &state);
        if (cached_shader) {
            pg->gl_program = (GLuint)cached_shader;
        } else {
            pg->gl_program = generate_shaders(state);

            /* cache it */
            ShaderState *cache_state = g_malloc(sizeof(*cache_state));
            memcpy(cache_state, &state, sizeof(*cache_state));
            g_hash_table_insert(pg->shader_cache, cache_state,
                                (gpointer)pg->gl_program);
        }
    }

    glUseProgram(pg->gl_program);


    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: update combiner constants");

    /* update combiner constants */
    for (i = 0; i<= 8; i++) {
        uint32_t constant[2];
        if (i == 8) {
            /* final combiner */
            constant[0] = pg->regs[NV_PGRAPH_SPECFOGFACTOR0];
            constant[1] = pg->regs[NV_PGRAPH_SPECFOGFACTOR1];
        } else {
            constant[0] = pg->regs[NV_PGRAPH_COMBINEFACTOR0 + i * 4];
            constant[1] = pg->regs[NV_PGRAPH_COMBINEFACTOR1 + i * 4];
        }

        int j;
        for (j = 0; j < 2; j++) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "c_%d_%d", i, j);
            GLint loc = glGetUniformLocation(pg->gl_program, tmp);
            if (loc != -1) {
                float value[4];
                value[0] = (float) ((constant[j] >> 16) & 0xFF) / 255.0f;
                value[1] = (float) ((constant[j] >> 8) & 0xFF) / 255.0f;
                value[2] = (float) (constant[j] & 0xFF) / 255.0f;
                value[3] = (float) ((constant[j] >> 24) & 0xFF) / 255.0f;

                glUniform4fv(loc, 1, value);
            }
        }
    }

    glPopDebugGroup();

    /* update fixed function uniforms */
    if (fixed_function) {
        GLint comMatLoc = glGetUniformLocation(pg->gl_program, "composite");
        glUniformMatrix4fv(comMatLoc, 1, GL_FALSE, pg->composite_matrix);

        GLint texMat0Loc = glGetUniformLocation(pg->gl_program, "textureMatrix0");
        glUniformMatrix4fv(texMat0Loc, 1, GL_FALSE, pg->texture_matrix0);
        GLint texMat1Loc = glGetUniformLocation(pg->gl_program, "textureMatrix1");
        glUniformMatrix4fv(texMat1Loc, 1, GL_FALSE, pg->texture_matrix1);
        GLint texMat2Loc = glGetUniformLocation(pg->gl_program, "textureMatrix2");
        glUniformMatrix4fv(texMat2Loc, 1, GL_FALSE, pg->texture_matrix2);
        GLint texMat3Loc = glGetUniformLocation(pg->gl_program, "textureMatrix3");
        glUniformMatrix4fv(texMat3Loc, 1, GL_FALSE, pg->texture_matrix3);

        float zclip_max = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMAX];
        float zclip_min = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMIN];

        /* estimate the viewport by assuming it matches the surface ... */
        float m11 = 0.5 * pg->surface_width;
        float m22 = -0.5 * pg->surface_height;
        float m33 = zclip_max - zclip_min;
        //float m41 = m11;
        //float m42 = -m22;
        float m43 = zclip_min;
        //float m44 = 1.0;


        if (m33 == 0.0) {
            m33 = 1.0;
        }
        float invViewport[16] = {
            1.0/m11, 0, 0, 0,
            0, 1.0/m22, 0, 0,
            0, 0, 1.0/m33, 0,
            -1.0, 1.0, -m43/m33, 1.0
        };

        GLint viewLoc = glGetUniformLocation(pg->gl_program, "invViewport");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &invViewport[0]);

    }

    pg->shaders_dirty = false;
}

static uint8_t* map_surface(NV2A_GPUState *d, Surface* s, DMAObject* dma, hwaddr* len)
{

    /* There's a bunch of bugs that could cause us to hit this function
     * at the wrong time and get a invalid dma object.
     * Check that it's sane. */
    assert(dma->dma_class == NV_DMA_IN_MEMORY_CLASS);

    assert(dma->address + s->offset != 0);
    assert(s->offset <= dma->limit);
    assert(s->offset
            + s->pitch * d->pgraph.surface_height
                <= dma->limit + 1);

    return nv_dma_map(d, dma, len);

}

static void mark_cpu_surface_dirty(NV2A_GPUState *d, Surface* s, DMAObject* dma) {
    memory_region_set_dirty(d->vram, dma->address + s->offset,
                                     s->pitch * d->pgraph.surface_height);
}

static void mark_cpu_surface_clean(NV2A_GPUState *d, Surface* s, DMAObject* dma, unsigned client) {
    memory_region_reset_dirty(d->vram, dma->address + s->offset,
                                       s->pitch * d->pgraph.surface_height,
                                       client);
}

static bool is_cpu_surface_dirty(NV2A_GPUState *d, Surface* s, DMAObject* dma, unsigned client) {
    return memory_region_get_dirty(d->vram, dma->address + s->offset,
                                            s->pitch * d->pgraph.surface_height,
                                            client);
}

static void perform_surface_update(NV2A_GPUState *d, Surface* s, DMAObject* dma, bool upload, uint8_t* data, GLenum gl_format, GLenum gl_type, unsigned int bytes_per_pixel) {

    /* TODO */
    assert(d->pgraph.surface_x == 0 && d->pgraph.surface_y == 0);

    if (upload) {
        /* surface modified (or moved) by the cpu.
         * copy it into the opengl renderbuffer */

        assert(!s->draw_dirty);

        assert(s->pitch % bytes_per_pixel == 0);

        glUseProgram(0);

        int rl, pa;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rl);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &pa);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->pitch / bytes_per_pixel);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        /* glDrawPixels is crazy deprecated, but there really isn't
         * an easy alternative */

        glWindowPos2i(0, d->pgraph.surface_height);
        glPixelZoom(1, -1);
        glDrawPixels(d->pgraph.surface_width,
                     d->pgraph.surface_height,
                     gl_format, gl_type,
                     data + s->offset);
        assert(glGetError() == GL_NO_ERROR);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, rl);
        glPixelStorei(GL_UNPACK_ALIGNMENT, pa);

    } else {

        /* read the opengl renderbuffer into the surface */

        glo_readpixels(gl_format, gl_type,
                       bytes_per_pixel, s->pitch,
                       d->pgraph.surface_width, d->pgraph.surface_height,
                       data + s->offset);
        assert(glGetError() == GL_NO_ERROR);

    }

    uint8_t *out = data + s->offset;
    NV2A_GPU_DPRINTF("Surface %s 0x%llx - 0x%llx, "
                  "(0x%llx - 0x%llx, %d %d, %d %d, %d) - %x %x %x %x\n",
        upload?"upload (CPU->GPU)":"download (GPU->CPU)",
        dma->address, dma->address + dma->limit,
        dma->address + s->offset,
        dma->address + s->pitch * d->pgraph.surface_height,
        d->pgraph.surface_x, d->pgraph.surface_y,
        d->pgraph.surface_width, d->pgraph.surface_height,
        s->pitch,
        out[0], out[1], out[2], out[3]);

}

static void pgraph_update_surface_zeta(NV2A_GPUState *d, bool upload)
{
//FIXME FIXME FIXME: Broken feature because the read/draw-pixel operation does fail atm
#if 0
    /* Early out if we have no surface, otherwise load DMA object */
    Surface* s = &d->pgraph.surface_zeta;
    if (s->format == 0) { return; }
    DMAObject zeta_dma = nv_dma_load(d, d->pgraph.dma_zeta);

    /* Check dirty flags */
    if (upload) {
        /* If the surface was not written by the CPU we don't have to upload */
        if (!is_cpu_surface_dirty(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA)) { return; }
    } else {
        /* If we didn't draw on it we don't have to redownload */
        if (!s->draw_dirty) { return; }
    }

    /* Construct the GL format */
    bool has_stencil;
    GLenum gl_type_stencil;
    GLenum gl_type_depth;
    unsigned int depth_offset;
    unsigned int stencil_offset;
    unsigned int bytes_per_pixel;
    switch (s->format) {
    case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
        /* No Stencil */
        has_stencil = false;
        /* 16 Bit Depth */
        gl_type_depth = GL_UNSIGNED_SHORT;
        depth_offset = 0;
        /* = 16 Bit Total */
        bytes_per_pixel = 2;
    case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
        bytes_per_pixel = 4;
        /* 8 Bit Stencil */
        has_stencil = true;
        gl_type_stencil = GL_UNSIGNED_BYTE;
        stencil_offset = 0;
        /* 24 Bit Depth */
        gl_type_depth = GL_UNSIGNED_INT; /* FIXME: Must be handled using a converter? Actually want 24 bit integer! */
        depth_offset = 0;
        /* FIXME: Use these 2 variables to actually make things right?!
            glGetDoublev(GL_DEPTH_BIAS,  &depth_bias);  // Returns 0.0
             glGetDoublev(GL_DEPTH_SCALE, &depth_scale); // Returns 1.0
        */
        /* = 32 Bit Total */
        bytes_per_pixel = 4;
        break;
    default:
        assert(false);
    }

    /* Map surface into memory */
    hwaddr zeta_len;
    uint8_t *zeta_data = map_surface(d, s, &zeta_dma, &zeta_len);

    /* Allow depth access, then perform the stencil upload or download */
    if (upload) {
        glDepthMask(GL_TRUE);
        //FIXME: Defer flag..
    }
    perform_surface_update(d, s, &zeta_dma, upload, zeta_data+depth_offset, GL_DEPTH_COMPONENT, gl_type_depth, bytes_per_pixel);
    update_gl_depth_mask(&d->pgraph); //FIXME: Defer..

    /* FIXME: Stencil done afterwards so we can possibly overwrite the higher bits of the depth read! Would that work? */
    /* Allow stencil access, then perform the stencil upload or download */
    if (has_stencil) {
        if (upload) {
            glStencilMask(0xFF);
            //FIXME: Defer flag..
        }
        perform_surface_update(d, s, &zeta_dma, upload, zeta_data+stencil_offset, GL_STENCIL_INDEX, gl_type_stencil, bytes_per_pixel);
        update_gl_stencil_mask(&d->pgraph); //FIXME: Defer..
    }

    /* Update dirty flags */
    if (!upload) {
        /* Surface downloaded. Handlers (VGA etc.) need to update */
        mark_cpu_surface_dirty(d, s, &zeta_dma);
        /* We haven't drawn to this again yet, we just downloaded it*/
        s->draw_dirty = false;
    }
    /* Mark it as clean only for us, so changes dirty it again */
    mark_cpu_surface_clean(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA);
#endif
}

static void pgraph_update_surface_color(NV2A_GPUState *d, bool upload)
{

    /* Early out if we have no surface, otherwise load DMA object */
    Surface* s = &d->pgraph.surface_color;
    if (s->format == 0) { return; }
    DMAObject color_dma = nv_dma_load(d, d->pgraph.dma_color);

    /* Check dirty flags */
    if (upload) {
        /* If the surface was not written by the CPU we don't have to upload */
        if (!is_cpu_surface_dirty(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR)) { return; }
    } else {
        /* If we didn't draw on it we don't have to redownload */
        if (!s->draw_dirty) { return; }
    }

    /* Construct the GL format */
    GLenum gl_format;
    GLenum gl_type;
    unsigned int bytes_per_pixel;
    switch (s->format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
        bytes_per_pixel = 2;
        gl_format = GL_BGR;
        gl_type = GL_UNSIGNED_SHORT_5_6_5_REV;
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
        bytes_per_pixel = 4;
        gl_format = GL_BGRA;
        gl_type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    default:
        assert(false);
    }

    /* Map surface into memory */
    hwaddr color_len;
    uint8_t *color_data = map_surface(d, s, &color_dma, &color_len);

    /* Allow depth access, then perform the stencil upload or download */
    if (upload) {
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        //FIXME: Set defer flag
    }
    perform_surface_update(d, s, &color_dma, upload, color_data, gl_format, gl_type, bytes_per_pixel);
    update_gl_color_mask(&d->pgraph); //FIXME: Defer..

    /* Update dirty flags */
    if (!upload) {
        /* Surface downloaded. Handlers (VGA etc.) need to update */
        mark_cpu_surface_dirty(d, s, &color_dma);
        /* We haven't drawn to this again yet, we just downloaded it*/
        s->draw_dirty = false;
    }
    /* Mark it as clean only for us, so changes dirty it again */
    mark_cpu_surface_clean(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR);
}

static inline void pgraph_update_surfaces(NV2A_GPUState *d, bool upload)
{
    pgraph_update_surface_zeta(d, upload);
    pgraph_update_surface_color(d, upload);
}

static void pgraph_init(PGRAPHState *pg)
{
    int i;

    qemu_mutex_init(&pg->lock);
    qemu_cond_init(&pg->interrupt_cond);
    qemu_cond_init(&pg->fifo_access_cond);
    qemu_sem_init(&pg->read_3d, 0);

    /* fire up opengl */

    pg->gl_context = glo_context_create(GLO_FF_DEFAULT);
    assert(pg->gl_context);

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: pgraph_init");

    /* Check context capabilities */
    const GLubyte *extensions = glGetString(GL_EXTENSIONS);

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_texture_compression_s3tc",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_framebuffer_object",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_vertex_program",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_fragment_program",
                             extensions));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_texture_rectangle",
                             extensions));

    GLint max_vertex_attributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attributes);
    assert(max_vertex_attributes >= NV2A_GPU_VERTEXSHADER_ATTRIBUTES);

    glGenFramebuffersEXT(1, &pg->gl_framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, pg->gl_framebuffer);

    glGenRenderbuffersEXT(1, &pg->gl_renderbuffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, pg->gl_renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8,
                             640, 480);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_RENDERBUFFER_EXT,
                                 pg->gl_renderbuffer);

    glGenRenderbuffersEXT(1, &pg->gl_depth_renderbuffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, pg->gl_depth_renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24,
                             640, 480);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                 GL_DEPTH_ATTACHMENT_EXT,
                                 GL_RENDERBUFFER_EXT,
                                 pg->gl_depth_renderbuffer);


    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
            == GL_FRAMEBUFFER_COMPLETE_EXT);

    glViewport(0, 0, 640, 480);
    //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

    pg->shaders_dirty = true;

    /* generate textures */
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: generate textures");
    for (i = 0; i < NV2A_GPU_MAX_TEXTURES; i++) {
        Texture *texture = &pg->textures[i];
        glGenTextures(1, &texture->gl_texture);
        glGenTextures(1, &texture->gl_texture_rect);
    }
    glPopDebugGroup();

    pg->shader_cache = g_hash_table_new(shader_hash, shader_equal);

    assert(glGetError() == GL_NO_ERROR);


    glPopDebugGroup();

    glo_set_current(NULL);
}

static void pgraph_destroy(PGRAPHState *pg)
{
    int i;

    qemu_mutex_destroy(&pg->lock);
    qemu_cond_destroy(&pg->interrupt_cond);
    qemu_cond_destroy(&pg->fifo_access_cond);
    qemu_sem_destroy(&pg->read_3d);

    glo_set_current(pg->gl_context);

    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: pgraph_destroy");

    glDeleteRenderbuffersEXT(1, &pg->gl_renderbuffer);
    glDeleteRenderbuffersEXT(1, &pg->gl_depth_renderbuffer);
    glDeleteFramebuffersEXT(1, &pg->gl_framebuffer);

    for (i = 0; i < NV2A_GPU_MAX_TEXTURES; i++) {
        Texture *texture = &pg->textures[i];
        glDeleteTextures(1, &texture->gl_texture);
        glDeleteTextures(1, &texture->gl_texture_rect);
    }

    glPopDebugGroup();

    glo_set_current(NULL);

    glo_context_destroy(pg->gl_context);
}

static void pgraph_method(NV2A_GPUState *d,
                          unsigned int subchannel,
                          unsigned int method,
                          uint32_t parameter)
{
    int i;
    GraphicsSubchannel *subchannel_data;
    GraphicsObject *object;

    unsigned int slot;
    VertexAttribute *vertex_attribute;
    VertexShader *vertexshader;
    VertexShaderConstant *constant;

    PGRAPHState *pg = &d->pgraph;

    qemu_mutex_lock(&pg->lock);

    assert(pg->channel_valid);
    subchannel_data = &pg->subchannel_data[subchannel];
    object = &subchannel_data->object;

    ContextSurfaces2DState *context_surfaces_2d
        = &object->data.context_surfaces_2d;
    ImageBlitState *image_blit = &object->data.image_blit;
    KelvinState *kelvin = &object->data.kelvin;



    pgraph_method_log(subchannel, object->graphics_class, method, parameter);

    if (method == NV_SET_OBJECT) {
        subchannel_data->object_instance = parameter;

        qemu_mutex_unlock(&pg->lock);
        //qemu_mutex_lock_iothread();
        load_graphics_object(d, parameter, object);
        //qemu_mutex_unlock_iothread();
        return;
    }

    uint32_t class_method = (object->graphics_class << 16) | method;
    switch (class_method) {
    case NV062_SET_CONTEXT_DMA_IMAGE_SOURCE:
        context_surfaces_2d->dma_image_source = parameter;
        break;
    case NV062_SET_CONTEXT_DMA_IMAGE_DESTIN:
        context_surfaces_2d->dma_image_dest = parameter;
        break;
    case NV062_SET_COLOR_FORMAT:
        context_surfaces_2d->color_format = parameter;
        break;
    case NV062_SET_PITCH:
        context_surfaces_2d->source_pitch = parameter & 0xFFFF;
        context_surfaces_2d->dest_pitch = parameter >> 16;
        break;
    case NV062_SET_OFFSET_SOURCE:
        context_surfaces_2d->source_offset = parameter;
        break;
    case NV062_SET_OFFSET_DESTIN:
        context_surfaces_2d->dest_offset = parameter;
        break;

    case NV09F_SET_CONTEXT_SURFACES:
        image_blit->context_surfaces = parameter;
        break;
    case NV09F_SET_OPERATION:
        image_blit->operation = parameter;
        break;
    case NV09F_CONTROL_POINT_IN:
        image_blit->in_x = parameter & 0xFFFF;
        image_blit->in_y = parameter >> 16;
        break;
    case NV09F_CONTROL_POINT_OUT:
        image_blit->out_x = parameter & 0xFFFF;
        image_blit->out_y = parameter >> 16;
        break;
    case NV09F_SIZE:
        image_blit->width = parameter & 0xFFFF;
        image_blit->height = parameter >> 16;

        /* I guess this kicks it off? */
        if (image_blit->operation == NV09F_SET_OPERATION_SRCCOPY) { 
            GraphicsObject *context_surfaces_obj =
                lookup_graphics_object(pg, image_blit->context_surfaces);
            assert(context_surfaces_obj);
            assert(context_surfaces_obj->graphics_class
                == NV_CONTEXT_SURFACES_2D);

            ContextSurfaces2DState *context_surfaces =
                &context_surfaces_obj->data.context_surfaces_2d;

            unsigned int bytes_per_pixel;
            switch (context_surfaces->color_format) {
            case NV062_SET_COLOR_FORMAT_LE_Y8:
                bytes_per_pixel = 1;
                break;
            case NV062_SET_COLOR_FORMAT_LE_A8R8G8B8:
                bytes_per_pixel = 4;
                break;
            default:
                assert(false);
            }

            hwaddr source_dma_len, dest_dma_len;
            uint8_t *source, *dest;

            source = nv_dma_load_and_map(d, context_surfaces->dma_image_source,
                                &source_dma_len);
            assert(context_surfaces->source_offset < source_dma_len);
            source += context_surfaces->source_offset;

            dest = nv_dma_load_and_map(d, context_surfaces->dma_image_dest,
                              &dest_dma_len);
            assert(context_surfaces->dest_offset < dest_dma_len);
            dest += context_surfaces->dest_offset;

            int y;
            for (y=0; y<image_blit->height; y++) {
                uint8_t *source_row = source
                    + (image_blit->in_y + y) * context_surfaces->source_pitch
                    + image_blit->in_x * bytes_per_pixel;
                
                uint8_t *dest_row = dest
                    + (image_blit->out_y + y) * context_surfaces->dest_pitch
                    + image_blit->out_x * bytes_per_pixel;

                memmove(dest_row, source_row,
                        image_blit->width * bytes_per_pixel);
            }

        } else {
            assert(false);
        }

        break;


    case NV097_NO_OPERATION:
        /* The bios uses nop as a software method call -
         * it seems to expect a notify interrupt if the parameter isn't 0.
         * According to a nouveau guy it should still be a nop regardless
         * of the parameter. It's possible a debug register enables this,
         * but nothing obvious sticks out. Weird.
         */
        if (parameter != 0) {
            assert(!(pg->pending_interrupts & NV_PGRAPH_INTR_NOTIFY));

            pg->trapped_channel_id = pg->channel_id;
            pg->trapped_subchannel = subchannel;
            pg->trapped_method = method;
            pg->trapped_data[0] = parameter;
            pg->notify_source = NV_PGRAPH_NSOURCE_NOTIFICATION; /* TODO: check this */
            pg->pending_interrupts |= NV_PGRAPH_INTR_NOTIFY;

            qemu_mutex_unlock(&pg->lock);
            qemu_mutex_lock_iothread();
            update_irq(d);
            qemu_mutex_lock(&pg->lock);
            qemu_mutex_unlock_iothread();

            while (pg->pending_interrupts & NV_PGRAPH_INTR_NOTIFY) {
                qemu_cond_wait(&pg->interrupt_cond, &pg->lock);
            }
        }
        break;
    
    case NV097_WAIT_FOR_IDLE:
        glFinish();
        pgraph_update_surfaces(d, false);
        break;

    case NV097_FLIP_STALL:
        pgraph_update_surfaces(d, false);

        /* Tell the debugger that the frame was completed */
        //FIXME: Figure out if this is a good position, we really have to figure out what a frame is:
        //       - When a pull from the VGA controller happens?
        //       - Result at vblank?
        //       - When D3D completes a frame?
        //       - ...
        static bool initialized = false;
        static void(*imported_glFrameTerminatorGREMEDY)(void) = NULL;
        if (!initialized) {
            const GLubyte *extensions = glGetString(GL_EXTENSIONS);
            if (glo_check_extension((const GLubyte *)"GL_GREMEDY_frame_terminator",extensions)) {
                imported_glFrameTerminatorGREMEDY = glo_get_extension_proc((const GLubyte *)"glFrameTerminatorGREMEDY");
            }
            initialized = true;
        }
        if (imported_glFrameTerminatorGREMEDY) {
            imported_glFrameTerminatorGREMEDY();
        }

        qemu_mutex_unlock(&pg->lock);
        qemu_sem_wait(&pg->read_3d);
        qemu_mutex_lock(&pg->lock);
        break;
    
    case NV097_SET_CONTEXT_DMA_NOTIFIES:
        kelvin->dma_notifies = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_A:
        pg->dma_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_B:
        pg->dma_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_STATE:
        kelvin->dma_state = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_COLOR:
        /* try to get any straggling draws in before the surface's changed :/ */
        pgraph_update_surface_color(d, false);

        pg->dma_color = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_ZETA:
        /* try to get any straggling draws in before the surface's changed :/ */
        pgraph_update_surface_zeta(d, false);

        pg->dma_zeta = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_A:
        kelvin->dma_vertex_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_B:
        kelvin->dma_vertex_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_SEMAPHORE:
        kelvin->dma_semaphore = parameter;
        break;

    case NV097_SET_SURFACE_CLIP_HORIZONTAL:
        pgraph_update_surfaces(d, false);

        pg->surface_x =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_X);
        pg->surface_width =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH);
        break;
    case NV097_SET_SURFACE_CLIP_VERTICAL:
        pgraph_update_surfaces(d, false);

        pg->surface_y =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_Y);
        pg->surface_height =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT);
        break;
    case NV097_SET_SURFACE_FORMAT:
        pgraph_update_surfaces(d, false);

        pg->surface_color.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_COLOR);
        pg->surface_zeta.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ZETA);
        break;
    case NV097_SET_SURFACE_PITCH:
        pgraph_update_surfaces(d, false);

        pg->surface_color.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_COLOR);
        pg->surface_zeta.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_ZETA);
        break;
    case NV097_SET_SURFACE_COLOR_OFFSET:
        pgraph_update_surface_color(d, false);

        pg->surface_color.offset = parameter;
        break;
    case NV097_SET_SURFACE_ZETA_OFFSET:
        pgraph_update_surface_zeta(d, false);

        pg->surface_zeta.offset = parameter;
        break;

    case NV097_SET_COMBINER_ALPHA_ICW ...
            NV097_SET_COMBINER_ALPHA_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAI0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW0:
        pg->regs[NV_PGRAPH_COMBINESPECFOG0] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW1:
        pg->regs[NV_PGRAPH_COMBINESPECFOG1] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COLOR_MASK:
        pg->color_mask = parameter;
        update_gl_color_mask(pg);
        break;
    case NV097_SET_STENCIL_MASK:
        SET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE, parameter);
        update_gl_stencil_mask(pg);
        break;
    case NV097_SET_DEPTH_MASK:
        pg->depth_mask = parameter;
        update_gl_depth_mask(pg);
        break;

    case NV097_SET_CLIP_MIN:
        pg->regs[NV_PGRAPH_ZCLIPMIN] = parameter;
        break;
    case NV097_SET_CLIP_MAX:
        pg->regs[NV_PGRAPH_ZCLIPMAX] = parameter;
        break;

    case NV097_SET_COMPOSITE_MATRIX ...
            NV097_SET_COMPOSITE_MATRIX + 0x3c:
        slot = (class_method - NV097_SET_COMPOSITE_MATRIX) / 4;
        pg->composite_matrix[slot] = *(float*)&parameter;
        break;

    case NV097_SET_TEXTURE_MATRIX0 ...
            NV097_SET_TEXTURE_MATRIX0 + 0x3c:
        slot = (class_method - NV097_SET_TEXTURE_MATRIX0) / 4;
        pg->texture_matrix0[slot] = *(float*)&parameter;
        break;
    case NV097_SET_TEXTURE_MATRIX1 ...
            NV097_SET_TEXTURE_MATRIX1 + 0x3c:
        slot = (class_method - NV097_SET_TEXTURE_MATRIX1) / 4;
        pg->texture_matrix1[slot] = *(float*)&parameter;
        break;
    case NV097_SET_TEXTURE_MATRIX2 ...
            NV097_SET_TEXTURE_MATRIX2 + 0x3c:
        slot = (class_method - NV097_SET_TEXTURE_MATRIX2) / 4;
        pg->texture_matrix2[slot] = *(float*)&parameter;
        break;
    case NV097_SET_TEXTURE_MATRIX3 ...
            NV097_SET_TEXTURE_MATRIX3 + 0x3c:
        slot = (class_method - NV097_SET_TEXTURE_MATRIX3) / 4;
        pg->texture_matrix3[slot] = *(float*)&parameter;
        break;

    case NV097_SET_VIEWPORT_OFFSET ...
            NV097_SET_VIEWPORT_OFFSET + 12:

        slot = (class_method - NV097_SET_VIEWPORT_OFFSET) / 4;

        /* populate magic viewport offset constant */
        kelvin->constants[59].data[slot] = parameter;
        kelvin->constants[59].dirty = true;
        break;

    case NV097_SET_COMBINER_FACTOR0 ...
            NV097_SET_COMBINER_FACTOR0 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR0) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_FACTOR1 ...
            NV097_SET_COMBINER_FACTOR1 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR1) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR1 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_ALPHA_OCW ...
            NV097_SET_COMBINER_ALPHA_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAO0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_COLOR_ICW ...
            NV097_SET_COMBINER_COLOR_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORI0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_VIEWPORT_SCALE ...
            NV097_SET_VIEWPORT_SCALE + 12:

        slot = (class_method - NV097_SET_VIEWPORT_SCALE) / 4;

        /* populate magic viewport scale constant */
        kelvin->constants[58].data[slot] = parameter;
        kelvin->constants[58].dirty = true;
        break;

    case NV097_SET_TRANSFORM_PROGRAM ...
            NV097_SET_TRANSFORM_PROGRAM + 0x7c:

        slot = (class_method - NV097_SET_TRANSFORM_PROGRAM) / 4;
        /* TODO: It should still work using a non-increasing slot??? */

        vertexshader = &kelvin->vertexshaders[kelvin->vertexshader_load_slot];
        assert(vertexshader->program_length < NV2A_GPU_MAX_VERTEXSHADER_LENGTH);
        vertexshader->program_data[
            vertexshader->program_length++] = parameter;
        break;

    case NV097_SET_TRANSFORM_CONSTANT ...
            NV097_SET_TRANSFORM_CONSTANT + 0x7c:

        slot = (class_method - NV097_SET_TRANSFORM_CONSTANT) / 4;

        constant = &kelvin->constants[kelvin->constant_load_slot+slot/4];
        constant->data[slot%4] = parameter;
        constant->dirty = true;
        break;

    case NV097_SET_VERTEX4F ...
            NV097_SET_VERTEX4F + 12: {

        slot = (class_method - NV097_SET_VERTEX4F) / 4;

        assert(kelvin->inline_buffer_length < NV2A_GPU_MAX_BATCH_LENGTH);
        
        InlineVertexBufferEntry *entry =
            &kelvin->inline_buffer[kelvin->inline_buffer_length];

        entry->position[slot] = parameter;
        if (slot == 3) {
            entry->diffuse =
                kelvin->vertex_attributes[NV2A_GPU_VERTEX_ATTR_DIFFUSE].inline_value;
            kelvin->inline_buffer_length++;
        }
        break;
    }

    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT ...
            NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 0x3c:

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4;
        vertex_attribute = &kelvin->vertex_attributes[slot];

        vertex_attribute->format =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE);
        vertex_attribute->count =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE);
        vertex_attribute->stride =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE);


        switch (vertex_attribute->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
            vertex_attribute->gl_type = GL_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 4;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
            vertex_attribute->gl_type = GL_UNSIGNED_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
            /* "3 signed, normalized components packed in 32-bits. (11,11,10)" */
            vertex_attribute->size = 4;
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->needs_conversion = true;
            vertex_attribute->converted_size = 4;
            vertex_attribute->converted_count = 3 * vertex_attribute->count;
            break;
        default:
            assert(false);
            break;
        }

        if (vertex_attribute->needs_conversion) {
            vertex_attribute->converted_elements = 0;
        } else {
            if (vertex_attribute->converted_buffer) {
                free(vertex_attribute->converted_buffer);
                vertex_attribute->converted_buffer = NULL;
            }
        }

        break;
    case NV097_SET_VERTEX_DATA_ARRAY_OFFSET ...
            NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 0x3c:

        slot = (class_method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4;

        kelvin->vertex_attributes[slot].dma_select =
            parameter & 0x80000000;
        kelvin->vertex_attributes[slot].offset =
            parameter & 0x7fffffff;

        kelvin->vertex_attributes[slot].converted_elements = 0;

        break;

    case NV097_SET_BEGIN_END:
        if (parameter == NV097_SET_BEGIN_END_OP_END) {

            if (kelvin->inline_buffer_length) {
                assert(!kelvin->inline_array_length);
                assert(!kelvin->inline_elements_length);
                glEnableVertexAttribArray(NV2A_GPU_VERTEX_ATTR_POSITION);
                glVertexAttribPointer(NV2A_GPU_VERTEX_ATTR_POSITION,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        sizeof(InlineVertexBufferEntry),
                        kelvin->inline_buffer);

                glEnableVertexAttribArray(NV2A_GPU_VERTEX_ATTR_DIFFUSE);
                glVertexAttribPointer(NV2A_GPU_VERTEX_ATTR_DIFFUSE,
                        4,
                        GL_UNSIGNED_BYTE,
                        GL_TRUE,
                        sizeof(InlineVertexBufferEntry),
                        &kelvin->inline_buffer[0].diffuse);

                glDrawArrays(kelvin->gl_primitive_mode,
                             0, kelvin->inline_buffer_length);
            } else if (kelvin->inline_array_length) {
                assert(!kelvin->inline_buffer_length);
                assert(!kelvin->inline_elements_length);
                unsigned int vertex_size =
                    kelvin_bind_inline_array(kelvin);
                unsigned int index_count =
                    kelvin->inline_array_length*4 / vertex_size;
                
                kelvin_bind_converted_vertex_attributes(d, kelvin,
                    true, index_count);
                glDrawArrays(kelvin->gl_primitive_mode,
                             0, index_count);
            } else if (kelvin->inline_elements_length) {
                assert(!kelvin->inline_array_length);
                assert(!kelvin->inline_buffer_length);

                uint32_t max_element = 0;
                uint32_t min_element = (uint32_t)-1;
                for (i=0; i<kelvin->inline_elements_length; i++) {
                    max_element = MAX(kelvin->inline_elements[i], max_element);
                    min_element = MIN(kelvin->inline_elements[i], min_element);
                }

                kelvin_bind_converted_vertex_attributes(d, kelvin,
                    false, max_element+1);
                glDrawElements(kelvin->gl_primitive_mode,
                               kelvin->inline_elements_length,
                               GL_UNSIGNED_INT,
                               kelvin->inline_elements);
            }/* else {
                assert(false);
            }*/
            assert(glGetError() == GL_NO_ERROR);
            glPopDebugGroup();
        } else {
            assert(parameter <= NV097_SET_BEGIN_END_OP_POLYGON);

            /* Debug output */
            {
                char buffer[128];
                sprintf(buffer,"NV2A: BEGIN_END 0x%X", parameter);
                glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, buffer);
            }

            if (pg->depth_mask || GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                     NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE)) {
                pgraph_update_surface_zeta(d, true);
            }
            if (pg->color_mask) {
                pgraph_update_surface_color(d, true);
            }

            bool use_vertex_program = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                               NV_PGRAPH_CSV0_D_MODE) == 2;
            if (use_vertex_program) {
                glEnable(GL_VERTEX_PROGRAM_ARB);
                kelvin_bind_vertex_program(kelvin);
            } else {
                glDisable(GL_VERTEX_PROGRAM_ARB);
            }

            pgraph_bind_shaders(pg);

            pgraph_bind_textures(d);
            kelvin_bind_vertex_attributes(d, kelvin);


            kelvin->gl_primitive_mode = kelvin_primitive_map[parameter];

            kelvin->inline_elements_length = 0;
            kelvin->inline_array_length = 0;
            kelvin->inline_buffer_length = 0;
        }
        if (pg->depth_mask || GET_MASK(pg->regs[NV_PGRAPH_CONTROL_1],
                 NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE)) {
            pg->surface_zeta.draw_dirty = true;
        }
        if (pg->color_mask) {
            pg->surface_color.draw_dirty = true;
        }
        break;
    CASE_4(NV097_SET_TEXTURE_OFFSET, 64):
        slot = (class_method - NV097_SET_TEXTURE_OFFSET) / 64;
        pg->textures[slot].offset = parameter;
        pg->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_FORMAT, 64):
        slot = (class_method - NV097_SET_TEXTURE_FORMAT) / 64;
        
        pg->textures[slot].dma_select =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA) == 2;
        pg->textures[slot].dimensionality =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY);
        pg->textures[slot].color_format = 
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_COLOR);
        pg->textures[slot].levels =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS);
        pg->textures[slot].log_width =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U);
        pg->textures[slot].log_height =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V);

        pg->textures[slot].dirty = true;
        pg->shaders_dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_ADDRESS, 64):
        slot = (class_method - NV097_SET_TEXTURE_ADDRESS) / 64;

        pg->textures[slot].wrap_u =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_U);
        pg->textures[slot].cylwrap_u =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_CYLWRAP_U);
        pg->textures[slot].wrap_v =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_V);
        pg->textures[slot].cylwrap_v =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_CYLWRAP_V);
        pg->textures[slot].wrap_p =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_P);
        pg->textures[slot].cylwrap_p =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_CYLWRAP_P);
        pg->textures[slot].cylwrap_q =
            GET_MASK(parameter, NV097_SET_TEXTURE_ADDRESS_CYLWRAP_Q);

        pg->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_CONTROL0, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL0) / 64;
        
        pg->textures[slot].enabled =
            parameter & NV097_SET_TEXTURE_CONTROL0_ENABLE;
        pg->textures[slot].min_mipmap_level =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP);
        pg->textures[slot].max_mipmap_level =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP);

        pg->shaders_dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_CONTROL1, 64):
        slot = (class_method - NV097_SET_TEXTURE_CONTROL1) / 64;

        pg->textures[slot].pitch =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL1_IMAGE_PITCH);

        pg->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_FILTER, 64):
        slot = (class_method - NV097_SET_TEXTURE_FILTER) / 64;

        pg->textures[slot].lod_bias =
            GET_MASK(parameter, NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS);
        pg->textures[slot].min_filter =
            GET_MASK(parameter, NV097_SET_TEXTURE_FILTER_MIN);
        pg->textures[slot].mag_filter =
            GET_MASK(parameter, NV097_SET_TEXTURE_FILTER_MAG);

        pg->textures[slot].dirty = true;
        break;
    CASE_4(NV097_SET_TEXTURE_IMAGE_RECT, 64):
        slot = (class_method - NV097_SET_TEXTURE_IMAGE_RECT) / 64;
        
        pg->textures[slot].rect_width = 
            GET_MASK(parameter, NV097_SET_TEXTURE_IMAGE_RECT_WIDTH);
        pg->textures[slot].rect_height =
            GET_MASK(parameter, NV097_SET_TEXTURE_IMAGE_RECT_HEIGHT);
        
        pg->textures[slot].dirty = true;
        break;

    case NV097_ARRAY_ELEMENT16:
        assert(kelvin->inline_elements_length < NV2A_GPU_MAX_BATCH_LENGTH);
        kelvin->inline_elements[
            kelvin->inline_elements_length++] = parameter & 0xFFFF;
        kelvin->inline_elements[
            kelvin->inline_elements_length++] = parameter >> 16;
        break;
    case NV097_ARRAY_ELEMENT32:
        assert(kelvin->inline_elements_length < NV2A_GPU_MAX_BATCH_LENGTH);
        kelvin->inline_elements[
            kelvin->inline_elements_length++] = parameter;
        break;
    case NV097_DRAW_ARRAYS: {
        unsigned int start = GET_MASK(parameter, NV097_DRAW_ARRAYS_START_INDEX);
        unsigned int count = GET_MASK(parameter, NV097_DRAW_ARRAYS_COUNT)+1;


        kelvin_bind_converted_vertex_attributes(d, kelvin,
            false, start + count);
        glDrawArrays(kelvin->gl_primitive_mode, start, count);

        break;
    }
    case NV097_INLINE_ARRAY:
        assert(kelvin->inline_array_length < NV2A_GPU_MAX_BATCH_LENGTH);
        kelvin->inline_array[
            kelvin->inline_array_length++] = parameter;
        break;

    case NV097_SET_VERTEX_DATA4UB ...
            NV097_SET_VERTEX_DATA4UB + 0x3c:
        slot = (class_method - NV097_SET_VERTEX_DATA4UB) / 4;
        kelvin->vertex_attributes[slot].inline_value = parameter;
        break;

    case NV097_SET_SEMAPHORE_OFFSET:
        kelvin->semaphore_offset = parameter;
        break;
    case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: {

        pgraph_update_surfaces(d, false);

        //qemu_mutex_unlock(&d->pgraph.lock);
        //qemu_mutex_lock_iothread();

        hwaddr semaphore_dma_len;
        uint8_t *semaphore_data = nv_dma_load_and_map(d, kelvin->dma_semaphore,
                                                      &semaphore_dma_len);
        assert(kelvin->semaphore_offset < semaphore_dma_len);
        semaphore_data += kelvin->semaphore_offset;

        stl_le_p(semaphore_data, parameter);

        //qemu_mutex_lock(&d->pgraph.lock);
        //qemu_mutex_unlock_iothread();

        break;
    }
    case NV097_SET_ZSTENCIL_CLEAR_VALUE:
        pg->regs[NV_PGRAPH_ZSTENCILCLEARVALUE] = parameter;
        break;

    case NV097_SET_COLOR_CLEAR_VALUE:
        pg->regs[NV_PGRAPH_COLORCLEARVALUE] = parameter;
        break;

    case NV097_CLEAR_SURFACE:
        /* QQQ */
        NV2A_GPU_DPRINTF("------------------CLEAR 0x%x---------------\n", parameter);
        //glClearColor(1, 0, 0, 1);

        /* Early out if we have nothing to clear */
        if (!(parameter &
            (NV097_CLEAR_SURFACE_COLOR | NV097_CLEAR_SURFACE_ZETA))) {
            break;
        }

        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x0, -1, "NV2A: CLEAR_SURFACE");

        GLbitfield gl_mask = 0;

        if (parameter & NV097_CLEAR_SURFACE_ZETA) {
            pgraph_update_surface_zeta(d, true);

            uint32_t clear_zstencil = d->pgraph.regs[NV_PGRAPH_ZSTENCILCLEARVALUE];
            GLint gl_clear_stencil;
            GLdouble gl_clear_depth;
            switch(pg->surface_zeta.format) {
                case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
                    //FIXME what happens with gl_clear_stencil?
                    gl_clear_depth = (clear_zstencil & 0xFFFF) / (double)0xFFFF;
                    break;
                case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
                    gl_clear_stencil = clear_zstencil & 0xFF;
                    gl_clear_depth = (clear_zstencil >> 8) / (double)0xFFFFFF;
                    break;
                default:
                    assert(0);
            }

            if (parameter & NV097_CLEAR_SURFACE_Z) {
                gl_mask |= GL_DEPTH_BUFFER_BIT;
                glDepthMask(GL_TRUE);
                glClearDepth(gl_clear_depth);
                //FIXME: Set defer flag
            }
            if (parameter & NV097_CLEAR_SURFACE_STENCIL) {
                gl_mask |= GL_STENCIL_BUFFER_BIT;
                glStencilMask(0xFF); /* We have 8 bits maximum anyway */
                glClearStencil(gl_clear_stencil);
                //FIXME: Set defer flag
            }

            pg->surface_zeta.draw_dirty = true;
        }

        if (parameter & NV097_CLEAR_SURFACE_COLOR) {
            pgraph_update_surface_color(d, true);

            gl_mask |= GL_COLOR_BUFFER_BIT;

            uint32_t clear_color = d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE];

            glColorMask((parameter & NV097_CLEAR_SURFACE_R)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_G)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_B)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_A)?GL_TRUE:GL_FALSE);
            //FIXME: Set defer flag

            glClearColor( ((clear_color >> 16) & 0xFF) / 255.0f, /* red */
                          ((clear_color >> 8) & 0xFF) / 255.0f,  /* green */
                          (clear_color & 0xFF) / 255.0f,         /* blue */
                          ((clear_color >> 24) & 0xFF) / 255.0f);/* alpha */

            pg->surface_color.draw_dirty = true;
        }

        glEnable(GL_SCISSOR_TEST);

        unsigned int xmin = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMIN);
        unsigned int xmax = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMAX);
        unsigned int ymin = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMIN);
        unsigned int ymax = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMAX);
        glScissor(xmin, pg->surface_height-ymax, xmax-xmin, ymax-ymin);

        NV2A_GPU_DPRINTF("------------------CLEAR 0x%x %d,%d - %d,%d  %x---------------\n",
            parameter, xmin, ymin, xmax, ymax, d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE]);

        glClear(gl_mask);

        /* The NV2A clear is just a glorified memset so we cleared masks.
           Restore the actual masks */
        update_gl_depth_mask(pg); //FIXME: Defer
        update_gl_stencil_mask(pg); //FIXME: Defer
        update_gl_color_mask(pg); //FIXME: Defer

        glDisable(GL_SCISSOR_TEST);

        glPopDebugGroup();

        break;

    case NV097_SET_CLEAR_RECT_HORIZONTAL:
        pg->regs[NV_PGRAPH_CLEARRECTX] = parameter;
        break;
    case NV097_SET_CLEAR_RECT_VERTICAL:
        pg->regs[NV_PGRAPH_CLEARRECTY] = parameter;
        break;

    case NV097_SET_SPECULAR_FOG_FACTOR ...
            NV097_SET_SPECULAR_FOG_FACTOR + 4:
        slot = (class_method - NV097_SET_SPECULAR_FOG_FACTOR) / 4;
        pg->regs[NV_PGRAPH_SPECFOGFACTOR0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_COLOR_OCW ...
            NV097_SET_COMBINER_COLOR_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORO0 + slot*4] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_COMBINER_CONTROL:
        pg->regs[NV_PGRAPH_COMBINECTL] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_SHADER_STAGE_PROGRAM:
        pg->regs[NV_PGRAPH_SHADERPROG] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_SHADER_OTHER_STAGE_INPUT:
        pg->regs[NV_PGRAPH_SHADERCTL] = parameter;
        pg->shaders_dirty = true;
        break;

    case NV097_SET_TRANSFORM_EXECUTION_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_MODE,
                 GET_MASK(parameter, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE));
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_RANGE_MODE,
                 GET_MASK(parameter, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE));
        break;
    case NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN:
        kelvin->enable_vertex_program_write = parameter;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_LOAD:
        assert(parameter < NV2A_GPU_VERTEXSHADER_SLOTS);
        kelvin->vertexshader_load_slot = parameter;
        kelvin->vertexshaders[parameter].program_length = 0; /* ??? */
        kelvin->vertexshaders[parameter].dirty = true;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_START:
        assert(parameter < NV2A_GPU_VERTEXSHADER_SLOTS);
        /* if the shader changed, dirty all the constants */
        if (parameter != kelvin->vertexshader_start_slot) {
            for (i=0; i<NV2A_GPU_VERTEXSHADER_CONSTANTS; i++) {
                kelvin->constants[i].dirty = true;
            }
        }
        kelvin->vertexshader_start_slot = parameter;
        break;
    case NV097_SET_TRANSFORM_CONSTANT_LOAD:
        assert(parameter < NV2A_GPU_VERTEXSHADER_CONSTANTS);
        kelvin->constant_load_slot = parameter;
        NV2A_GPU_DPRINTF("load to %d\n", parameter);
        break;

    case NV097_SET_SHADE_MODE:
        kelvin->shade_mode = parameter;
        {
            GLenum gl_mode;
            switch(kelvin->shade_mode) {
                case NV097_SET_SHADE_MODE_FLAT: gl_mode = GL_FLAT; break;
                case NV097_SET_SHADE_MODE_SMOOTH: gl_mode = GL_SMOOTH; break;
                default:
                    assert(0);
            }
            glShadeModel(gl_mode);
        }
        break;

#if 1
    case NV097_SET_ALPHA_TEST_ENABLE:
        set_gl_state(GL_ALPHA_TEST,kelvin->alpha_test_enable = parameter);
        break;
    case NV097_SET_BLEND_ENABLE:
        set_gl_state(GL_BLEND,kelvin->blend_enable = parameter);
        break;
    case NV097_SET_CULL_FACE:
        set_gl_cull_face(kelvin->cull_face = parameter);
        break;
    case NV097_SET_FRONT_FACE:
        set_gl_front_face(kelvin->front_face = parameter);
        break;
    case NV097_SET_CULL_FACE_ENABLE:
        set_gl_state(GL_CULL_FACE,kelvin->cull_face_enable = parameter);
        break;
#endif
#if 0
//FIXME: 2D seems to be gone because of stupid fixed function emu zbuffer calculation
    case NV097_SET_DEPTH_TEST_ENABLE:
        set_gl_state(GL_DEPTH_TEST,kelvin->depth_test_enable = parameter);
        break;
    case NV097_SET_DEPTH_FUNC:
        glDepthFunc(map_gl_compare_func(kelvin->depth_func = parameter));
        break;
#endif
#if 1
    case NV097_SET_ALPHA_FUNC:
        glAlphaFunc(map_gl_compare_func(kelvin->alpha_func = parameter),
                    kelvin->alpha_ref);
        break;
    case NV097_SET_ALPHA_REF:
        glAlphaFunc(map_gl_compare_func(kelvin->alpha_func),
                    kelvin->alpha_ref = *(float*)&parameter);
        break;
    case NV097_SET_BLEND_FUNC_SFACTOR:
        set_gl_blend_func(kelvin->blend_func_sfactor = parameter,
                          kelvin->blend_func_dfactor);
        break;
    case NV097_SET_BLEND_FUNC_DFACTOR:
        set_gl_blend_func(kelvin->blend_func_sfactor,
                          kelvin->blend_func_dfactor = parameter);
        break;
    case NV097_SET_EDGE_FLAG:
        glEdgeFlag((kelvin->edge_flag = parameter)?GL_TRUE:GL_FALSE);
        break;
#endif
    default:
        NV2A_GPU_DPRINTF("    unhandled  (0x%02x 0x%08x)\n",
                     object->graphics_class, method);
        {
            char buffer[128];
            sprintf(buffer,"NV2A: unhandled method in class 0x%02x: 0x%08x",
                    object->graphics_class, 
                    method);
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION,
                                 GL_DEBUG_TYPE_MARKER, 
                                 0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, 
                                 buffer);
        }
        break;
    }
    qemu_mutex_unlock(&d->pgraph.lock);

}


static void pgraph_context_switch(NV2A_GPUState *d, unsigned int channel_id)
{
    bool valid;
    qemu_mutex_lock(&d->pgraph.lock);
    valid = d->pgraph.channel_valid && d->pgraph.channel_id == channel_id;
    if (!valid) {
        d->pgraph.trapped_channel_id = channel_id;
    }
    qemu_mutex_unlock(&d->pgraph.lock);
    if (!valid) {
        NV2A_GPU_DPRINTF("puller needs to switch to ch %d\n", channel_id);
        
        qemu_mutex_lock_iothread();
        d->pgraph.pending_interrupts |= NV_PGRAPH_INTR_CONTEXT_SWITCH;
        update_irq(d);
        qemu_mutex_unlock_iothread();

        qemu_mutex_lock(&d->pgraph.lock);
        while (d->pgraph.pending_interrupts & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
            qemu_cond_wait(&d->pgraph.interrupt_cond, &d->pgraph.lock);
        }
        qemu_mutex_unlock(&d->pgraph.lock);
    }
}

static void pgraph_wait_fifo_access(NV2A_GPUState *d) {
    qemu_mutex_lock(&d->pgraph.lock);
    while (!d->pgraph.fifo_access) {
        qemu_cond_wait(&d->pgraph.fifo_access_cond, &d->pgraph.lock);
    }
    qemu_mutex_unlock(&d->pgraph.lock);
}

static void *pfifo_puller_thread(void *arg)
{
    NV2A_GPUState *d = arg;
    Cache1State *state = &d->pfifo.cache1;
    CacheEntry *command;
    RAMHTEntry entry;

    PGRAPHState *pg = &d->pgraph;
    glo_set_current(pg->gl_context);

    while (true) {
        qemu_mutex_lock(&state->pull_lock);
        if (!state->pull_enabled) {
            qemu_mutex_unlock(&state->pull_lock);
            glo_set_current(NULL);
            return NULL;
        }
        qemu_mutex_unlock(&state->pull_lock);

        qemu_mutex_lock(&state->cache_lock);
        while (QSIMPLEQ_EMPTY(&state->cache)) {
            qemu_cond_wait(&state->cache_cond, &state->cache_lock);

            /* we could have been woken up to tell us we should die */
            qemu_mutex_lock(&state->pull_lock);
            if (!state->pull_enabled) {
                qemu_mutex_unlock(&state->pull_lock);
                qemu_mutex_unlock(&state->cache_lock);
                glo_set_current(NULL);
                return NULL;
            }
            qemu_mutex_unlock(&state->pull_lock);
        }
        command = QSIMPLEQ_FIRST(&state->cache);
        QSIMPLEQ_REMOVE_HEAD(&state->cache, entry);
        state->cache_size--;
        qemu_mutex_unlock(&state->cache_lock);

        if (command->method == 0) {
            //qemu_mutex_lock_iothread();
            entry = ramht_lookup(d, command->parameter);
            assert(entry.valid);

            assert(entry.channel_id == state->channel_id);
            //qemu_mutex_unlock_iothread();

            switch (entry.engine) {
            case ENGINE_GRAPHICS:
                pgraph_context_switch(d, entry.channel_id);
                pgraph_wait_fifo_access(d);
                pgraph_method(d, command->subchannel, 0, entry.instance);
                break;
            default:
                assert(false);
                break;
            }

            /* the engine is bound to the subchannel */
            qemu_mutex_lock(&state->pull_lock);
            state->bound_engines[command->subchannel] = entry.engine;
            state->last_engine = entry.engine;
            qemu_mutex_unlock(&state->pull_lock);
        } else if (command->method >= 0x100) {
            /* method passed to engine */

            uint32_t parameter = command->parameter;

            /* methods that take objects.
             * TODO: Check this range is correct for the nv2a */
            if (command->method >= 0x180 && command->method < 0x200) {
                //qemu_mutex_lock_iothread();
                entry = ramht_lookup(d, parameter);
                assert(entry.valid);
                assert(entry.channel_id == state->channel_id);
                parameter = entry.instance;
                //qemu_mutex_unlock_iothread();
            }

            qemu_mutex_lock(&state->pull_lock);
            enum FIFOEngine engine = state->bound_engines[command->subchannel];
            qemu_mutex_unlock(&state->pull_lock);

            switch (engine) {
            case ENGINE_GRAPHICS:
                pgraph_wait_fifo_access(d);
                pgraph_method(d, command->subchannel,
                                   command->method, parameter);
                break;
            default:
                assert(false);
                break;
            }

            qemu_mutex_lock(&state->pull_lock);
            state->last_engine = state->bound_engines[command->subchannel];
            qemu_mutex_unlock(&state->pull_lock);
        }

        g_free(command);
    }

    glo_set_current(NULL);
    return NULL;
}

/* pusher should be fine to run from a mimo handler
 * whenever's it's convenient */
static void pfifo_run_pusher(NV2A_GPUState *d) {
    uint8_t channel_id;
    ChannelControl *control;
    Cache1State *state;
    CacheEntry *command;
    uint8_t *dma;
    hwaddr dma_len;
    uint32_t word;

    /* TODO: How is cache1 selected? */
    state = &d->pfifo.cache1;
    channel_id = state->channel_id;
    control = &d->user.channel_control[channel_id];

    if (!state->push_enabled) return;


    /* only handling DMA for now... */

    /* Channel running DMA */
    assert(d->pfifo.channel_modes & (1 << channel_id));
    assert(state->mode == FIFO_DMA);

    if (!state->dma_push_enabled) return;
    if (state->dma_push_suspended) return;

    /* We're running so there should be no pending errors... */
    assert(state->error == NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE);

    dma = nv_dma_load_and_map(d, state->dma_instance, &dma_len);

    NV2A_GPU_DPRINTF("DMA pusher: max 0x%llx, 0x%llx - 0x%llx\n",
                 dma_len, control->dma_get, control->dma_put);

    /* based on the convenient pseudocode in envytools */
    while (control->dma_get != control->dma_put) {
        if (control->dma_get >= dma_len) {

            state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION;
            break;
        }

        word = ldl_le_p(dma + control->dma_get);
        control->dma_get += 4;

        if (state->method_count) {
            /* data word of methods command */
            state->data_shadow = word;

            command = g_malloc0(sizeof(CacheEntry));
            command->method = state->method;
            command->subchannel = state->subchannel;
            command->nonincreasing = state->method_nonincreasing;
            command->parameter = word;
            qemu_mutex_lock(&state->cache_lock);
            QSIMPLEQ_INSERT_TAIL(&state->cache, command, entry);
            state->cache_size++;
            qemu_cond_signal(&state->cache_cond);
            qemu_mutex_unlock(&state->cache_lock);

            if (!state->method_nonincreasing) {
                state->method += 4;
            }
            state->method_count--;
            state->dcount++;
        } else {
            /* no command active - this is the first word of a new one */
            state->rsvd_shadow = word;
            /* match all forms */
            if ((word & 0xe0000003) == 0x20000000) {
                /* old jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0x1fffffff;
                NV2A_GPU_DPRINTF("pb OLD_JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 1) {
                /* jump */
                state->get_jmp_shadow = control->dma_get;
                control->dma_get = word & 0xfffffffc;
                NV2A_GPU_DPRINTF("pb JMP 0x%llx\n", control->dma_get);
            } else if ((word & 3) == 2) {
                /* call */
                if (state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
                    break;
                }
                state->subroutine_return = control->dma_get;
                state->subroutine_active = true;
                control->dma_get = word & 0xfffffffc;
                NV2A_GPU_DPRINTF("pb CALL 0x%llx\n", control->dma_get);
            } else if (word == 0x00020000) {
                /* return */
                if (!state->subroutine_active) {
                    state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN;
                    break;
                }
                control->dma_get = state->subroutine_return;
                state->subroutine_active = false;
                NV2A_GPU_DPRINTF("pb RET 0x%llx\n", control->dma_get);
            } else if ((word & 0xe0030003) == 0) {
                /* increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = false;
                state->dcount = 0;
            } else if ((word & 0xe0030003) == 0x40000000) {
                /* non-increasing methods */
                state->method = word & 0x1fff;
                state->subchannel = (word >> 13) & 7;
                state->method_count = (word >> 18) & 0x7ff;
                state->method_nonincreasing = true;
                state->dcount = 0;
            } else {
                NV2A_GPU_DPRINTF("pb reserved cmd 0x%llx - 0x%x\n",
                             control->dma_get, word);
                state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD;
                break;
            }
        }
    }

    if (state->error) {
        NV2A_GPU_DPRINTF("pb error: %d\n", state->error);
        assert(false);

        state->dma_push_suspended = true;

        d->pfifo.pending_interrupts |= NV_PFIFO_INTR_0_DMA_PUSHER;
        update_irq(d);
    }
}





/* PMC - card master control */
static uint64_t pmc_read(void *opaque,
                              hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PMC_BOOT_0:
        /* chipset and stepping:
         * NV2A_GPU, A02, Rev 0 */

        r = 0x02A000A2;
        break;
    case NV_PMC_INTR_0:
        /* Shows which functional units have pending IRQ */
        r = d->pmc.pending_interrupts;
        break;
    case NV_PMC_INTR_EN_0:
        /* Selects which functional units can cause IRQs */
        r = d->pmc.enabled_interrupts;
        break;
    default:
        break;
    }

    reg_log_read(NV_PMC, addr, r);
    return r;
}
static void pmc_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PMC, addr, val);

    switch (addr) {
    case NV_PMC_INTR_0:
        /* the bits of the interrupts to clear are wrtten */
        d->pmc.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PMC_INTR_EN_0:
        d->pmc.enabled_interrupts = val;
        update_irq(d);
        break;
    default:
        break;
    }
}


/* PBUS - bus control */
static uint64_t pbus_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PBUS_PCI_NV_0:
        r = pci_get_long(d->dev.config + PCI_VENDOR_ID);
        break;
    case NV_PBUS_PCI_NV_1:
        r = pci_get_long(d->dev.config + PCI_COMMAND);
        break;
    case NV_PBUS_PCI_NV_2:
        r = pci_get_long(d->dev.config + PCI_CLASS_REVISION);
        break;
    case NV_PBUS_PCI_NV_3:
        r = pci_get_long(d->dev.config + PCI_CACHE_LINE_SIZE);
        break;
    case NV_PBUS_PCI_NV_4:
        r = pci_get_long(d->dev.config + PCI_BASE_ADDRESS_0);
        break;
    case NV_PBUS_PCI_NV_5:
        r = pci_get_long(d->dev.config + PCI_BASE_ADDRESS_1);
        break;
    case NV_PBUS_PCI_NV_6:
        r = pci_get_long(d->dev.config + PCI_BASE_ADDRESS_2);
        break;
    /* XXX: .. */
    case NV_PBUS_PCI_NV_11:
        r = pci_get_long(d->dev.config + PCI_SUBSYSTEM_VENDOR_ID);
        break;
    case NV_PBUS_PCI_NV_12:
        r = pci_get_long(d->dev.config + PCI_ROM_ADDRESS);
        break;
    case NV_PBUS_PCI_NV_13:
        r = pci_get_long(d->dev.config + PCI_CAPABILITY_LIST);
        break;
    /* XXX: .. */
    case NV_PBUS_PCI_NV_15:
        r = pci_get_long(d->dev.config + PCI_INTERRUPT_LINE);
        break;
    /* XXX: .. */
    default:
        break;
    }

    reg_log_read(NV_PBUS, addr, r);
    return r;
}
static void pbus_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PBUS, addr, val);

    switch (addr) {
    case NV_PBUS_PCI_NV_0:
        /* Read only */
        break;
    case NV_PBUS_PCI_NV_1:
        pci_set_long(d->dev.config + PCI_COMMAND, val);
        break;
    case NV_PBUS_PCI_NV_2:
        /* Read only */
        break;
    case NV_PBUS_PCI_NV_3:
        pci_set_long(d->dev.config + PCI_CACHE_LINE_SIZE, val);
        break;
    case NV_PBUS_PCI_NV_4:
        /* XXX: Align to 16MB? */
        pci_set_long(d->dev.config + PCI_BASE_ADDRESS_0, val);
        break;
    case NV_PBUS_PCI_NV_5:
        /* XXX: Align to 16MB? */
        pci_set_long(d->dev.config + PCI_BASE_ADDRESS_1, val);
        break;
    case NV_PBUS_PCI_NV_6:
        /* XXX: Align to 512kB? This is masked differently than NV_4 and NV_5! */
        pci_set_long(d->dev.config + PCI_BASE_ADDRESS_2, val);
        break;
    /* XXX: .. */
    case NV_PBUS_PCI_NV_11:
        /* Read only */
        break;
    case NV_PBUS_PCI_NV_12:
        pci_set_long(d->dev.config + PCI_ROM_ADDRESS, val);
        break;
    case NV_PBUS_PCI_NV_13:
        pci_set_long(d->dev.config + PCI_CAPABILITY_LIST, val);
        break;
    /* XXX: .. */
    case NV_PBUS_PCI_NV_15:
        pci_set_long(d->dev.config + PCI_INTERRUPT_LINE, val);
        break;
    /* XXX: .. */
    default:
        break;
    }
}


/* PFIFO - MMIO and DMA FIFO submission to PGRAPH and VPE */
static uint64_t pfifo_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    int i;
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFIFO_INTR_0:
        r = d->pfifo.pending_interrupts;
        break;
    case NV_PFIFO_INTR_EN_0:
        r = d->pfifo.enabled_interrupts;
        break;
    case NV_PFIFO_RAMHT:
        SET_MASK(r, NV_PFIFO_RAMHT_BASE_ADDRESS, d->pfifo.ramht_address >> 12);
        SET_MASK(r, NV_PFIFO_RAMHT_SEARCH, d->pfifo.ramht_search);
        SET_MASK(r, NV_PFIFO_RAMHT_SIZE, ffs(d->pfifo.ramht_size)-13);
        break;
    case NV_PFIFO_RAMFC:
        SET_MASK(r, NV_PFIFO_RAMFC_BASE_ADDRESS1,
                 d->pfifo.ramfc_address1 >> 10);
        SET_MASK(r, NV_PFIFO_RAMFC_BASE_ADDRESS2,
                 d->pfifo.ramfc_address2 >> 10);
        SET_MASK(r, NV_PFIFO_RAMFC_SIZE, d->pfifo.ramfc_size);
        break;
    case NV_PFIFO_RUNOUT_STATUS:
        r = NV_PFIFO_RUNOUT_STATUS_LOW_MARK; /* low mark empty */
        break;
    case NV_PFIFO_MODE:
        r = d->pfifo.channel_modes;
        break;
    case NV_PFIFO_DMA:
        r = d->pfifo.channels_pending_push;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        r = d->pfifo.cache1.push_enabled;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        SET_MASK(r, NV_PFIFO_CACHE1_PUSH1_CHID, d->pfifo.cache1.channel_id);
        SET_MASK(r, NV_PFIFO_CACHE1_PUSH1_MODE, d->pfifo.cache1.mode);
        break;
    case NV_PFIFO_CACHE1_STATUS:
        qemu_mutex_lock(&d->pfifo.cache1.cache_lock);
        if (QSIMPLEQ_EMPTY(&d->pfifo.cache1.cache)) {
            r |= NV_PFIFO_CACHE1_STATUS_LOW_MARK; /* low mark empty */
        }
        qemu_mutex_unlock(&d->pfifo.cache1.cache_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_ACCESS,
                 d->pfifo.cache1.dma_push_enabled);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_STATUS,
                 d->pfifo.cache1.dma_push_suspended);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_PUSH_BUFFER, 1); /* buffer emoty */
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE,
                 d->pfifo.cache1.method_nonincreasing);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD,
                 d->pfifo.cache1.method >> 2);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL,
                 d->pfifo.cache1.subchannel);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT,
                 d->pfifo.cache1.method_count);
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                 d->pfifo.cache1.error);
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        SET_MASK(r, NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS,
                 d->pfifo.cache1.dma_instance >> 4);
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_put;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        r = d->user.channel_control[d->pfifo.cache1.channel_id].dma_get;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        r = d->pfifo.cache1.subroutine_return
            | d->pfifo.cache1.subroutine_active;
        break;
    case NV_PFIFO_CACHE1_PULL0:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        r = d->pfifo.cache1.pull_enabled;
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        for (i=0; i<NV2A_GPU_NUM_SUBCHANNELS; i++) {
            r |= d->pfifo.cache1.bound_engines[i] << (i*2);
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        r = d->pfifo.cache1.dcount;
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        r = d->pfifo.cache1.get_jmp_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        r = d->pfifo.cache1.rsvd_shadow;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        r = d->pfifo.cache1.data_shadow;
        break;
    default:
        break;
    }

    reg_log_read(NV_PFIFO, addr, r);
    return r;
}
static void pfifo_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned int size)
{
    int i;
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PFIFO, addr, val);

    switch (addr) {
    case NV_PFIFO_INTR_0:
        d->pfifo.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PFIFO_INTR_EN_0:
        d->pfifo.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PFIFO_RAMHT:
        d->pfifo.ramht_address =
            GET_MASK(val, NV_PFIFO_RAMHT_BASE_ADDRESS) << 12;
        d->pfifo.ramht_size = 1 << (GET_MASK(val, NV_PFIFO_RAMHT_SIZE)+12);
        d->pfifo.ramht_search = GET_MASK(val, NV_PFIFO_RAMHT_SEARCH);
        break;
    case NV_PFIFO_RAMFC:
        d->pfifo.ramfc_address1 =
            GET_MASK(val, NV_PFIFO_RAMFC_BASE_ADDRESS1) << 10;
        d->pfifo.ramfc_address2 =
            GET_MASK(val, NV_PFIFO_RAMFC_BASE_ADDRESS2) << 10;
        d->pfifo.ramfc_size = GET_MASK(val, NV_PFIFO_RAMFC_SIZE);
        break;
    case NV_PFIFO_MODE:
        d->pfifo.channel_modes = val;
        break;
    case NV_PFIFO_DMA:
        d->pfifo.channels_pending_push = val;
        break;

    case NV_PFIFO_CACHE1_PUSH0:
        d->pfifo.cache1.push_enabled = val & NV_PFIFO_CACHE1_PUSH0_ACCESS;
        break;
    case NV_PFIFO_CACHE1_PUSH1:
        d->pfifo.cache1.channel_id = GET_MASK(val, NV_PFIFO_CACHE1_PUSH1_CHID);
        d->pfifo.cache1.mode = GET_MASK(val, NV_PFIFO_CACHE1_PUSH1_MODE);
        assert(d->pfifo.cache1.channel_id < NV2A_GPU_NUM_CHANNELS);
        break;
    case NV_PFIFO_CACHE1_DMA_PUSH:
        d->pfifo.cache1.dma_push_enabled =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_ACCESS);
        if (d->pfifo.cache1.dma_push_suspended
             && !GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_STATUS)) {
            d->pfifo.cache1.dma_push_suspended = false;
            pfifo_run_pusher(d);
        }
        d->pfifo.cache1.dma_push_suspended =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_PUSH_STATUS);
        break;
    case NV_PFIFO_CACHE1_DMA_STATE:
        d->pfifo.cache1.method_nonincreasing =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE);
        d->pfifo.cache1.method =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD) << 2;
        d->pfifo.cache1.subchannel =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL);
        d->pfifo.cache1.method_count =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT);
        d->pfifo.cache1.error =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_STATE_ERROR);
        break;
    case NV_PFIFO_CACHE1_DMA_INSTANCE:
        d->pfifo.cache1.dma_instance =
            GET_MASK(val, NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS) << 4;
        break;
    case NV_PFIFO_CACHE1_DMA_PUT:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_put = val;
        break;
    case NV_PFIFO_CACHE1_DMA_GET:
        d->user.channel_control[d->pfifo.cache1.channel_id].dma_get = val;
        break;
    case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        d->pfifo.cache1.subroutine_return =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_RETURN_OFFSET);
        d->pfifo.cache1.subroutine_active =
            (val & NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE);
        break;
    case NV_PFIFO_CACHE1_PULL0:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        if ((val & NV_PFIFO_CACHE1_PULL0_ACCESS)
             && !d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = true;

            /* fire up puller thread */
            qemu_thread_create(&d->pfifo.puller_thread, "nv2a/pfifo_puller",
                               pfifo_puller_thread, d, QEMU_THREAD_DETACHED);
        } else if (!(val & NV_PFIFO_CACHE1_PULL0_ACCESS)
                     && d->pfifo.cache1.pull_enabled) {
            d->pfifo.cache1.pull_enabled = false;

            /* the puller thread should die, wake it up. */
            qemu_cond_broadcast(&d->pfifo.cache1.cache_cond);
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_ENGINE:
        qemu_mutex_lock(&d->pfifo.cache1.pull_lock);
        for (i=0; i<NV2A_GPU_NUM_SUBCHANNELS; i++) {
            d->pfifo.cache1.bound_engines[i] = (val >> (i*2)) & 3;
        }
        qemu_mutex_unlock(&d->pfifo.cache1.pull_lock);
        break;
    case NV_PFIFO_CACHE1_DMA_DCOUNT:
        d->pfifo.cache1.dcount =
            (val & NV_PFIFO_CACHE1_DMA_DCOUNT_VALUE);
        break;
    case NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW:
        d->pfifo.cache1.get_jmp_shadow =
            (val & NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW_OFFSET);
        break;
    case NV_PFIFO_CACHE1_DMA_RSVD_SHADOW:
        d->pfifo.cache1.rsvd_shadow = val;
        break;
    case NV_PFIFO_CACHE1_DMA_DATA_SHADOW:
        d->pfifo.cache1.data_shadow = val;
        break;
    default:
        break;
    }
}


static uint64_t prma_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMA, addr, 0);
    return 0;
}
static void prma_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMA, addr, val);
}


static void pvideo_vga_invalidate(NV2A_GPUState *d)
{
    int y1 = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                      NV_PVIDEO_POINT_OUT_Y);
    int y2 = y1 + GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                           NV_PVIDEO_SIZE_OUT_HEIGHT);
    NV2A_GPU_DPRINTF("pvideo_vga_invalidate %d %d\n", y1, y2);
    vga_invalidate_scanlines(&d->vga, y1, y2);
}

static uint64_t pvideo_read(void *opaque,
                            hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PVIDEO_STOP:
        r = 0;
        break;
    default:
        r = d->pvideo.regs[addr];
        break;
    }

    reg_log_read(NV_PVIDEO, addr, r);
    return r;
}
static void pvideo_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PVIDEO, addr, val);

    switch (addr) {
    case NV_PVIDEO_BUFFER:
        d->pvideo.regs[addr] = val;
        d->vga.enable_overlay = true;
        pvideo_vga_invalidate(d);
        break;
    case NV_PVIDEO_STOP:
        d->pvideo.regs[NV_PVIDEO_BUFFER] = 0;
        d->vga.enable_overlay = false;
        pvideo_vga_invalidate(d);
        break;
    default:
        d->pvideo.regs[addr] = val;
        break;
    }
}




/* PIMTER - time measurement and time-based alarms */
static uint64_t ptimer_get_clock(NV2A_GPUState *d)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    d->pramdac.core_clock_freq * d->ptimer.numerator,
                    get_ticks_per_sec() * d->ptimer.denominator);
}
static uint64_t ptimer_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PTIMER_INTR_0:
        r = d->ptimer.pending_interrupts;
        break;
    case NV_PTIMER_INTR_EN_0:
        r = d->ptimer.enabled_interrupts;
        break;
    case NV_PTIMER_NUMERATOR:
        r = d->ptimer.numerator;
        break;
    case NV_PTIMER_DENOMINATOR:
        r = d->ptimer.denominator;
        break;
    case NV_PTIMER_TIME_0:
        r = (ptimer_get_clock(d) & 0x7ffffff) << 5;
        break;
    case NV_PTIMER_TIME_1:
        r = (ptimer_get_clock(d) >> 27) & 0x1fffffff;
        break;
    default:
        break;
    }

    reg_log_read(NV_PTIMER, addr, r);
    return r;
}
static void ptimer_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PTIMER, addr, val);

    switch (addr) {
    case NV_PTIMER_INTR_0:
        d->ptimer.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PTIMER_INTR_EN_0:
        d->ptimer.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PTIMER_DENOMINATOR:
        d->ptimer.denominator = val;
        break;
    case NV_PTIMER_NUMERATOR:
        d->ptimer.numerator = val;
        break;
    case NV_PTIMER_ALARM_0:
        d->ptimer.alarm_time = val;
        break;
    default:
        break;
    }
}


static uint64_t pcounter_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PCOUNTER, addr, 0);
    return 0;
}
static void pcounter_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PCOUNTER, addr, val);
}


static uint64_t pvpe_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PVPE, addr, 0);
    return 0;
}
static void pvpe_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PVPE, addr, val);
}


static uint64_t ptv_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PTV, addr, 0);
    return 0;
}
static void ptv_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PTV, addr, val);
}


static uint64_t prmfb_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMFB, addr, 0);
    return 0;
}
static void prmfb_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMFB, addr, val);
}


/* PRMVIO - aliases VGA sequencer and graphics controller registers */
static uint64_t prmvio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    reg_log_read(NV_PRMVIO, addr, r);
    return r;
}
static void prmvio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PRMVIO, addr, val);

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t pfb_read(void *opaque,
                         hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFB_CFG0:
        /* 3-4 memory partitions. The debug bios checks this. */
        r = 3;
        break;
    case NV_PFB_CSTATUS:
        r = memory_region_size(d->vram);
        break;
    case NV_PFB_WBC:
        r = 0; /* Flush not pending. */
        break;
    default:
        r = d->pfb.regs[addr];
        break;
    }

    reg_log_read(NV_PFB, addr, r);
    return r;
}
static void pfb_write(void *opaque, hwaddr addr,
                       uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PFB, addr, val);

    switch (addr) {
    default:
        d->pfb.regs[addr] = val;
        break;
    }
}


static uint64_t pstraps_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PSTRAPS, addr, 0);
    return 0;
}
static void pstraps_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PSTRAPS, addr, val);
}

/* PGRAPH - accelerated 2d/3d drawing engine */
static uint64_t pgraph_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PGRAPH_INTR:
        r = d->pgraph.pending_interrupts;
        break;
    case NV_PGRAPH_INTR_EN:
        r = d->pgraph.enabled_interrupts;
        break;
    case NV_PGRAPH_NSOURCE:
        r = d->pgraph.notify_source;
        break;
    case NV_PGRAPH_CTX_USER:
        qemu_mutex_lock(&d->pgraph.lock);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D,
                 d->pgraph.context[d->pgraph.channel_id].channel_3d);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHANNEL_3D_VALID, 1);
        SET_MASK(r, NV_PGRAPH_CTX_USER_SUBCH, 
                 d->pgraph.context[d->pgraph.channel_id].subchannel << 13);
        SET_MASK(r, NV_PGRAPH_CTX_USER_CHID, d->pgraph.channel_id);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_TRAPPED_ADDR:
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_CHID, d->pgraph.trapped_channel_id);
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_SUBCH, d->pgraph.trapped_subchannel);
        SET_MASK(r, NV_PGRAPH_TRAPPED_ADDR_MTHD, d->pgraph.trapped_method);
        break;
    case NV_PGRAPH_TRAPPED_DATA_LOW:
        r = d->pgraph.trapped_data[0];
        break;
    case NV_PGRAPH_FIFO:
        SET_MASK(r, NV_PGRAPH_FIFO_ACCESS, d->pgraph.fifo_access);
        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        r = d->pgraph.context_table >> 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        r = d->pgraph.context_address >> 4;
        break;
    default:
        r = d->pgraph.regs[addr];
        break;
    }

    reg_log_read(NV_PGRAPH, addr, r);
    return r;
}
static void pgraph_set_context_user(NV2A_GPUState *d, uint32_t val)
{
    d->pgraph.channel_id = (val & NV_PGRAPH_CTX_USER_CHID) >> 24;

    d->pgraph.context[d->pgraph.channel_id].channel_3d =
        GET_MASK(val, NV_PGRAPH_CTX_USER_CHANNEL_3D);
    d->pgraph.context[d->pgraph.channel_id].subchannel =
        GET_MASK(val, NV_PGRAPH_CTX_USER_SUBCH);
}
static void pgraph_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PGRAPH, addr, val);

    switch (addr) {
    case NV_PGRAPH_INTR:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.pending_interrupts &= ~val;
        qemu_cond_broadcast(&d->pgraph.interrupt_cond);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_INTR_EN:
        d->pgraph.enabled_interrupts = val;
        break;
    case NV_PGRAPH_CTX_CONTROL:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.channel_valid = (val & NV_PGRAPH_CTX_CONTROL_CHID);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_CTX_USER:
        qemu_mutex_lock(&d->pgraph.lock);
        pgraph_set_context_user(d, val);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_INCREMENT:
        if (val & NV_PGRAPH_INCREMENT_READ_3D) {
            qemu_sem_post(&d->pgraph.read_3d);
        }
        break;
    case NV_PGRAPH_FIFO:
        qemu_mutex_lock(&d->pgraph.lock);
        d->pgraph.fifo_access = GET_MASK(val, NV_PGRAPH_FIFO_ACCESS);
        qemu_cond_broadcast(&d->pgraph.fifo_access_cond);
        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    case NV_PGRAPH_CHANNEL_CTX_TABLE:
        d->pgraph.context_table =
            (val & NV_PGRAPH_CHANNEL_CTX_TABLE_INST) << 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_POINTER:
        d->pgraph.context_address =
            (val & NV_PGRAPH_CHANNEL_CTX_POINTER_INST) << 4;
        break;
    case NV_PGRAPH_CHANNEL_CTX_TRIGGER:
        qemu_mutex_lock(&d->pgraph.lock);

        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_READ_IN) {
            NV2A_GPU_DPRINTF("PGRAPH: read channel %d context from %llx\n",
                         d->pgraph.channel_id, d->pgraph.context_address);

            uint8_t *context_ptr = d->ramin_ptr + d->pgraph.context_address;
            uint32_t context_user = ldl_le_p(context_ptr);

            NV2A_GPU_DPRINTF("    - CTX_USER = 0x%x\n", context_user);


            pgraph_set_context_user(d, context_user);
        }
        if (val & NV_PGRAPH_CHANNEL_CTX_TRIGGER_WRITE_OUT) {
            /* do stuff ... */
        }

        qemu_mutex_unlock(&d->pgraph.lock);
        break;
    default:
        d->pgraph.regs[addr] = val;
        break;
    }
}


static uint64_t pcrtc_read(void *opaque,
                                hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
        case NV_PCRTC_INTR_0:
            r = d->pcrtc.pending_interrupts;
            break;
        case NV_PCRTC_INTR_EN_0:
            r = d->pcrtc.enabled_interrupts;
            break;
        case NV_PCRTC_START:
            r = d->pcrtc.start;
            break;
        default:
            break;
    }

    reg_log_read(NV_PCRTC, addr, r);
    return r;
}
static void pcrtc_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PCRTC, addr, val);

    switch (addr) {
    case NV_PCRTC_INTR_0:
        d->pcrtc.pending_interrupts &= ~val;
        update_irq(d);
        break;
    case NV_PCRTC_INTR_EN_0:
        d->pcrtc.enabled_interrupts = val;
        update_irq(d);
        break;
    case NV_PCRTC_START:
        val &= 0x03FFFFFF;
        assert(val < memory_region_size(d->vram));
        d->pcrtc.start = val;
        break;
    default:
        break;
    }
}


/* PRMCIO - aliases VGA CRTC and attribute controller registers */
static uint64_t prmcio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;
    uint64_t r = vga_ioport_read(&d->vga, addr);

    reg_log_read(NV_PRMCIO, addr, r);
    return r;
}
static void prmcio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_PRMCIO, addr, val);

    switch (addr) {
    case VGA_ATT_W:
        /* Cromwell sets attrs without enabling VGA_AR_ENABLE_DISPLAY
         * (which should result in a blank screen).
         * Either nvidia's hardware is lenient or it is set through
         * something else. The former seems more likely.
         */
        if (d->vga.ar_flip_flop == 0) {
            val |= VGA_AR_ENABLE_DISPLAY;
        }
        break;
    default:
        break;
    }

    vga_ioport_write(&d->vga, addr, val);
}


static uint64_t pramdac_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    uint64_t r = 0;
    switch (addr & ~3) {
    case NV_PRAMDAC_NVPLL_COEFF:
        r = d->pramdac.core_clock_coeff;
        break;
    case NV_PRAMDAC_MPLL_COEFF:
        r = d->pramdac.memory_clock_coeff;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        r = d->pramdac.video_clock_coeff;
        break;
    case NV_PRAMDAC_PLL_TEST_COUNTER:
        /* emulated PLLs locked instantly? */
        r = NV_PRAMDAC_PLL_TEST_COUNTER_VPLL2_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_NVPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_MPLL_LOCK
             | NV_PRAMDAC_PLL_TEST_COUNTER_VPLL_LOCK;
        break;
    default:
        break;
    }

    /* Surprisingly, QEMU doesn't handle unaligned access for you properly */
    r >>= 32 - 8 * size - 8 * (addr & 3);

    NV2A_GPU_DPRINTF("PRAMDAC: read %d [0x%llx] -> %llx\n", size, addr, r);
    return r;
}
static void pramdac_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;
    uint32_t m, n, p;

    reg_log_write(NV_PRAMDAC, addr, val);

    switch (addr) {
    case NV_PRAMDAC_NVPLL_COEFF:
        d->pramdac.core_clock_coeff = val;

        m = val & NV_PRAMDAC_NVPLL_COEFF_MDIV;
        n = (val & NV_PRAMDAC_NVPLL_COEFF_NDIV) >> 8;
        p = (val & NV_PRAMDAC_NVPLL_COEFF_PDIV) >> 16;

        if (m == 0) {
            d->pramdac.core_clock_freq = 0;
        } else {
            d->pramdac.core_clock_freq = (NV2A_GPU_CRYSTAL_FREQ * n)
                                          / (1 << p) / m;
        }

        break;
    case NV_PRAMDAC_MPLL_COEFF:
        d->pramdac.memory_clock_coeff = val;
        break;
    case NV_PRAMDAC_VPLL_COEFF:
        d->pramdac.video_clock_coeff = val;
        break;
    default:
        break;
    }
}


static uint64_t prmdio_read(void *opaque,
                                  hwaddr addr, unsigned int size)
{
    reg_log_read(NV_PRMDIO, addr, 0);
    return 0;
}
static void prmdio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned int size)
{
    reg_log_write(NV_PRMDIO, addr, val);
}


/* PRAMIN - RAMIN access */
/*
static uint64_t pramin_read(void *opaque,
                                 hwaddr addr, unsigned int size)
{
    NV2A_GPU_DPRINTF("nv2a PRAMIN: read [0x%llx] -> 0x%llx\n", addr, r);
    return 0;
}
static void pramin_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    NV2A_GPU_DPRINTF("nv2a PRAMIN: [0x%llx] = 0x%02llx\n", addr, val);
}*/


/* USER - PFIFO MMIO and DMA submission area */
static uint64_t user_read(void *opaque,
                               hwaddr addr, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_GPU_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    uint64_t r = 0;
    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            r = control->dma_put;
            break;
        case NV_USER_DMA_GET:
            r = control->dma_get;
            break;
        case NV_USER_REF:
            r = control->ref;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        assert(false);
    }

    reg_log_read(NV_USER, addr, r);
    return r;
}
static void user_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned int size)
{
    NV2A_GPUState *d = opaque;

    reg_log_write(NV_USER, addr, val);

    unsigned int channel_id = addr >> 16;
    assert(channel_id < NV2A_GPU_NUM_CHANNELS);

    ChannelControl *control = &d->user.channel_control[channel_id];

    if (d->pfifo.channel_modes & (1 << channel_id)) {
        /* DMA Mode */
        switch (addr & 0xFFFF) {
        case NV_USER_DMA_PUT:
            control->dma_put = val;

            if (d->pfifo.cache1.push_enabled) {
                pfifo_run_pusher(d);
            }
            break;
        case NV_USER_DMA_GET:
            control->dma_get = val;
            break;
        case NV_USER_REF:
            control->ref = val;
            break;
        default:
            break;
        }
    } else {
        /* PIO Mode */
        assert(false);
    }

}




typedef struct NV2A_GPUBlockInfo {
    const char* name;
    hwaddr offset;
    uint64_t size;
    MemoryRegionOps ops;
} NV2A_GPUBlockInfo;

static const struct NV2A_GPUBlockInfo blocktable[] = {
    [ NV_PMC ]  = {
        .name = "PMC",
        .offset = 0x000000,
        .size   = 0x001000,
        .ops = {
            .read = pmc_read,
            .write = pmc_write,
        },
    },
    [ NV_PBUS ]  = {
        .name = "PBUS",
        .offset = 0x001000,
        .size   = 0x001000,
        .ops = {
            .read = pbus_read,
            .write = pbus_write,
        },
    },
    [ NV_PFIFO ]  = {
        .name = "PFIFO",
        .offset = 0x002000,
        .size   = 0x002000,
        .ops = {
            .read = pfifo_read,
            .write = pfifo_write,
        },
    },
    [ NV_PRMA ]  = {
        .name = "PRMA",
        .offset = 0x007000,
        .size   = 0x001000,
        .ops = {
            .read = prma_read,
            .write = prma_write,
        },
    },
    [ NV_PVIDEO ]  = {
        .name = "PVIDEO",
        .offset = 0x008000,
        .size   = 0x001000,
        .ops = {
            .read = pvideo_read,
            .write = pvideo_write,
        },
    },
    [ NV_PTIMER ]  = {
        .name = "PTIMER",
        .offset = 0x009000,
        .size   = 0x001000,
        .ops = {
            .read = ptimer_read,
            .write = ptimer_write,
        },
    },
    [ NV_PCOUNTER ]  = {
        .name = "PCOUNTER",
        .offset = 0x00a000,
        .size   = 0x001000,
        .ops = {
            .read = pcounter_read,
            .write = pcounter_write,
        },
    },
    [ NV_PVPE ]  = {
        .name = "PVPE",
        .offset = 0x00b000,
        .size   = 0x001000,
        .ops = {
            .read = pvpe_read,
            .write = pvpe_write,
        },
    },
    [ NV_PTV ]  = {
        .name = "PTV",
        .offset = 0x00d000,
        .size   = 0x001000,
        .ops = {
            .read = ptv_read,
            .write = ptv_write,
        },
    },
    [ NV_PRMFB ]  = {
        .name = "PRMFB",
        .offset = 0x0a0000,
        .size   = 0x020000,
        .ops = {
            .read = prmfb_read,
            .write = prmfb_write,
        },
    },
    [ NV_PRMVIO ]  = {
        .name = "PRMVIO",
        .offset = 0x0c0000,
        .size   = 0x001000,
        .ops = {
            .read = prmvio_read,
            .write = prmvio_write,
        },
    },
    [ NV_PFB ]  = {
        .name = "PFB",
        .offset = 0x100000,
        .size   = 0x001000,
        .ops = {
            .read = pfb_read,
            .write = pfb_write,
        },
    },
    [ NV_PSTRAPS ]  = {
        .name = "PSTRAPS",
        .offset = 0x101000,
        .size   = 0x001000,
        .ops = {
            .read = pstraps_read,
            .write = pstraps_write,
        },
    },
    [ NV_PGRAPH ]  = {
        .name = "PGRAPH",
        .offset = 0x400000,
        .size   = 0x002000,
        .ops = {
            .read = pgraph_read,
            .write = pgraph_write,
        },
    },
    [ NV_PCRTC ]  = {
        .name = "PCRTC",
        .offset = 0x600000,
        .size   = 0x001000,
        .ops = {
            .read = pcrtc_read,
            .write = pcrtc_write,
        },
    },
    [ NV_PRMCIO ]  = {
        .name = "PRMCIO",
        .offset = 0x601000,
        .size   = 0x001000,
        .ops = {
            .read = prmcio_read,
            .write = prmcio_write,
        },
    },
    [ NV_PRAMDAC ]  = {
        .name = "PRAMDAC",
        .offset = 0x680000,
        .size   = 0x001000,
        .ops = {
            .read = pramdac_read,
            .write = pramdac_write,
        },
    },
    [ NV_PRMDIO ]  = {
        .name = "PRMDIO",
        .offset = 0x681000,
        .size   = 0x001000,
        .ops = {
            .read = prmdio_read,
            .write = prmdio_write,
        },
    },
    /*[ NV_PRAMIN ]  = {
        .name = "PRAMIN",
        .offset = 0x700000,
        .size   = 0x100000,
        .ops = {
            .read = pramin_read,
            .write = pramin_write,
        },
    },*/
    [ NV_USER ]  = {
        .name = "USER",
        .offset = 0x800000,
        .size   = 0x800000,
        .ops = {
            .read = user_read,
            .write = user_write,
        },
    },
};

static const char* nv2a_gpu_reg_names[] = {};
static const char* nv2a_gpu_method_names[] = {};

static void reg_log_read(int block, hwaddr addr, uint64_t val) {
    if (blocktable[block].name) {
        hwaddr naddr = blocktable[block].offset + addr;
        if (naddr < sizeof(nv2a_gpu_reg_names)/sizeof(const char*)
                && nv2a_gpu_reg_names[naddr]) {
            NV2A_GPU_DPRINTF("%s: read [%s] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_gpu_reg_names[naddr], val);
        } else {
            NV2A_GPU_DPRINTF("%s: read [" TARGET_FMT_plx "] -> 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_GPU_DPRINTF("(%d?): read [" TARGET_FMT_plx "] -> 0x%" PRIx64 "\n",
                block, addr, val);
    }
}

static void reg_log_write(int block, hwaddr addr, uint64_t val) {
    if (blocktable[block].name) {
        hwaddr naddr = blocktable[block].offset + addr;
        if (naddr < sizeof(nv2a_gpu_reg_names)/sizeof(const char*)
                && nv2a_gpu_reg_names[naddr]) {
            NV2A_GPU_DPRINTF("%s: [%s] = 0x%" PRIx64 "\n",
                    blocktable[block].name, nv2a_gpu_reg_names[naddr], val);
        } else {
            NV2A_GPU_DPRINTF("%s: [" TARGET_FMT_plx "] = 0x%" PRIx64 "\n",
                    blocktable[block].name, addr, val);
        }
    } else {
        NV2A_GPU_DPRINTF("(%d?): [" TARGET_FMT_plx "] = 0x%" PRIx64 "\n",
                block, addr, val);
    }
}
static void pgraph_method_log(unsigned int subchannel,
                              unsigned int graphics_class,
                              unsigned int method, uint32_t parameter) {
    static unsigned int last = 0;
    static unsigned int count = 0;
    if (last == 0x1800 && method != last) {
        NV2A_GPU_DPRINTF("pgraph method (%d) 0x%x * %d\n",
                        subchannel, last, count);  
    }
    if (method != 0x1800) {
        const char* method_name = NULL;
        unsigned int nmethod = 0;
        switch (graphics_class) {
            case NV_KELVIN_PRIMITIVE:
                nmethod = method | (0x5c << 16);
                break;
            case NV_CONTEXT_SURFACES_2D:
                nmethod = method | (0x6d << 16);
                break;
            default:
                break;
        }
        if (nmethod != 0
            && nmethod < sizeof(nv2a_gpu_method_names)/sizeof(const char*)) {
            method_name = nv2a_gpu_method_names[nmethod];
        }
        if (method_name) {
            NV2A_GPU_DPRINTF("pgraph method (%d): %s (0x%x)\n",
                     subchannel, method_name, parameter);
        } else {
            NV2A_GPU_DPRINTF("pgraph method (%d): 0x%x -> 0x%04x (0x%x)\n",
                     subchannel, graphics_class, method, parameter);
        }

    }
    if (method == last) { count++; }
    else {count = 0; }
    last = method;
}

static uint8_t cliptobyte(int x)
{
    return (uint8_t)((x < 0) ? 0 : ((x > 255) ? 255 : x));
}

static void nv2a_gpu_overlay_draw_line(VGACommonState *vga, uint8_t *line, int y)
{
    NV2A_GPU_DPRINTF("nv2a_gpu_overlay_draw_line\n");

    NV2A_GPUState *d = container_of(vga, NV2A_GPUState, vga);
    DisplaySurface *surface = qemu_console_surface(d->vga.con);

    int surf_bpp = surface_bytes_per_pixel(surface);
    int surf_width = surface_width(surface);

    if (!(d->pvideo.regs[NV_PVIDEO_BUFFER] & NV_PVIDEO_BUFFER_0_USE)) return;

    hwaddr base = d->pvideo.regs[NV_PVIDEO_BASE];
    hwaddr limit = d->pvideo.regs[NV_PVIDEO_LIMIT];
    hwaddr offset = d->pvideo.regs[NV_PVIDEO_OFFSET];

    int in_width = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN],
                            NV_PVIDEO_SIZE_IN_WIDTH);
    int in_height = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN],
                             NV_PVIDEO_SIZE_IN_HEIGHT);
    int in_s = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_S);
    int in_t = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_T);
    int in_pitch = GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT],
                            NV_PVIDEO_FORMAT_PITCH);
    int in_color = GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT],
                            NV_PVIDEO_FORMAT_COLOR);

    // TODO: support other color formats
    assert(in_color == NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8);

    int out_width = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                             NV_PVIDEO_SIZE_OUT_WIDTH);
    int out_height = GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                             NV_PVIDEO_SIZE_OUT_HEIGHT);
    int out_x = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                         NV_PVIDEO_POINT_OUT_X);
    int out_y = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                         NV_PVIDEO_POINT_OUT_Y);


    if (y < out_y || y >= out_y + out_height) return;

    // TODO: scaling, color keys

    int in_y = y - out_y;
    if (in_y >= in_height) return;

    assert(offset + in_pitch * (in_y + 1) <= limit);
    uint8_t *in_line = d->vram_ptr + base + offset + in_pitch * in_y;

    int x;
    for (x=0; x<out_width; x++) {
        int ox = out_x + x;
        if (ox >= surf_width) break;
        int ix = in_s + x;
        if (ix >= in_width) break;

        // YUY2 to RGB
        int c, d, e;
        c = (int)in_line[ix * 2] - 16;
        if (ix % 2) {
            d = (int)in_line[ix * 2 - 1] - 128;
            e = (int)in_line[ix * 2 + 1] - 128;
        } else {
            d = (int)in_line[ix * 2 + 1] - 128;
            e = (int)in_line[ix * 2 + 3] - 128;
        }
        int r, g, b;
        r = cliptobyte((298 * c + 409 * e + 128) >> 8);
        g = cliptobyte((298 * c - 100 * d - 208 * e + 128) >> 8);
        b = cliptobyte((298 * c + 516 * d + 128) >> 8);

        unsigned int pixel = vga->rgb_to_pixel(r, g, b);
        switch (surf_bpp) {
        case 1:
            ((uint8_t*)line)[ox] = pixel;
            break;
        case 2:
            ((uint16_t*)line)[ox] = pixel;
            break;
        case 4:
            ((uint32_t*)line)[ox] = pixel;
            break;
        default:
            assert(false);
            break;
        }
    }
}

static int nv2a_gpu_get_bpp(VGACommonState *s)
{
    if ((s->cr[0x28] & 3) == 3) {
        return 32;
    }
    return (s->cr[0x28] & 3) * 8;
}

static void nv2a_gpu_get_offsets(VGACommonState *s,
                             uint32_t *pline_offset,
                             uint32_t *pstart_addr,
                             uint32_t *pline_compare)
{
    NV2A_GPUState *d = container_of(s, NV2A_GPUState, vga);
    uint32_t start_addr, line_offset, line_compare;

    line_offset = s->cr[0x13]
        | ((s->cr[0x19] & 0xe0) << 3)
        | ((s->cr[0x25] & 0x20) << 6);
    line_offset <<= 3;
    *pline_offset = line_offset;

    start_addr = d->pcrtc.start / 4;
    *pstart_addr = start_addr;

    line_compare = s->cr[VGA_CRTC_LINE_COMPARE] |
        ((s->cr[VGA_CRTC_OVERFLOW] & 0x10) << 4) |
        ((s->cr[VGA_CRTC_MAX_SCAN] & 0x40) << 3);
    *pline_compare = line_compare;
}


static void nv2a_gpu_vga_gfx_update(void *opaque)
{
    VGACommonState *vga = opaque;
    vga->hw_ops->gfx_update(vga);

    NV2A_GPUState *d = container_of(vga, NV2A_GPUState, vga);
    d->pcrtc.pending_interrupts |= NV_PCRTC_INTR_0_VBLANK;
    update_irq(d);
}

static void nv2a_gpu_init_memory(NV2A_GPUState *d, MemoryRegion *ram)
{
    /* xbox is UMA - vram *is* ram */
    d->vram = ram;

     /* PCI exposed vram */
    memory_region_init_alias(&d->vram_pci, OBJECT(d), "nv2a-vram-pci", d->vram,
                             0, memory_region_size(d->vram));
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH, &d->vram_pci);


    /* RAMIN - should be in vram somewhere, but not quite sure where atm */
    memory_region_init_ram(&d->ramin, OBJECT(d), "nv2a-ramin", 0x100000);
    /* memory_region_init_alias(&d->ramin, "nv2a-ramin", &d->vram,
                         memory_region_size(&d->vram) - 0x100000,
                         0x100000); */

    memory_region_add_subregion(&d->mmio, 0x700000, &d->ramin);


    d->vram_ptr = memory_region_get_ram_ptr(d->vram);
    d->ramin_ptr = memory_region_get_ram_ptr(&d->ramin);

    memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A_GPU_COLOR);
    memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A_GPU_ZETA);
    memory_region_set_log(d->vram, true, DIRTY_MEMORY_NV2A_GPU_RESOURCE);

    /* hacky. swap out vga's vram */
    memory_region_destroy(&d->vga.vram);
    memory_region_init_alias(&d->vga.vram, OBJECT(d), "vga.vram",
                             d->vram, 0, memory_region_size(d->vram));
    d->vga.vram_ptr = memory_region_get_ram_ptr(&d->vga.vram);
    vga_dirty_log_start(&d->vga);
}

static int nv2a_gpu_initfn(PCIDevice *dev)
{
    int i;
    NV2A_GPUState *d;

    d = NV2A_GPU_DEVICE(dev);

    d->pcrtc.start = 0;

    d->pramdac.core_clock_coeff = 0x00011c01; /* 189MHz...? */
    d->pramdac.core_clock_freq = 189000000;
    d->pramdac.memory_clock_coeff = 0;
    d->pramdac.video_clock_coeff = 0x0003C20D; /* 25182Khz...? */

    /* Setup IRQ */
    pci_set_byte(d->dev.config + PCI_INTERRUPT_PIN, 0x01); /* XXX: Why isn't this overwritten by the driver? */
    pci_set_byte(d->dev.config + PCI_INTERRUPT_LINE, 0x03); /* XXX: Why isn't this overwritten by the driver? */

    /* legacy VGA shit */
    VGACommonState *vga = &d->vga;
    vga->vram_size_mb = 4;
    /* seems to start in color mode */
    vga->msr = VGA_MIS_COLOR;

    vga_common_init(vga, OBJECT(dev));
    vga->get_bpp = nv2a_gpu_get_bpp;
    vga->get_offsets = nv2a_gpu_get_offsets;
    vga->overlay_draw_line = nv2a_gpu_overlay_draw_line;

    d->hw_ops = *vga->hw_ops;
    d->hw_ops.gfx_update = nv2a_gpu_vga_gfx_update;
    vga->con = graphic_console_init(DEVICE(dev), 0, &d->hw_ops, vga);


    /* mmio */
    memory_region_init(&d->mmio, OBJECT(dev), "nv2a-mmio", 0x1000000);
    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    for (i=0; i<sizeof(blocktable)/sizeof(blocktable[0]); i++) {
        if (!blocktable[i].name) continue;
        memory_region_init_io(&d->block_mmio[i], OBJECT(dev),
                              &blocktable[i].ops, d,
                              blocktable[i].name, blocktable[i].size);
        memory_region_add_subregion(&d->mmio, blocktable[i].offset,
                                    &d->block_mmio[i]);
    }

    /* init fifo cache1 */
    qemu_mutex_init(&d->pfifo.cache1.pull_lock);
    qemu_mutex_init(&d->pfifo.cache1.cache_lock);
    qemu_cond_init(&d->pfifo.cache1.cache_cond);
    QSIMPLEQ_INIT(&d->pfifo.cache1.cache);

    pgraph_init(&d->pgraph);

    return 0;
}

static void nv2a_gpu_exitfn(PCIDevice *dev)
{
    NV2A_GPUState *d;
    d = NV2A_GPU_DEVICE(dev);

    qemu_mutex_destroy(&d->pfifo.cache1.pull_lock);
    qemu_mutex_destroy(&d->pfifo.cache1.cache_lock);
    qemu_cond_destroy(&d->pfifo.cache1.cache_cond);

    pgraph_destroy(&d->pgraph);
}

static void nv2a_gpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_GEFORCE_NV2A;
    k->revision = 161;
    k->class_id = PCI_CLASS_DISPLAY_3D;
    k->init = nv2a_gpu_initfn;
    k->exit = nv2a_gpu_exitfn;

    dc->desc = "GeForce NV2A Integrated Graphics";
}

static const TypeInfo nv2a_gpu_info = {
    .name          = "nv2a",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NV2A_GPUState),
    .class_init    = nv2a_gpu_class_init,
};

static void nv2a_gpu_register(void)
{
    type_register_static(&nv2a_gpu_info);
}
type_init(nv2a_gpu_register);





void nv2a_gpu_init(PCIBus *bus, int devfn, MemoryRegion *ram)
{
    PCIDevice *dev = pci_create_simple(bus, devfn, "nv2a");
    NV2A_GPUState *d = NV2A_GPU_DEVICE(dev);
    nv2a_gpu_init_memory(d, ram);
}

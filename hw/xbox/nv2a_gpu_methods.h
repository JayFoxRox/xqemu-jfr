/*
 * QEMU Geforce NV2A GPU method defines
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
#   define NV097_FLIP_INCREMENT_WRITE                         0x0097012c
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
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5     0x1
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5     0x2
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5                0x3
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8     0x4
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8     0x5
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8 0x6
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8 0x7
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8              0x8
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_B8                    0x9
#           define NV097_SET_SURFACE_FORMAT_COLOR_LE_G8B8                  0xA
#       define NV097_SET_SURFACE_FORMAT_ZETA                      0x000000F0
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z16                       0x1
#           define NV097_SET_SURFACE_FORMAT_ZETA_Z24S8                     0x2
#       define NV097_SET_SURFACE_FORMAT_TYPE                      0x00000F00
#           define NV097_SET_SURFACE_FORMAT_TYPE_PITCH                     0x1
#           define NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE                   0x2
#       define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING             0x0000F000
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1         0x0
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2  0x1
#           define NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_SQUARE_OFFSET_4  0x2
#       define NV097_SET_SURFACE_FORMAT_WIDTH                     0x00FF0000
#       define NV097_SET_SURFACE_FORMAT_HEIGHT                    0xFF000000
#   define NV097_SET_SURFACE_PITCH                            0x0097020C
#       define NV097_SET_SURFACE_PITCH_COLOR                      0x0000FFFF
#       define NV097_SET_SURFACE_PITCH_ZETA                       0xFFFF0000
#   define NV097_SET_SURFACE_COLOR_OFFSET                     0x00970210
#   define NV097_SET_SURFACE_ZETA_OFFSET                      0x00970214
#   define NV097_SET_COMBINER_ALPHA_ICW                       0x00970260
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW0                0x00970288
#   define NV097_SET_COMBINER_SPECULAR_FOG_CW1                0x0097028C
#   define NV097_SET_FOG_COLOR                                0x009702a8
#       define NV097_SET_FOG_COLOR_RED                            0x000000FF
#       define NV097_SET_FOG_COLOR_GREEN                          0x0000FF00
#       define NV097_SET_FOG_COLOR_BLUE                           0x00FF0000
#       define NV097_SET_FOG_COLOR_ALPHA                          0xFF000000
#   define NV097_SET_FOG_MODE                                 0x0097029c
#       define NV097_SET_FOG_MODE_LINEAR                          0x00002601
#       define NV097_SET_FOG_MODE_EXP                             0x00000800
#       define NV097_SET_FOG_MODE_EXP2                            0x00000801
#       define NV097_SET_FOG_MODE_EXP_ABS                         0x00000802
#       define NV097_SET_FOG_MODE_EXP2_ABS                        0x00000803
#       define NV097_SET_FOG_MODE_LINEAR_ABS                      0x00000804
#   define NV097_SET_FOG_ENABLE                               0x009702a4 /* Bool */
#   define NV097_SET_COLOR_MASK                               0x00970358
#       define NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE            0xFF000000 /* Bool */
#       define NV097_SET_COLOR_MASK_RED_WRITE_ENABLE              0x00FF0000 /* Bool */
#       define NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE            0x0000FF00 /* Bool */
#       define NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE             0x000000FF /* Bool */
#   define NV097_SET_CLIP_MIN                                 0x00970394
#   define NV097_SET_CLIP_MAX                                 0x00970398
#   define NV097_SET_TEXTURE_MATRIX_ENABLE                    0x00970420 /* 4 Slots, bool */
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

// ? are probably 16 bit values, 2 per method. But are they signed? unsigned? float?
#   define NV097_SET_VERTEX3F                                 0x00971500 /* 3 floats */
#   define NV097_SET_VERTEX4F                                 0x00971518 /* 4 floats */
#   define NV097_SET_VERTEX4S                                 0x00971528 /* 2 ? */
#   define NV097_SET_NORMAL3F                                 0x00971530 /* 3 floats */
#   define NV097_SET_NORMAL3S                                 0x00971540 /* 2 ? */
#   define NV097_SET_DIFFUSE_COLOR4F                          0x00971550 /* 4 floats */
#   define NV097_SET_DIFFUSE_COLOR3F                          0x00971560 /* 3 floats */
#   define NV097_SET_DIFFUSE_COLOR4UB                         0x0097156c /* 1 DWORD */
#   define NV097_SET_SPECULAR_COLOR4F                         0x00971570 /* 4 floats */
#   define NV097_SET_SPECULAR_COLOR3F                         0x00971580 /* 3 floats */
#   define NV097_SET_SPECULAR_COLOR4UB                        0x0097158c /* 1 DWORD */
#   define NV097_SET_TEXCOORD0_2F                             0x00971590 /* 2 floats */
#   define NV097_SET_TEXCOORD0_2S                             0x00971598 /* 1 ? */
#   define NV097_SET_TEXCOORD0_4F                             0x009715a0 /* 4 floats */
#   define NV097_SET_TEXCOORD0_4S                             0x009715b0 /* 2 ? */
#   define NV097_SET_TEXCOORD1_2F                             0x009715b8 /* 2 floats */
#   define NV097_SET_TEXCOORD1_2S                             0x009715c0 /* 1 ? */
#   define NV097_SET_TEXCOORD1_4F                             0x009715c8 /* 4 floats */
#   define NV097_SET_TEXCOORD1_4S                             0x009715d8 /* 2 ? */
#   define NV097_SET_TEXCOORD2_2F                             0x009715e0 /* 2 floats */
#   define NV097_SET_TEXCOORD2_2S                             0x009715e8 /* 1 ? */
#   define NV097_SET_TEXCOORD2_4F                             0x009715f0 /* 4 floats */
#   define NV097_SET_TEXCOORD2_4S                             0x00971600 /* 2 ? */
#   define NV097_SET_TEXCOORD3_2F                             0x00971608 /* 2 floats */
#   define NV097_SET_TEXCOORD3_2S                             0x00971610 /* 1 ?*/
#   define NV097_SET_TEXCOORD3_4F                             0x00971620 /* 4 floats */
#   define NV097_SET_TEXCOORD3_4S                             0x00971630 /* 2 ? */
#   define NV097_SET_FOG1F                                    0x00971698 /* 1 float */
#   define NV097_SET_WEIGHT1F                                 0x0097169c /* 1 float */
#   define NV097_SET_WEIGHT2F                                 0x009716a0 /* 2 floats */
#   define NV097_SET_WEIGHT3F                                 0x009716b0 /* 3 floats */
#   define NV097_SET_WEIGHT4F                                 0x009716c0 /* 4 floats */

#   define NV097_SET_VERTEX_DATA_ARRAY_OFFSET                 0x00971720
#   define NV097_SET_VERTEX_DATA_ARRAY_FORMAT                 0x00971760
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE            0x0000000F
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D     0
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1         1
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F          2
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL     4
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K       5
#           define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP        6
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE            0x000000F0
#       define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE          0xFFFFFF00

#define NV097_SET_SHADER_CLIP_PLANE_MODE                      0x009717f8
/* stage 0 { Bit  0: s_gez, Bit  1: t_gez, Bit  2: r_gez, Bit  3: q_gez }
   stage 1 { Bit  4: s_gez, Bit  5: t_gez, Bit  6: r_gez, Bit  7: q_gez }
   stage 2 { Bit  8: s_gez, Bit  9: t_gez, Bit 10: r_gez, Bit 11: q_gez }
   stage 3 { Bit 12: s_gez, Bit 13: t_gez, Bit 14: r_gez, Bit 15: q_gez }
*/

#   define NV097_SET_BEGIN_END                                0x009717fc
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
#   define NV097_SET_POINT_SIZE                               0x0097043c
#   define NV097_ARRAY_ELEMENT16                              0x00971800
#   define NV097_ARRAY_ELEMENT32                              0x00971808
#   define NV097_DRAW_ARRAYS                                  0x00971810
#       define NV097_DRAW_ARRAYS_COUNT                            0xFF000000
#       define NV097_DRAW_ARRAYS_START_INDEX                      0x00FFFFFF
#   define NV097_INLINE_ARRAY                                 0x00971818
#   define NV097_SET_EYE_VECTOR                               0x0097181c /* 3 floats */
#   define NV097_SET_VERTEX_DATA2F_M                          0x00971880 /* 16*2 floats */
#   define NV097_SET_VERTEX_DATA4F_M                          0x00971a00 /* 16*4 floats */
#   define NV097_SET_VERTEX_DATA2S                            0x00971900 /* 16 ? */
#   define NV097_SET_VERTEX_DATA4UB                           0x00971940 /* 16 DWORDs */
#   define NV097_SET_VERTEX_DATA4S_M                          0x00971980 /* 16*2 ? */
#   define NV097_SET_TEXTURE_OFFSET                           0x00971B00
#   define NV097_SET_TEXTURE_FORMAT                           0x00971B04
#       define NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE            (1 << 2) /* Bool */
#       define NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA               0x00000003
#       define NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY            0x000000F0
#       define NV097_SET_TEXTURE_FORMAT_COLOR                     0x0000FF00
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8            0x01
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
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_X8_Y24_FIXED 0x2A
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_X8_Y24_FLOAT 0x2B
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED 0x2C
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FLOAT 0x2D
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED 0x2E
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FLOAT 0x2F
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED 0x30
#           define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT 0x31
#           define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8      0x3A
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
#       define NV097_SET_TEXTURE_CONTROL0_ALPHA_KILL_ENABLE       0x00000004
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

#   define NV097_SET_CONTROL0                                 0x00970290
#       define NV097_SET_CONTROL0_COLOR_SPACE_CONVERT             0xF0000000
#           define NV097_SET_CONTROL0_COLOR_SPACE_CONVERT_PASS          0x0
#           define NV097_SET_CONTROL0_COLOR_SPACE_CONVERT_CRYCB_TO_RGB  0x1
#           define NV097_SET_CONTROL0_COLOR_SPACE_CONVERT_SCRYSCB_TO_RGB 0x2
#       define NV097_SET_CONTROL0_PREMULTIPLIEDALPHA              0x0F000000 /* Bool */
#       define NV097_SET_CONTROL0_TEXTUREPERSPECTIVE              0x00F00000 /* Bool */
#       define NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE            0x000F0000 /* Bool */
#       define NV097_SET_CONTROL0_Z_FORMAT                        0x0000FF00
#           define NV097_SET_CONTROL0_Z_FORMAT_FIXED                  0x00
#           define NV097_SET_CONTROL0_Z_FORMAT_FLOAT                  0x01
#       define NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE            0x000000FF

#   define NV097_SET_DEPTH_MASK                               0x0097035c /* Bool */
#   define NV097_SET_ALPHA_TEST_ENABLE                        0x00970300 /* Bool */
#   define NV097_SET_BLEND_ENABLE                             0x00970304 /* Bool */
#   define NV097_SET_CULL_FACE_ENABLE                         0x00970308 /* Bool */
#   define NV097_SET_DEPTH_TEST_ENABLE                        0x0097030c /* Bool */
#   define NV097_SET_STENCIL_TEST_ENABLE                      0x0097032c /* Bool */

#   define NV097_SET_ALPHA_REF                                0x00970340

#   define NV097_SET_STENCIL_MASK                             0x00970360

#   define NV097_SET_STENCIL_FUNC_REF                         0x00970368
#   define NV097_SET_STENCIL_FUNC_MASK                        0x0097036c

#   define NV097_SET_STENCIL_OP_FAIL                          0x00970370
#   define NV097_SET_STENCIL_OP_ZFAIL                         0x00970374
#   define NV097_SET_STENCIL_OP_ZPASS                         0x00970378
// Used for the 3 methods above
#       define NV097_SET_STENCIL_OP_KEEP                          0x00001E00
#       define NV097_SET_STENCIL_OP_ZERO                          0x00000000
#       define NV097_SET_STENCIL_OP_REPLACE                       0x00001E01
#       define NV097_SET_STENCIL_OP_INCRSAT                       0x00001E02
#       define NV097_SET_STENCIL_OP_DECRSAT                       0x00001E03
#       define NV097_SET_STENCIL_OP_INVERT                        0x0000150A
#       define NV097_SET_STENCIL_OP_INCR                          0x00008507
#       define NV097_SET_STENCIL_OP_DECR                          0x00008508

// These are comparision functions and use map_method_to_..._func
#   define NV097_SET_STENCIL_FUNC                            0x00970364
#   define NV097_SET_DEPTH_FUNC                              0x00970354
#   define NV097_SET_ALPHA_FUNC                              0x0097033c

#       define NV097_FUNC_NEVER                                   0x00000200
#       define NV097_FUNC_LESS                                    0x00000201
#       define NV097_FUNC_EQUAL                                   0x00000202
#       define NV097_FUNC_LEQUAL                                  0x00000203
#       define NV097_FUNC_GREATER                                 0x00000204
#       define NV097_FUNC_NOTEQUAL                                0x00000205
#       define NV097_FUNC_GEQUAL                                  0x00000206
#       define NV097_FUNC_ALWAYS                                  0x00000207

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

#   define NV097_SET_EDGE_FLAG                               0x009716bc /* Bool */

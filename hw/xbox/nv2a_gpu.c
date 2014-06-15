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

#include "hw/xbox/swizzle.h"
#include "hw/xbox/u_format_r11g11b10f.h"

#include "hw/xbox/nv2a_gpu_vsh.h"
#include "hw/xbox/nv2a_gpu_psh.h"
#include "hw/xbox/nv2a_gpu.h"

//#define LP // Future feature, ignore this for now
#ifdef LP
#include "launch_program.fox"
#endif

//Hack: this will ignore clip_x and clip_y, sonic heroes e3 is a good test case for this for now, it's the only software I'm aware of that does this yet
#define FORCE_NOXY
#define CHECKXY(pg) { printf("Clip with XY: %d x %d at %d, %d\n",(pg)->clip_width,(pg)->clip_height,(pg)->clip_x,(pg)->clip_y); }

#define DEBUG_NV2A_GPU_DISABLE_MIPMAP
//#define DEBUG_NV2A_GPU_EXPORT
#define DEBUG_NV2A_GPU
#ifdef DEBUG_NV2A_GPU
# define NV2A_GPU_DPRINTF(format, ...)       printf("nv2a: " format, ## __VA_ARGS__)
#else
# define NV2A_GPU_DPRINTF(format, ...)       do { } while (0)
#endif

#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
const char** feedback_varyings[] = {
  "debug_v0",
/*
  "debug_R12[0]",
  "debug_R12[1]",
  "debug_R12[2]",
  "debug_R12[3]",
  "debug_R12[4]",
  "debug_R12[5]",
  "debug_R12[6]",
  "debug_R12[7]",
  "debug_R12[8]",
*/
  "debug_oPos"
};
#endif

#define GLSL_LOG_LENGTH 8192

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

#include "nv2a_gpu_registers.h"
#include "nv2a_gpu_methods.h"


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
#ifdef DEBUG_NV2A_GPU_DISABLE_MIPMAP
    GL_LINEAR,
    GL_LINEAR,
    GL_LINEAR,
    GL_LINEAR,
#else
    GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_LINEAR,
#endif
    GL_LINEAR, /* TODO: Convolution filter... */
};

static const GLenum kelvin_texture_mag_filter_map[] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    0,
    GL_LINEAR /* TODO: Convolution filter... */
};

/* Attribute conversion */

static inline void* convert_cmp_to_f(unsigned int stride, size_t num_elements, const void* in) {
#if 0
    void* buffer = malloc(3*sizeof(float)*num_elements);
    for(i = 0; i < num_elements; i++) {
        r11g11b10f_to_float3(ldl_le_p(in),
                             (float*)out);
    }
    return buffer;
#endif
return NULL;
}

/* Texture conversion */

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
        {2, false, GL_RGBA, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5] =
        {2, false, GL_RGB,  GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4] =
        {2, false, GL_RGBA, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5] =
        {2, false, GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] =
        {4, false, GL_RGBA, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] =
        {4, false, GL_RGB,  GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV },

    [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8] =
        {4, false, GL_RGB,  GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, convert_cr8yb8cb8ya8_to_a8r8g8b8},

    /* TODO: 8-bit palettized textures */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8] =
        {1, false, GL_RGBA, GL_COLOR_INDEX, GL_UNSIGNED_BYTE }, /*FIXME: glColorTable or do this in a conversion function, this also means conversion functions must be able to modify the hash or dirty regions */

    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, GL_RGBA},
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8] =
        {4, false, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, GL_RGBA},

    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5] =
        {2, true, GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] =
        {4, true, GL_RGBA, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    /* TODO: how do opengl alpha textures work? */
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] =
        {1, false, GL_ALPHA,  GL_ALPHA,  GL_UNSIGNED_BYTE},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8] = /* FIXME: Unsure if AY8 is A4Y4 or A8Y8? Used in conker demo at startup for fonts */
        {2, false, GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] =
        {4, true, GL_RGB,  GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] = /* FIXME: What is this? Isn't this depth + luminance? */
        {2, true, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT},
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8] =
        {4, false, GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8] =
        {4, true, GL_RGBA, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV},
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

#define GET_PG_REG(pg, v, mask) GET_MASK(pg->regs[NV_PGRAPH_ ## v], NV_PGRAPH_ ## v ## _ ## mask)

#define SET_PG_REG(pg, v, mask, val) SET_MASK(pg->regs[NV_PGRAPH_ ## v], NV_PGRAPH_ ## v ## _ ## mask, val)

#define CASE_2(v, step)                                              \
    case (v)+(step):                                                 \
    case (v)

#define CASE_3(v, step)                                              \
    case (v)+(step)*2:                                               \
    CASE_2(v, step)

#define CASE_4(v, step)                                              \
    case (v)+(step)*3:                                               \
    CASE_3(v, step)

#define CASE_RANGE(v, count)                                         \
    case (v) ... ((v)+((count)-1)*4):                                \
        slot = (class_method - (v)) / 4;


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

    void(*converter)(void); //FIXME: Type..
    bool needs_conversion;
    uint8_t *converted_buffer;
    unsigned int converted_elements;
    unsigned int converted_size;
    unsigned int converted_count;

    GLenum gl_size;
    GLenum gl_type;
    GLboolean gl_normalize;
} VertexAttribute;

typedef struct VertexShaderConstant {
    bool dirty;
    uint32_t data[4];
} VertexShaderConstant;

typedef struct VertexShader {
    bool dirty;
    uint32_t program_data[NV2A_GPU_MAX_VERTEXSHADER_LENGTH*4]; /* Each instruction is 16 byte */
} VertexShader;

typedef struct Texture {
    bool dirty;
    bool enabled;

    unsigned int dimensionality;
    unsigned int color_format;
    unsigned int levels;
    unsigned int log_width, log_height, log_depth;

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

    bool alphakill;

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
    bool compare_mode[4][4];
    bool alphakill[4];

    /* vertex shader */
    guint vertex_shader_hash;

} ShaderState;

typedef struct Surface {
    bool draw_dirty;
    unsigned int pitch;
    unsigned int format;

    hwaddr offset;
} Surface;

typedef struct InlineVertexBufferEntry {
    float v[16][4];
} InlineVertexBufferEntry;

typedef struct KelvinState {
    hwaddr dma_notifies;
    hwaddr dma_state;
    hwaddr dma_semaphore;
    unsigned int semaphore_offset;

    uint32_t shade_mode;

    bool blend_enable;
    bool cull_face_enable;

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

    int FIXME_REMOVEME_comp_src; /*/ FIXME: REMOVEME! This is to see how
                                            applications upload the composite
                                            matrix */

    hwaddr dma_color, dma_zeta;
#ifdef CACHE_DMA
    DMAObject dma_object_color, dma_object_zeta;
#endif
    Surface surface_color, surface_zeta;
    uint8_t surface_type;
    uint8_t surface_anti_aliasing;
    uint8_t surface_swizzle_width_shift, surface_swizzle_height_shift;
    unsigned int clip_x, clip_y;
    unsigned int clip_width, clip_height;

    hwaddr dma_a, dma_b;
    Texture textures[NV2A_GPU_MAX_TEXTURES];

    struct {
        bool depth_mask;
        bool color_mask;
        bool stencil_mask;
        bool depth_test;
        bool depth_test_func;
        bool alpha_test;
        bool alpha_test_func;
        bool stencil_test;
        bool stencil_test_func;
        bool stencil_test_op;
        bool fog;
        bool fog_color;
        bool fog_mode;
        bool eye_vector;
        // Prepared but not used
        bool front_face;
        bool blend;
        bool blend_func;
        bool cull_face;
        bool cull_face_mode;
        bool edge_flag;
        bool framebuffer;
        // Old flags which might be removed again?
        bool shaders;
    } dirty;

    struct {
        GHashTable* renderbuffer;
        GHashTable* framebuffer;
        // Old cache which will possibly renamed or removed?
        GHashTable *shader;
    } cache;

    GLuint gl_program;

    float eye_vector[3];

    float composite_matrix[16]; //FIXME: Should be stored within the constant array?

    bool texture_matrix_enable[4];

    float texture_matrix0[16];
    float texture_matrix1[16];
    float texture_matrix2[16];
    float texture_matrix3[16];

    GloContext *gl_context;

    hwaddr dma_vertex_a, dma_vertex_b;

    GraphicsSubchannel subchannel_data[NV2A_GPU_NUM_SUBCHANNELS];

    GLenum gl_primitive_mode;

    bool enable_vertex_program_write;

    unsigned int vertexshader_start_slot;
    unsigned int vertexshader_load_slot;
    VertexShader vertexshader;

    unsigned int constant_load_slot;
    VertexShaderConstant constants[NV2A_GPU_VERTEXSHADER_CONSTANTS];

    VertexAttribute vertex_attributes[NV2A_GPU_VERTEXSHADER_ATTRIBUTES];

    unsigned int inline_array_length;
    uint32_t inline_array[NV2A_GPU_MAX_BATCH_LENGTH];

    unsigned int inline_elements_length;
    uint32_t inline_elements[NV2A_GPU_MAX_BATCH_LENGTH];

    unsigned int inline_buffer_length;
    InlineVertexBufferEntry inline_buffer[NV2A_GPU_MAX_BATCH_LENGTH];

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

#if 1 // Experimental stuff, not public yet, change to #if 0
#include "nv2a_names.fox"
#define nv2a_gpu_reg_names nv2a_reg_names
#define nv2a_gpu_method_names nv2a_method_names
#else
static const char* nv2a_gpu_reg_names[] = {};
static const char* nv2a_gpu_method_names[] = {};
#endif

static inline void pgraph_update_surfaces(NV2A_GPUState *d, bool upload, bool zeta, bool color); //FIXME: Remove!
#include "hw/xbox/nv2a_gpu_debugger.h"

//FIXME: Move to top and use c file
#include "hw/xbox/nv2a_gpu_cache.h"


#define NV2A_GPU_DEVICE(obj) \
    OBJECT_CHECK(NV2A_GPUState, (obj), "nv2a")

/* new style (work in function) so we can easily restore the state anytime */

static inline uint32_t map_method_to_register_func(uint32_t method_func) {
    switch(method_func) {
    case NV097_FUNC_NEVER:
        return NV_PGRAPH_FUNC_NEVER;
    case NV097_FUNC_LESS:
        return NV_PGRAPH_FUNC_LESS;
    case NV097_FUNC_EQUAL:
        return NV_PGRAPH_FUNC_EQUAL;
    case NV097_FUNC_LEQUAL:
        return NV_PGRAPH_FUNC_LEQUAL;
    case NV097_FUNC_GREATER:
        return NV_PGRAPH_FUNC_GREATER;
    case NV097_FUNC_NOTEQUAL:
        return NV_PGRAPH_FUNC_NOTEQUAL;
    case NV097_FUNC_GEQUAL:
        return NV_PGRAPH_FUNC_GEQUAL;
    case NV097_FUNC_ALWAYS:
        return NV_PGRAPH_FUNC_ALWAYS;
    default:
        assert(0);
    }
}

static inline uint32_t map_method_to_register_stencil_op(
    uint32_t method_stencil_op)
{
    switch(method_stencil_op) {
    case NV097_SET_STENCIL_OP_KEEP:
        return NV_PGRAPH_STENCIL_OP_KEEP;
    case NV097_SET_STENCIL_OP_ZERO:
        return NV_PGRAPH_STENCIL_OP_ZERO;
    case NV097_SET_STENCIL_OP_REPLACE:
        return NV_PGRAPH_STENCIL_OP_REPLACE;
    case NV097_SET_STENCIL_OP_INCRSAT:
        return NV_PGRAPH_STENCIL_OP_INCRSAT;
    case NV097_SET_STENCIL_OP_DECRSAT:
        return NV_PGRAPH_STENCIL_OP_DECRSAT;
    case NV097_SET_STENCIL_OP_INVERT:
        return NV_PGRAPH_STENCIL_OP_INVERT;
    case NV097_SET_STENCIL_OP_INCR:
        return NV_PGRAPH_STENCIL_OP_INCR;
    case NV097_SET_STENCIL_OP_DECR:
        return NV_PGRAPH_STENCIL_OP_DECR;
    default:
        assert(0);
    }
}

static inline uint32_t map_register_to_gl_stencil_op(
    uint32_t register_stencil_op)
{
    switch(register_stencil_op) {
    case NV_PGRAPH_STENCIL_OP_KEEP:
        return GL_KEEP;
    case NV_PGRAPH_STENCIL_OP_ZERO:
        return GL_ZERO;
    case NV_PGRAPH_STENCIL_OP_REPLACE:
        return GL_REPLACE;
    case NV_PGRAPH_STENCIL_OP_INCRSAT:
        return GL_INCR;
    case NV_PGRAPH_STENCIL_OP_DECRSAT:
        return GL_DECR;
    case NV_PGRAPH_STENCIL_OP_INVERT:
        return GL_INVERT;
    case NV_PGRAPH_STENCIL_OP_INCR:
        return GL_INCR_WRAP;
    case NV_PGRAPH_STENCIL_OP_DECR:
        return GL_DECR_WRAP;
    default:
        assert(0);
    }
}

static inline GLenum map_register_to_gl_func(uint32_t method_func)
{
    switch(method_func) {
    case NV_PGRAPH_FUNC_NEVER:
        return GL_NEVER;
    case NV_PGRAPH_FUNC_LESS:
        return GL_LESS;
    case NV_PGRAPH_FUNC_EQUAL:
        return GL_EQUAL;
    case NV_PGRAPH_FUNC_LEQUAL:
        return GL_LEQUAL;
    case NV_PGRAPH_FUNC_GREATER:
        return GL_GREATER;
    case NV_PGRAPH_FUNC_NOTEQUAL:
        return GL_NOTEQUAL;
    case NV_PGRAPH_FUNC_GEQUAL:
        return GL_GEQUAL;
    case NV_PGRAPH_FUNC_ALWAYS:
        return GL_ALWAYS;
    default:
        assert(0);
    }
}

// Updates a dirty state and its dirty bit, returns the new state
static inline bool update_gl_state(GLenum cap, bool state, bool* dirty)
{
    if (*dirty) {
        if (state) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
        *dirty = false;
    }
    return state;
}

static inline void update_gl_stencil_test_op(PGRAPHState* pg)
{
    if (pg->dirty.stencil_test_op) {
        glStencilOp(map_register_to_gl_stencil_op(
                        GET_PG_REG(pg, CONTROL_2, STENCIL_OP_FAIL)),
                    map_register_to_gl_stencil_op(
                        GET_PG_REG(pg, CONTROL_2, STENCIL_OP_ZFAIL)),
                    map_register_to_gl_stencil_op(
                        GET_PG_REG(pg, CONTROL_2, STENCIL_OP_ZPASS)));
        pg->dirty.stencil_test_op = false;
    }
}

static inline void update_gl_stencil_test_func(PGRAPHState* pg)
{
    if (pg->dirty.stencil_test_func) {
        glStencilFunc(map_register_to_gl_func(
                          GET_PG_REG(pg, CONTROL_1, STENCIL_FUNC)),
                      GET_PG_REG(pg, CONTROL_1, STENCIL_REF),
                      GET_PG_REG(pg, CONTROL_1, STENCIL_MASK_READ));
        pg->dirty.stencil_test_func = false;
    }
}

static inline void update_gl_alpha_test_func(PGRAPHState* pg)
{
    if (pg->dirty.alpha_test_func) {
        glAlphaFunc(map_register_to_gl_func(
                        GET_PG_REG(pg, CONTROL_0, ALPHAFUNC)),
                    GET_PG_REG(pg, CONTROL_0, ALPHAREF) / 255.0f);
        pg->dirty.alpha_test_func = false;
    }
}

static inline void update_gl_alpha_test(PGRAPHState* pg)
{
    if (update_gl_state(GL_ALPHA_TEST,
                        GET_PG_REG(pg, CONTROL_0, ALPHATESTENABLE),
                        &pg->dirty.alpha_test)) {
        update_gl_alpha_test_func(pg);
    }
}

static inline void update_gl_depth_test_func(PGRAPHState* pg)
{
    if (pg->dirty.depth_test_func) {
        glDepthFunc(map_register_to_gl_func(
                        GET_PG_REG(pg, CONTROL_0, ZFUNC)));
        pg->dirty.depth_test_func = false;
    }
}

static inline void update_gl_depth_test(PGRAPHState* pg)
{
    if (update_gl_state(GL_DEPTH_TEST,
                        GET_PG_REG(pg, CONTROL_0, ZENABLE),
                        &pg->dirty.depth_test)) {
        update_gl_depth_test_func(pg);
    }
}

static inline void update_gl_fog_color(PGRAPHState* pg)
{
    if (pg->dirty.fog_color) {
        GLfloat gl_color[] = {
            GET_PG_REG(pg, FOGCOLOR, RED) / 255.0f,
            GET_PG_REG(pg, FOGCOLOR, GREEN) / 255.0f,
            GET_PG_REG(pg, FOGCOLOR, BLUE) / 255.0f,
            GET_PG_REG(pg, FOGCOLOR, ALPHA) / 255.0f
        };
        glFogfv(GL_FOG_COLOR, gl_color);
        pg->dirty.fog_color = false;
    }
}

static inline void update_gl_fog_mode(PGRAPHState* pg)
{
    if (pg->dirty.fog_mode) {
        GLint gl_mode;
        switch(GET_PG_REG(pg, CONTROL_3, FOG_MODE)) {
        case NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR:
            gl_mode = GL_LINEAR;
            break;
        case NV_PGRAPH_CONTROL_3_FOG_MODE_EXP:
            gl_mode = GL_EXP;
            break;
        case NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2:
            gl_mode = GL_EXP2;
            break;
        case NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR_ABS:
        case NV_PGRAPH_CONTROL_3_FOG_MODE_EXP_ABS:
        case NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2_ABS:
            // EYE_PLANE_ABSOLUTE ? probaly should be done in the vertex-shader
            // https://www.opengl.org/registry/specs/NV/fog_distance.txt
            assert(0);
            break;
        default:
            assert(0);
        }
        glFogi(GL_FOG_MODE, gl_mode);
        pg->dirty.fog_mode = false;
    }
}

static inline void update_gl_fog(PGRAPHState* pg)
{
    if (update_gl_state(GL_FOG,
                        GET_PG_REG(pg, CONTROL_3, FOGENABLE),
                        &pg->dirty.fog)) {
        update_gl_fog_mode(pg);
        update_gl_fog_color(pg);
        glHint(GL_FOG_HINT, GL_NICEST); // Not sure if necessary, but we should attempt to get the best fog possible
    }
}

static inline void update_gl_color_mask(PGRAPHState* pg)
{
    if (pg->dirty.color_mask) {
        bool mask_alpha = GET_PG_REG(pg, CONTROL_0, ALPHA_WRITE_ENABLE);
        bool mask_red = GET_PG_REG(pg, CONTROL_0, RED_WRITE_ENABLE);
        bool mask_green = GET_PG_REG(pg, CONTROL_0, GREEN_WRITE_ENABLE);
        bool mask_blue = GET_PG_REG(pg, CONTROL_0, BLUE_WRITE_ENABLE);
        glColorMask(mask_red?GL_TRUE:GL_FALSE,
                    mask_green?GL_TRUE:GL_FALSE,
                    mask_blue?GL_TRUE:GL_FALSE,
                    mask_alpha?GL_TRUE:GL_FALSE);
        pg->dirty.color_mask = false;
    }
}

static inline void update_gl_stencil_mask(PGRAPHState* pg)
{
    if (pg->dirty.stencil_mask) {
        GLuint gl_mask = GET_PG_REG(pg, CONTROL_1, STENCIL_MASK_WRITE);
        glStencilMask(GET_PG_REG(pg, CONTROL_0, STENCIL_WRITE_ENABLE)?
                      gl_mask:0x00);
        pg->dirty.stencil_mask = false;
    }
}

static inline void update_gl_depth_mask(PGRAPHState* pg)
{
    if (pg->dirty.depth_mask) {
        glDepthMask(GET_PG_REG(pg, CONTROL_0, ZWRITEENABLE)?GL_TRUE:GL_FALSE);
        pg->dirty.depth_mask = false;
    }
}

static inline void update_gl_stencil_test(PGRAPHState* pg)
{
    if (update_gl_state(GL_STENCIL_TEST,
                        GET_PG_REG(pg, CONTROL_1, STENCIL_TEST_ENABLE),
                        &pg->dirty.stencil_test)) {
        update_gl_stencil_test_func(pg);
        update_gl_stencil_test_op(pg);
    }
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
    switch (obj->graphics_class) {
    case NV_KELVIN_PRIMITIVE:

        /* temp hack? */
        d->pgraph.vertex_attributes[NV2A_GPU_VERTEX_ATTR_DIFFUSE].inline_value = 0xFFFFFFF;

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

static void get_surface_dimensions(PGRAPHState* pg, unsigned int* draw_width, unsigned int* draw_height) {
    unsigned int w = pg->clip_width;
    unsigned int h = pg->clip_height;
    switch (pg->surface_anti_aliasing) {
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1:
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2:
        w *= 2;
        break;
    case NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_SQUARE_OFFSET_4:
        w *= 2;
        h *= 2;
        break;
    default:
        assert(0);
    }
    if (draw_width) { *draw_width = w; }
    if (draw_height) { *draw_height = h; }
}

static void pgraph_bind_converted_vertex_attributes(NV2A_GPUState *d,
                                                    bool inline_array,
                                                    unsigned int num_elements)
{
    int i, j;
    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &d->pgraph.vertex_attributes[i];
        if (attribute->count && attribute->needs_conversion) {

            uint8_t *data;
            if (inline_array) {
                data = (uint8_t*)d->pgraph.inline_array
                        + attribute->inline_array_offset;
            } else {
                hwaddr dma_len;
                if (attribute->dma_select) {
                    data = nv_dma_load_and_map(d, d->pgraph.dma_vertex_b, &dma_len);
                } else {
                    data = nv_dma_load_and_map(d, d->pgraph.dma_vertex_a, &dma_len);
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
                uint8_t *in = data + j * attribute->stride; //FIXME: This wouldn't work for inline_arrays!
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

static unsigned int pgraph_bind_inline_array(NV2A_GPUState *d)
{
    int i;
    unsigned int offset = 0;
    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &d->pgraph.vertex_attributes[i];
        if (attribute->count) {
            offset += attribute->size * attribute->count;
        }
    }
    unsigned int stride = offset;
    offset = 0;
    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &d->pgraph.vertex_attributes[i];
        if (attribute->count) {

            glEnableVertexAttribArray(i);

            attribute->inline_array_offset = offset;

            if (!attribute->needs_conversion) {
                glVertexAttribPointer(i,
                    attribute->gl_size,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    stride,
                    (uint8_t*)d->pgraph.inline_array + offset);
            }

            offset += attribute->size * attribute->count;
        }
    }
    return offset;
}

static void pgraph_bind_vertex_attributes(NV2A_GPUState *d)
{
    int i;

    debugger_push_group("NV2A: pgraph_bind_vertex_attributes");

    for (i=0; i<NV2A_GPU_VERTEXSHADER_ATTRIBUTES; i++) {
        VertexAttribute *attribute = &d->pgraph.vertex_attributes[i];
        debugger_push_group("VA %i: count=%d, stride=%i, kelvin_format=0x%x, needs_conversion=%d",i,attribute->count,attribute->stride,attribute->format,attribute->needs_conversion);
        if (attribute->count) {
            glEnableVertexAttribArray(i);

            if (!attribute->needs_conversion) {
                hwaddr dma_len;
                uint8_t *vertex_data;

                //TODO: Load (and even map..) DMA Object once and re-use later, however, we don't know if the object is valid so we must delay the load until we want to use it
                /* TODO: cache coherence */
                if (attribute->dma_select) {
                    vertex_data = nv_dma_load_and_map(d, d->pgraph.dma_vertex_b, &dma_len);
                } else {
                    vertex_data = nv_dma_load_and_map(d, d->pgraph.dma_vertex_a, &dma_len);
                }
                assert(attribute->offset < dma_len);
                vertex_data += attribute->offset;
                glVertexAttribPointer(i,
                    attribute->gl_size,
                    attribute->gl_type,
                    attribute->gl_normalize,
                    attribute->stride,
                    vertex_data);
                debugger_message("apitrace bug?"); /* glVertexAttribPointer not shown! */
            }
        } else {
            glDisableVertexAttribArray(i);

            glVertexAttrib4ubv(i, (GLubyte *)&attribute->inline_value);
        }
        debugger_pop_group();
    }

    debugger_pop_group();

}

static void pgraph_update_state(PGRAPHState* pg) {
    update_gl_depth_mask(pg);
    update_gl_stencil_mask(pg);
    update_gl_color_mask(pg);

    update_gl_alpha_test(pg);
    update_gl_depth_test(pg);
    update_gl_stencil_test(pg);
    update_gl_fog(pg);
}

static void pgraph_bind_textures(NV2A_GPUState *d)
{
    int i;

    debugger_push_group("NV2A: pgraph_bind_textures");

    for (i=0; i<NV2A_GPU_MAX_TEXTURES; i++) {

        Texture *texture = &d->pgraph.textures[i];

        if (texture->dimensionality != 2) continue;
        
        glActiveTexture(GL_TEXTURE0_ARB + i);
        if (texture->enabled) {
            
            assert(texture->color_format
                    < sizeof(kelvin_color_format_map)/sizeof(ColorFormatInfo));

            ColorFormatInfo f = kelvin_color_format_map[texture->color_format];
            if (f.bytes_per_pixel == 0) {

                printf("NV2A: unhandled texture->color_format 0x%x\n",
                                 texture->color_format);
                debugger_message("NV2A: unhandled texture->color_format 0x%x",
                                 texture->color_format);

#if 1
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

//FIXME: 3D textures plox!

            glBindTexture(gl_target, gl_texture);


            /* Load texture DMA object */
            hwaddr dma_len;
            uint8_t *texture_data;
            DMAObject dma = nv_dma_load(d, texture->dma_select?d->pgraph.dma_b:
                                                               d->pgraph.dma_a);

            /* Label the texture so we can find it in the debugger */
            debugger_label(GL_TEXTURE, gl_texture,
                           "NV2A: 0x%X: { "
                           "color_format: 0x%X; "
                           "pitch: %i }",
                           dma.address + texture->offset,
                           texture->color_format,
                           texture->pitch);

            memory_region_sync_dirty_bitmap(d->vram);
            bool resource_dirty = memory_region_get_dirty(d->vram,
                                      dma.address + texture->offset,
                                      texture->pitch * height,
                                      DIRTY_MEMORY_NV2A_GPU_RESOURCE);

            /* Texture state and the texture content didn't change? Abort! */
            if (!texture->dirty && !resource_dirty) {
                continue;
            }

            // FIXME: Really have to handle overlapping resources before claiming this is clean, check my wiki
            memory_region_reset_dirty(d->vram,
                                      dma.address + texture->offset,
                                      texture->pitch * height,
                                      DIRTY_MEMORY_NV2A_GPU_RESOURCE);


            /* Locate texture data */
            texture_data = nv_dma_map(d, &dma, &dma_len);
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

#ifdef DEBUG_NV2A_GPU_DISABLE_MIPMAP
                int level;
#if 0
//FIXME: This hack causes problems with compressed textures
         They might be broken with normal mipmapping too
                printf("MIPMAP Hack: %dx%d requested %d level(s)\n", width, height, levels);
                for (level = 0; (level < (levels-1)) && (level < 3); level++) {
                    texture_data += width * height * f.bytes_per_pixel;
                    width /= 2;
                    height /= 2;
                }              
                printf("MIPMAP Hack: %dx%d returned\n", width, height);
#endif
                levels = 1;
#else
                glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
                    texture->min_mipmap_level);
                glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
                    levels-1);

                int level;
#endif
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
                        unswizzle_rect(texture_data, width, height,
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
        assert(glGetError() == 0);

    }

    debugger_pop_group();

}

static guint hash(gconstpointer key, size_t size)
{
    /* 64 bit Fowler/Noll/Vo FNV-1a hash code */
    uint64_t hval = 0xcbf29ce484222325ULL;
    const uint8_t *bp = key;
    const uint8_t *be = key + size;
//FIXME: Search for end of shader
    while (bp < be) {
        hval ^= (uint64_t) *bp++;
        hval += (hval << 1) + (hval << 4) + (hval << 5) +
            (hval << 7) + (hval << 8) + (hval << 40);
    }

    return (guint)hval;
}

static guint shader_hash(gconstpointer key)
{
    return hash(key, sizeof(ShaderState));
}

static gboolean shader_equal(gconstpointer a, gconstpointer b)
{
    const ShaderState *as = a, *bs = b;
    return memcmp(as, bs, sizeof(ShaderState)) == 0;
}

//FIXME: Split this into vertex, fragment and program functions and make sure to only use information which also influences the hash!
static GLuint generate_shaders(PGRAPHState* pg, ShaderState state)
{
    int i;

    GLuint program = glCreateProgram();

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glAttachShader(program, vertex_shader);

    GLint compiled;

    const char *vertex_shader_code;
    QString *transform_program_code;

    //XXX: This is a loosy check for fixed function
    if (state.vertex_shader_hash == 0) {
        /* generate vertex shader mimicking fixed function */

        vertex_shader_code =
          "#version 110\n"
          "\n"
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
          "   gl_FogFragCoord = 0.5;\n" //FIXME!!! Probably generated from the vertex attrib?
          "   gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n"
          "   gl_FrontColor = diffuse;\n"
          "   gl_TexCoord[0] = textureMatrix0 * multiTexCoord0;\n"
          "   gl_TexCoord[1] = textureMatrix1 * multiTexCoord1;\n"
          "   gl_TexCoord[2] = textureMatrix2 * multiTexCoord2;\n"
          "   gl_TexCoord[3] = textureMatrix3 * multiTexCoord3;\n"
          "}\n";
    } else {
        VertexShader *shader = &pg->vertexshader;
        transform_program_code = vsh_translate(VSH_VERSION_XVS,
                                                        &shader->program_data[pg->vertexshader_start_slot*4],
                                                        (NV2A_GPU_MAX_VERTEXSHADER_LENGTH-pg->vertexshader_start_slot)*4);
        vertex_shader_code = qstring_get_str(transform_program_code);
    }

    glShaderSource(vertex_shader, 1, &vertex_shader_code, NULL);
    glCompileShader(vertex_shader);

    NV2A_GPU_DPRINTF("bind new vertex shader, code:\n%s\n", vertex_shader_code);

    /* Check it compiled */
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[GLSL_LOG_LENGTH];
        glGetShaderInfoLog(vertex_shader, GLSL_LOG_LENGTH, NULL, log);
        log[GLSL_LOG_LENGTH - 1] = '\0';
        fprintf(stderr, "\n\n%s\n", vertex_shader_code);
        fprintf(stderr, "nv2a: vertex shader compilation failed: %s\n", log);
        abort();
    }

    //XXX: Again: This is a check for fixed function..
    if (state.vertex_shader_hash == 0) {
        /* Bind attributes for fixed function pipeline */
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_POSITION, "position");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_DIFFUSE, "diffuse");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_SPECULAR, "specular");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_FOG, "fog");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE0, "multiTexCoord0");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE1, "multiTexCoord1");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE2, "multiTexCoord2");
        glBindAttribLocation(program, NV2A_GPU_VERTEX_ATTR_TEXTURE3, "multiTexCoord3");
    } else {
        /* Release shader string */
        QDECREF(transform_program_code);
        /* Bind attributes for transform program*/
        char tmp[4];
        for(i = 0; i < 16; i++) {
            sprintf(tmp,"v%d",i);
            glBindAttribLocation(program, i, tmp);
        }

#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
        debugger_prepare_feedback(program, 0xFFFF+1); // This should be enough?
#endif

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
                   state.rect_tex, state.compare_mode, state.alphakill);

    const char *fragment_shader_code_str = qstring_get_str(fragment_shader_code);

    NV2A_GPU_DPRINTF("bind new fragment shader, code:\n%s\n", fragment_shader_code_str);

    glShaderSource(fragment_shader, 1, &fragment_shader_code_str, NULL);
    glCompileShader(fragment_shader);

    /* Check it compiled */
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[GLSL_LOG_LENGTH];
        glGetShaderInfoLog(fragment_shader, GLSL_LOG_LENGTH, NULL, log);
        log[GLSL_LOG_LENGTH - 1] = '\0';
        fprintf(stderr, "\n\n%s\n", fragment_shader_code_str);
        fprintf(stderr, "nv2a: fragment shader compilation failed: %s\n", log);
        abort();
    }
    assert(glGetError() == GL_NO_ERROR);

    QDECREF(fragment_shader_code);



    /* link the program */
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLchar log[GLSL_LOG_LENGTH];
        glGetProgramInfoLog(program, GLSL_LOG_LENGTH, NULL, log);
        log[GLSL_LOG_LENGTH - 1] = '\0';
        fprintf(stderr, "nv2a: shader linking failed: %s\n", log);
        abort();
    }

    glUseProgram(program);
    assert(glGetError() == 0);

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
        GLchar log[GLSL_LOG_LENGTH];
        glGetProgramInfoLog(program, GLSL_LOG_LENGTH, NULL, log);
        log[GLSL_LOG_LENGTH - 1] = '\0';
        fprintf(stderr, "nv2a: shader validation failed: %s\n", log);
        abort();
    }
    assert(glGetError() == GL_NO_ERROR);

    return program;
}

static void pgraph_bind_shaders(PGRAPHState *pg)
{
    debugger_push_group("NV2A: pgraph_bind_shaders");
    int i;

    bool fixed_function = GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                   NV_PGRAPH_CSV0_D_MODE) == 0;

    if (pg->dirty.shaders) {

        /* Hash the vertex shader if it exists */
        guint vertex_shader_hash;
        if (fixed_function) {
            vertex_shader_hash = 0;
        } else {
            //FIXME: This will currently ignore the dirty bit..
            unsigned int start = pg->vertexshader_start_slot;
            uint32_t* transform_program = &pg->vertexshader.program_data[start * 4];
            /* Search for the final instruction */
            for(i = 0; i < (NV2A_GPU_MAX_VERTEXSHADER_LENGTH - start); i++) {
                if (transform_program[i * 4 + 3] & 1) { i++; break; }
            }
            vertex_shader_hash = hash(transform_program, i * 4);
        }

        ShaderState state = {
            /* register combier stuff */
            .combiner_control = pg->regs[NV_PGRAPH_COMBINECTL],
            .shader_stage_program = pg->regs[NV_PGRAPH_SHADERPROG],
            .other_stage_input = pg->regs[NV_PGRAPH_SHADERCTL],
            .final_inputs_0 = pg->regs[NV_PGRAPH_COMBINESPECFOG0],
            .final_inputs_1 = pg->regs[NV_PGRAPH_COMBINESPECFOG1],
            /* vertex shader stuff */
            .vertex_shader_hash = vertex_shader_hash
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
            int j;
            for(j = 0; j < 4; j++) {
                /* compare_mode[i][j] = stage i, component j { s,t,r,q } */
                state.compare_mode[i][j] = 
                    (pg->regs[NV_PGRAPH_SHADERCLIPMODE] >> (4 * i + j)) & 1;
            }
            state.alphakill[i] = pg->textures[i].alphakill;
        }

        /* if the shader changed / is dirty, dirty all the constants so they will be uploaded again */
        for (i=0; i<NV2A_GPU_VERTEXSHADER_CONSTANTS; i++) {
            pg->constants[i].dirty = true;
        }

#if 1
        //FIXME: Add vertex shader to cache! - we should probably cache the 3 of them individually (vertex, fragment, program)
        gpointer cached_shader = g_hash_table_lookup(pg->cache.shader, &state);
        if (cached_shader) {
            pg->gl_program = (GLuint)cached_shader;
        } else {
            pg->gl_program = generate_shaders(pg, state);

            /* cache it */
            
            ShaderState *cache_state = g_malloc(sizeof(*cache_state));
            memcpy(cache_state, &state, sizeof(*cache_state));
            g_hash_table_insert(pg->cache.shader, cache_state,
                                (gpointer)pg->gl_program);
        }
#else
        pg->gl_program = generate_shaders(pg, state);
#endif
        pg->dirty.shaders = false;
    }

    glUseProgram(pg->gl_program);
    assert(glGetError() == 0);


    debugger_push_group("NV2A: update combiner constants");

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

        /* Set eye vector for combiner */
        if (pg->dirty.eye_vector) {
            GLint loc = glGetUniformLocation(pg->gl_program, "eye_vector");
            if (loc != -1) {
                glUniform3fv(loc, 1, pg->eye_vector);
            }
            pg->dirty.eye_vector = false;
        }

    }

    debugger_pop_group();

    float zclip_max = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMAX];
    float zclip_min = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMIN];

    /* update fixed function uniforms */
    if (fixed_function) {


        /* FIXME FIXME FIXME


          - c[58] and c[59] used for viewport
          - c[0,1,2,3] used for composite matrix


        */

        assert(pg->FIXME_REMOVEME_comp_src == 2);

        GLint comMatLoc = glGetUniformLocation(pg->gl_program, "composite");
        glUniformMatrix4fv(comMatLoc, 1, GL_FALSE, pg->composite_matrix);

        /* FIXME: Disabling texture matrices should probably be done in the shader? */
        const float identity_matrix[4*4] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        GLint texMat0Loc = glGetUniformLocation(pg->gl_program, "textureMatrix0");
        glUniformMatrix4fv(texMat0Loc, 1, GL_FALSE, pg->texture_matrix_enable[0]?pg->texture_matrix0:identity_matrix);
        GLint texMat1Loc = glGetUniformLocation(pg->gl_program, "textureMatrix1");
        glUniformMatrix4fv(texMat1Loc, 1, GL_FALSE, pg->texture_matrix_enable[1]?pg->texture_matrix1:identity_matrix);
        GLint texMat2Loc = glGetUniformLocation(pg->gl_program, "textureMatrix2");
        glUniformMatrix4fv(texMat2Loc, 1, GL_FALSE, pg->texture_matrix_enable[2]?pg->texture_matrix2:identity_matrix);
        GLint texMat3Loc = glGetUniformLocation(pg->gl_program, "textureMatrix3");
        glUniformMatrix4fv(texMat3Loc, 1, GL_FALSE, pg->texture_matrix_enable[3]?pg->texture_matrix3:identity_matrix);

        /* estimate the viewport by assuming it matches the clip region ... */
#if 0
        float m11 = *(float*)&pg->constants[59].data[0];
        float m22 = *(float*)&pg->constants[59].data[1];

        float invViewport[16] = {
            2.0/w, 0.0,   0.0,        0.0,
            0.0,   2.0/h, 0.0,        0.0,
            0.0,   0.0,  -2.0/zrange, 0.0,
            0.0,   0.0,  -m43/m33,    1.0
        };

#else



float w = *(float*)&pg->constants[59].data[0];
w = pg->clip_width;
float h = *(float*)&pg->constants[59].data[1];
h = pg->clip_height;

/*
        unsigned int w;
        unsigned int h;
        get_surface_dimensions(pg, &w, &h);
*/
//FIXME: Probably do this elsewhere..?



        float m11 = 0.5 * w;
        float m22 = -0.5 * h;
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
#endif
        GLint viewLoc = glGetUniformLocation(pg->gl_program, "invViewport");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &invViewport[0]);

    } else {

        //FIXME: Uniforms are *PER SHADER PROGRAM!* - Our cache / dirty system has to represent that..

        /* load constants */
        for (i=0; i<NV2A_GPU_VERTEXSHADER_CONSTANTS; i++) {
            VertexShaderConstant *constant = &pg->constants[i];
            //if (!constant->dirty) continue;

            char tmp[7];
            sprintf(tmp,"c[%d]",i);
            GLint loc = glGetUniformLocation(pg->gl_program, tmp);
            if (loc != -1) {
                glUniform4fv(loc, 1, (const GLfloat*)constant->data);
                //constant->dirty = false;
            }
        }

        GLint loc = glGetUniformLocation(pg->gl_program, "cliprange");
        glUniform2f(loc, zclip_min, zclip_max);
//        glDepthRangef();

    }

    debugger_pop_group();
}

static uint8_t* map_surface(NV2A_GPUState *d, Surface* s, DMAObject* dma, hwaddr* len)
{

    /* There's a bunch of bugs that could cause us to hit this function
     * at the wrong time and get a invalid dma object.
     * Check that it's sane. */
    assert(dma->dma_class == NV_DMA_IN_MEMORY_CLASS);
printf("0x%x+0x%x\n",dma->address,s->offset);
//    assert(dma->address + s->offset != 0);
    assert(s->offset <= dma->limit);
    assert(s->offset
            + s->pitch * d->pgraph.clip_height
                <= dma->limit + 1);

    return nv_dma_map(d, dma, len);

}

//FIXME: All of those don't respect a clip offset
//FIXME: Making these regions later shouldn't have a bad effect (except for more useless updates) - but it does crash
static void mark_cpu_surface_dirty(NV2A_GPUState *d, Surface* s, DMAObject* dma) {
    memory_region_set_dirty(d->vram, dma->address + s->offset,
                                     s->pitch * d->pgraph.clip_height); //FIXME: AA
}

static void mark_cpu_surface_clean(NV2A_GPUState *d, Surface* s, DMAObject* dma, unsigned client) {
    memory_region_reset_dirty(d->vram, dma->address + s->offset,
                                       s->pitch * d->pgraph.clip_height, //FIXME: AA
                                       client);
}

static bool is_cpu_surface_dirty(NV2A_GPUState *d, Surface* s, DMAObject* dma, unsigned client) {
    return memory_region_get_dirty(d->vram, dma->address + s->offset,
                                            s->pitch * d->pgraph.clip_height, //FIXME: AA
                                            client);
}

static void perform_surface_update(NV2A_GPUState *d, Surface* s, DMAObject* dma, bool upload, uint8_t* data, GLenum gl_format, GLenum gl_type, unsigned int bytes_per_pixel) {

    /* Make sure we are working with a clean context */
    assert(glGetError() == GL_NO_ERROR);

#ifdef FORCE_NOXY
    CHECKXY(&d->pgraph)
#else
    /* TODO */
    assert(d->pgraph.clip_x == 0 && d->pgraph.clip_y == 0);
#endif

    bool swizzle = (d->pgraph.surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);

    unsigned int w;
    unsigned int h;
    get_surface_dimensions(&d->pgraph, &w, &h);
    assert(w <= (s->pitch/bytes_per_pixel));

    void* gpu_buffer = data + s->offset;
    void* tmp_buffer;
    if (swizzle) {
        tmp_buffer = malloc(s->pitch * h);
    } else {
        tmp_buffer = NULL;
    }

    if (upload) {
        /* surface modified (or moved) by the cpu.
         * copy it into the opengl renderbuffer */
 
        assert(s->pitch % bytes_per_pixel == 0);

        glUseProgram(0);

        int rl, pa;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rl);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &pa);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s->pitch / bytes_per_pixel);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        /* glDrawPixels is crazy deprecated, but there really isn't
         * an easy alternative */

        // If the surface is swizzled we have to unswizzle the CPU data before uploading them
        if (swizzle) {
            unswizzle_rect(gpu_buffer,
                           1 << d->pgraph.surface_swizzle_width_shift,
                           1 << d->pgraph.surface_swizzle_height_shift,
                           tmp_buffer,
                           s->pitch,
                           bytes_per_pixel);
        }

#if 0
        /* FIXME: Also do this if we the CPU wasn't dirty! */
        /* Attempt to keep the unused buffer area clean */
        if (gl_format == GL_DEPTH_STENCIL) {
            glClearStencil(0x777777);
            glClear(GL_STENCIL_BUFFER_BIT);
        }
        if ((gl_format == GL_DEPTH_COMPONENT) || (gl_format == GL_DEPTH_STENCIL)) {
            glClearDepth(0.5f);
            glClear(GL_DEPTH_BUFFER_BIT);
        } else {
            glClearColor(1.0f,0.0f,1.0f,0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
#endif

        glWindowPos2i(0, d->pgraph.clip_height);
        glPixelZoom(1, -1);
        glDrawPixels(w,
                     h,
                     gl_format, gl_type,
                     swizzle ? tmp_buffer : gpu_buffer);

        assert(glGetError() == GL_NO_ERROR);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, rl);
        glPixelStorei(GL_UNPACK_ALIGNMENT, pa);

    } else {

        /* read the opengl renderbuffer into the surface */

        // Make sure we don't overwrite random CPU data between rows
        if (swizzle) {
            memcpy(tmp_buffer,gpu_buffer,s->pitch*h);
        }

        glo_readpixels(gl_format, gl_type,
                       bytes_per_pixel, s->pitch,
                       w, h,
                       swizzle ? tmp_buffer : gpu_buffer);
        assert(glGetError() == GL_NO_ERROR);

        /* When reading we have to swizzle the data for the CPU */
        if (swizzle) {
            swizzle_rect(tmp_buffer,
                         1 << d->pgraph.surface_swizzle_width_shift,
                         1 << d->pgraph.surface_swizzle_height_shift,
                         gpu_buffer,
                         s->pitch,
                         bytes_per_pixel);
        }

    }

    if (swizzle) {
        free(tmp_buffer);
    }

    uint8_t *out = data + s->offset;
    NV2A_GPU_DPRINTF("Surface %s 0x%llx - 0x%llx, "
                  "(0x%llx - 0x%llx, %d %d, %d %d, %d) - %x %x %x %x\n",
        upload?"upload (CPU->GPU)":"download (GPU->CPU)",
        dma->address, dma->address + dma->limit,
        dma->address + s->offset,
        dma->address + s->offset + s->pitch * d->pgraph.clip_height * 2,
        d->pgraph.clip_x, d->pgraph.clip_y,
        d->pgraph.clip_width, d->pgraph.clip_height,
        s->pitch,
        out[0], out[1], out[2], out[3]);

}

/* DO NOT USE THIS FUNCTION DIRECTLY! Use pgraph_update_surfaces instead! */
static void pgraph_update_surface_zeta(NV2A_GPUState *d, bool upload)
{

    DMAObject zeta_dma;

    /* Early out if we have no surface */
    Surface* s = &d->pgraph.surface_zeta;
    if (s->format == 0) { return; }

    /* Check dirty flags, load DMA object if necessary */
    if (upload) {
#ifdef CACHE_DMA
        zeta_dma = d->pgraph.dma_object_zeta;
#else
        zeta_dma = nv_dma_load(d, d->pgraph.dma_zeta);
#endif
        /* If the surface was not written by the CPU we don't have to upload */
        if (!is_cpu_surface_dirty(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA)) { return; }
        /* Assertion to make sure we don't overwrite GPU results */
        assert(s->draw_dirty == false);
    } else {
        /* If we didn't draw on it we don't have to redownload */
        if (!s->draw_dirty) { return; }
#ifdef CACHE_DMA
        zeta_dma = d->pgraph.dma_object_zeta;
#else
        zeta_dma = nv_dma_load(d, d->pgraph.dma_zeta);
#endif
        /* Assertion to make sure we don't overwrite CPU results */
        assert(is_cpu_surface_dirty(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA) == false);
    }

    /* Construct the GL format */
    //FIXME: Merge with surface framebuffer creation to one surface format table
    bool has_stencil;
    GLenum gl_format;
    GLenum gl_type;
    unsigned int bytes_per_pixel;
    switch (s->format) {
    case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
        has_stencil = false;
        bytes_per_pixel = 2;
        gl_format = GL_DEPTH_COMPONENT;
        gl_type = GL_UNSIGNED_SHORT;
        break;
    case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
        has_stencil = true;
        bytes_per_pixel = 4;
        gl_format = GL_DEPTH_STENCIL;
        gl_type = GL_UNSIGNED_INT_24_8; /* FIXME: Must be handled using a converter? Actually want 24 bit integer! */
        break;
    default:
        assert(false);
    }

    /* Map surface into memory */
    hwaddr zeta_len;
    uint8_t *zeta_data = map_surface(d, s, &zeta_dma, &zeta_len);

    /* Allow zeta access, then perform the zeta upload or download */
    if (upload) {
        glDepthMask(GL_TRUE);
        d->pgraph.dirty.depth_mask = true;
        if (has_stencil) {
            glStencilMask(0xFF);
            d->pgraph.dirty.stencil_mask = true;
        }
    }
    perform_surface_update(d, s, &zeta_dma, upload, zeta_data, gl_format, gl_type, bytes_per_pixel);

    /* Update dirty flags */
    if (!upload) {
        /* Surface downloaded. Handlers (VGA etc.) need to update */
        mark_cpu_surface_dirty(d, s, &zeta_dma);
        assert(is_cpu_surface_dirty(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_RESOURCE));
        /* We haven't drawn to this again yet, we just downloaded it*/
        s->draw_dirty = false;
    }
    /* Mark it as clean only for us, so changes dirty it again */
    mark_cpu_surface_clean(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA);
    assert(!is_cpu_surface_dirty(d, s, &zeta_dma, DIRTY_MEMORY_NV2A_GPU_ZETA));

}

/* DO NOT USE THIS FUNCTION DIRECTLY! Use pgraph_update_surfaces instead! */
static void pgraph_update_surface_color(NV2A_GPUState *d, bool upload)
{

    DMAObject color_dma;

    /* Early out if we have no surface */
    Surface* s = &d->pgraph.surface_color;
    if (s->format == 0) { return; }

    /* Check dirty flags, load DMA object if necessary */
    if (upload) {
#ifdef CACHE_DMA
        color_dma = d->pgraph.dma_object_color;
#else
        color_dma = nv_dma_load(d, d->pgraph.dma_color);
#endif
        /* If the surface was not written by the CPU we don't have to upload */
        if (!is_cpu_surface_dirty(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR)) { return; }
        /* Assertion to make sure we don't overwrite GPU results */
        assert(s->draw_dirty == false);
    } else {
        /* If we didn't draw on it we don't have to redownload */
        if (!s->draw_dirty) { return; }
#ifdef CACHE_DMA
        color_dma = d->pgraph.dma_object_color;
#else
        color_dma = nv_dma_load(d, d->pgraph.dma_color);
#endif
        /* Assertion to make sure we don't overwrite CPU results */
        assert(is_cpu_surface_dirty(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR) == false);
    }

    /* Construct the GL format */
    //FIXME: Merge with surface framebuffer creation to one surface format table
    GLenum gl_format;
    GLenum gl_type;
    unsigned int bytes_per_pixel;
    switch (s->format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
        bytes_per_pixel = 2;
        gl_format = GL_RGB;
        gl_type = GL_UNSIGNED_SHORT_5_6_5;
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

    /* Allow color access, then perform the color upload or download */
    if (upload) {
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        d->pgraph.dirty.color_mask = true;
    }
    perform_surface_update(d, s, &color_dma, upload, color_data, gl_format, gl_type, bytes_per_pixel);

    /* Update dirty flags */
    if (!upload) {
        /* Surface downloaded. Handlers (VGA etc.) need to update */
        mark_cpu_surface_dirty(d, s, &color_dma);
        assert(is_cpu_surface_dirty(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_RESOURCE));
        /* We haven't drawn to this again yet, we just downloaded it*/
        s->draw_dirty = false;
    }
    /* Mark it as clean only for us, so changes dirty it again */
    mark_cpu_surface_clean(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR);
    assert(!is_cpu_surface_dirty(d, s, &color_dma, DIRTY_MEMORY_NV2A_GPU_COLOR));

}


static void bind_gl_framebuffer(PGRAPHState* pg) {

    if (pg->dirty.framebuffer) {
    
        /* Get surface dimensions and make sure it can exist */
        unsigned int w;
        unsigned int h;
        get_surface_dimensions(pg, &w, &h);
        if ((w == 0) || (h == 0)) {
            debugger_message("No surfaces bound: width or height 0!");
            pg->dirty.framebuffer = false;
            return;
        }

        /* Map zeta format */
        //FIXME: Merge with stuff for surface reads to one surface format table
        bool has_depth;
        bool has_stencil;
        GLenum zeta_format;
        switch(pg->surface_zeta.format) {
        case 0:
            has_depth = false;
            has_stencil = false;
        case NV097_SET_SURFACE_FORMAT_ZETA_Z16:
            has_depth = true;
            has_stencil = false;
            zeta_format = GL_DEPTH_COMPONENT16;
            break;
        case NV097_SET_SURFACE_FORMAT_ZETA_Z24S8:
            has_depth = true;
            has_stencil = true;
            zeta_format = GL_DEPTH24_STENCIL8_EXT;
            break;
        default:
            assert(0);
        }

        /* Map color format */
        //FIXME: Merge with stuff for surface reads to one surface format table
        bool has_color;
        GLenum color_format;
        switch(pg->surface_color.format) {
        case 0:
            has_color = false;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
            has_color = true;
            color_format = GL_RGB565;
            break;
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
        case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
            has_color = true;
            color_format = GL_RGBA8;
            break;
        default:
            assert(0);
        }

        if (!has_depth && !has_stencil && !has_color) {
            debugger_message("No surfaces bound: no zeta, no color!");
            pg->dirty.framebuffer = false;
            assert(0); //FIXME: Untested code path!
            return;
        }

        debugger_push_group("bind_gl_framebuffer %dx%d (depth: %i, stencil: %i, color: %i)", w, h, has_depth, has_stencil, has_color);

        /* Zeta renderbuffer */
        Renderbuffer* zeta_renderbuffer;
        if (has_depth || has_stencil) {
            zeta_renderbuffer = bind_renderbuffer(pg, w, h, zeta_format);
            debugger_label(GL_RENDERBUFFER, zeta_renderbuffer->renderbuffer, "FIXME: ZETA %i",
    /*
                           "NV2A: 0x%X: { "
                           "format: 0x%X; "
                           "swizzle: %i (%ix%i); "
                           "pitch: %i; "
                           "clip: %ix%i; "
                           "AA: %i }",
                           color_dma.address + s->offset,
                           s->format,
                           pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE,
                           1 << d->pgraph.surface_swizzle_width_shift,
                           1 << d->pgraph.surface_swizzle_height_shift,
                           s->pitch,
                           pg->clip_width, pg->clip_height,
    */
                           pg->surface_anti_aliasing);
        }

        /* Color renderbuffer */
        Renderbuffer* color_renderbuffer;
        if (has_color) {
            color_renderbuffer = bind_renderbuffer(pg, w, h, color_format);
            debugger_label(GL_RENDERBUFFER, color_renderbuffer->renderbuffer, "FIXME: COLOR");
        }

        /* Framebuffer */
        Framebuffer* framebuffer = bind_framebuffer(pg,
                                       has_depth?
                                           zeta_renderbuffer->renderbuffer:
                                           0,
                                       has_stencil?
                                           zeta_renderbuffer->renderbuffer:
                                           0,
                                       has_color?
                                           color_renderbuffer->renderbuffer:
                                           0);

        debugger_pop_group();

        /* Mark the surface as dirty so it will be reuploaded */
        //FIXME FIXME FIXME FIXME FIXME!!!

        pg->dirty.framebuffer = false;
    }

}

static inline void pgraph_update_surfaces(NV2A_GPUState *d, bool upload, bool zeta, bool color)
{

// FIXME: This should probably be moved to the caller of this function or passed down the very bottom and returned as boolean..
    /* Early out if we have nothing to do */
    if (!zeta && !color) {
        return;
    }

//FIXME: Logic to think about: when exactly do we have to switch? only on uploads?
// Is it okay if we don't bind unless a write happens?
// ...
/* Switch to the current surface */
if (upload) {
    bind_gl_framebuffer(&d->pgraph);
}


    /* Assert to make sure we don't upload to the wrong surface */
    assert(!(upload && d->pgraph.dirty.framebuffer));

    debugger_push_group("pgraph_update_surfaces(upload: %i, zeta: %i, color: %i)",upload,zeta,color);

    /* Sync dirty bits, then update surfaces [and sync dirty bits again(?)] */
    memory_region_sync_dirty_bitmap(d->vram);
    if (zeta) {
      pgraph_update_surface_zeta(d, upload);
    }
    if (color) {
      pgraph_update_surface_color(d, upload);
    }
    debugger_pop_group();
//    memory_region_sync_dirty_bitmap(d->vram); //FIXME: Is this necessary?

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

    debugger_push_group("NV2A: pgraph_init");

    /* Check context capabilities */

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_texture_compression_s3tc"));

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_framebuffer_object"));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_texture_rectangle"));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_vertex_array_bgra"));

    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_packed_depth_stencil"));

#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
    assert(glo_check_extension((const GLubyte *)
                             "GL_EXT_transform_feedback"));
#endif

#ifdef LP
    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_color_buffer_float"));

    assert(glo_check_extension((const GLubyte *)
                             "GL_ARB_texture_float"));

    test_launchprogram();
#endif

    GLint max_vertex_attributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attributes);
    assert(max_vertex_attributes >= NV2A_GPU_VERTEXSHADER_ATTRIBUTES);

    //glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

    pg->dirty.shaders = true;

    /* generate textures */
    debugger_push_group("NV2A: generate textures");
    for (i = 0; i < NV2A_GPU_MAX_TEXTURES; i++) {
        Texture *texture = &pg->textures[i];
        glGenTextures(1, &texture->gl_texture);
        glGenTextures(1, &texture->gl_texture_rect);
    }
    debugger_pop_group();

    //FIXME: Move to cache init routine
    pg->cache.shader = g_hash_table_new(shader_hash, shader_equal);
    pg->cache.renderbuffer = g_hash_table_new(renderbuffer_hash, renderbuffer_equal);
    pg->cache.framebuffer = g_hash_table_new(framebuffer_hash, framebuffer_equal);
    //pgraph_cache_init(pg); ?

    assert(glGetError() == GL_NO_ERROR);

    debugger_pop_group();

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

    debugger_push_group("NV2A: pgraph_destroy");

//FIXME: Go through list of framebuffers for cleanup
#if 0
    glDeleteRenderbuffersEXT(1, &pg->gl_color_renderbuffer);
    glDeleteRenderbuffersEXT(1, &pg->gl_zeta_renderbuffer);
    glDeleteFramebuffersEXT(1, &pg->gl_framebuffer);
#endif

    for (i = 0; i < NV2A_GPU_MAX_TEXTURES; i++) {
        Texture *texture = &pg->textures[i];
        glDeleteTextures(1, &texture->gl_texture);
        glDeleteTextures(1, &texture->gl_texture_rect);
    }

    debugger_pop_group();

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

pgraph_update_surfaces(d,false,true,true);

        /* I guess this kicks it off? */
        if (image_blit->operation == NV09F_SET_OPERATION_SRCCOPY) {
            debugger_push_group("NV09F_SET_OPERATION_SRCCOPY");
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
            debugger_pop_group();
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
//FIXME: Disabled for testing            assert(!(pg->pending_interrupts & NV_PGRAPH_INTR_NOTIFY));

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
        pgraph_update_surfaces(d, false, true, true);
        break;

    case NV097_FLIP_INCREMENT_WRITE:
        pgraph_update_surfaces(d, false, true, true);
        //FIXME!
        break;

    case NV097_FLIP_STALL:
        pgraph_update_surfaces(d, false, true, true);

        /* Tell the debugger that the frame was completed */
        //FIXME: Figure out if this is a good position, we really have to figure out what a frame is:
        //       - When a pull from the VGA controller happens?
        //       - Result at vblank?
        //       - When D3D completes a frame?
        //       - ...
        debugger_finish_frame();
#if 1 //HACK: Set to 0 for AntiAlias or SetBackBuffer code
        qemu_mutex_unlock(&pg->lock);
        qemu_sem_wait(&pg->read_3d);
        qemu_mutex_lock(&pg->lock);
#endif
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
        pgraph_update_surfaces(d, false, false, true);

        pg->dma_color = parameter;
#ifdef CACHE_DMA
        pg->dma_object_color = nv_dma_load(d, d->pgraph.dma_color);
#endif
printf("Set color dma object at 0x%x\n",parameter);
        break;
    case NV097_SET_CONTEXT_DMA_ZETA:
        /* try to get any straggling draws in before the surface's changed :/ */
        pgraph_update_surfaces(d, false, true, false);

        pg->dma_zeta = parameter;
#ifdef CACHE_DMA
        pg->dma_object_zeta = nv_dma_load(d, d->pgraph.dma_zeta);
#endif
printf("Set zeta dma object at 0x%x\n",parameter);
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_A:
        pg->dma_vertex_a = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_VERTEX_B:
        pg->dma_vertex_b = parameter;
        break;
    case NV097_SET_CONTEXT_DMA_SEMAPHORE:
        kelvin->dma_semaphore = parameter;
        break;

    case NV097_SET_SURFACE_CLIP_HORIZONTAL:
        pgraph_update_surfaces(d, false, true, true);

        pg->clip_x =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_X);
        pg->clip_width =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_HORIZONTAL_WIDTH);

        pg->dirty.framebuffer = true;
        break;
    case NV097_SET_SURFACE_CLIP_VERTICAL:
        pgraph_update_surfaces(d, false, true, true);

        pg->clip_y =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_Y);
        pg->clip_height =
            GET_MASK(parameter, NV097_SET_SURFACE_CLIP_VERTICAL_HEIGHT);

        pg->dirty.framebuffer = true;
        break;
    case NV097_SET_SURFACE_FORMAT:
        pgraph_update_surfaces(d, false, true, true);

        pg->surface_color.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_COLOR);
        pg->surface_zeta.format =
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ZETA);
        pg->surface_type = 
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_TYPE);
        pg->surface_anti_aliasing = 
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_ANTI_ALIASING);
        pg->surface_swizzle_width_shift = 
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_WIDTH);
        pg->surface_swizzle_height_shift = 
            GET_MASK(parameter, NV097_SET_SURFACE_FORMAT_HEIGHT);

        pg->dirty.framebuffer = true;
        break;
    case NV097_SET_SURFACE_PITCH:
        pgraph_update_surfaces(d, false, true, true);

        pg->surface_color.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_COLOR);
        pg->surface_zeta.pitch =
            GET_MASK(parameter, NV097_SET_SURFACE_PITCH_ZETA);
        break;
    case NV097_SET_SURFACE_COLOR_OFFSET:
        pgraph_update_surfaces(d, false, false, true);
        debugger_message("NV2A: Changed COLOR_OFFSET to 0x%x\n",parameter);
        printf("Modified color offset to 0x%x\n",parameter);
        pg->surface_color.offset = parameter;
        break;
    case NV097_SET_SURFACE_ZETA_OFFSET:
        pgraph_update_surfaces(d, false, true, false);
        debugger_message("NV2A: Changed ZETA_OFFSET to 0x%x\n",parameter);
        printf("Modified zeta offset to 0x%x\n",parameter);
        pg->surface_zeta.offset = parameter;
        break;

    case NV097_SET_COMBINER_ALPHA_ICW ...
            NV097_SET_COMBINER_ALPHA_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAI0 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW0:
        pg->regs[NV_PGRAPH_COMBINESPECFOG0] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_SPECULAR_FOG_CW1:
        pg->regs[NV_PGRAPH_COMBINESPECFOG1] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_STENCIL_TEST_ENABLE:
        SET_PG_REG(pg, CONTROL_1, STENCIL_TEST_ENABLE, parameter);
        pg->dirty.stencil_test = true;
        break;

    case NV097_SET_STENCIL_OP_FAIL:
        SET_PG_REG(pg, CONTROL_2, STENCIL_OP_FAIL,
                   map_method_to_register_stencil_op(parameter));
        pg->dirty.stencil_test_op = true;
        break;
    case NV097_SET_STENCIL_OP_ZFAIL:
        SET_PG_REG(pg, CONTROL_2, STENCIL_OP_ZFAIL,
                   map_method_to_register_stencil_op(parameter));
        pg->dirty.stencil_test_op = true;
        break;
    case NV097_SET_STENCIL_OP_ZPASS:
        SET_PG_REG(pg, CONTROL_2, STENCIL_OP_ZPASS,
                   map_method_to_register_stencil_op(parameter));
        pg->dirty.stencil_test_op = true; 
        break;

    case NV097_SET_STENCIL_FUNC_REF:
        SET_PG_REG(pg, CONTROL_1, STENCIL_REF, parameter);
        pg->dirty.stencil_test_func = true;
        break;
    case NV097_SET_STENCIL_FUNC_MASK:
        SET_PG_REG(pg, CONTROL_1, STENCIL_MASK_READ, parameter);
        pg->dirty.stencil_test_func = true;
        break;
    case NV097_SET_STENCIL_FUNC:
        SET_PG_REG(pg, CONTROL_1, STENCIL_FUNC,
                   map_method_to_register_func(parameter));
        pg->dirty.stencil_test_func = true;
        break;
    case NV097_SET_COLOR_MASK:
        SET_PG_REG(pg, CONTROL_0, ALPHA_WRITE_ENABLE,
                   GET_MASK(parameter, NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE));
        SET_PG_REG(pg, CONTROL_0, RED_WRITE_ENABLE,
                   GET_MASK(parameter, NV097_SET_COLOR_MASK_RED_WRITE_ENABLE));
        SET_PG_REG(pg, CONTROL_0, GREEN_WRITE_ENABLE,
                   GET_MASK(parameter, NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE));
        SET_PG_REG(pg, CONTROL_0, BLUE_WRITE_ENABLE,
                   GET_MASK(parameter, NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE));
        pg->dirty.color_mask = true;
        break;
    case NV097_SET_STENCIL_MASK:
        SET_PG_REG(pg, CONTROL_1, STENCIL_MASK_WRITE, parameter);
        pg->dirty.stencil_mask = true;
        break;

    case NV097_SET_CONTROL0:
        SET_PG_REG(pg, CONTROL_0, CSCONVERT,
                   GET_MASK(parameter, NV097_SET_CONTROL0_COLOR_SPACE_CONVERT)); //XXX: Not emulated
        SET_PG_REG(pg, CONTROL_3, PREMULTALPHA,
                   GET_MASK(parameter, NV097_SET_CONTROL0_PREMULTIPLIEDALPHA)); //XXX: Not emulated
        SET_PG_REG(pg, CONTROL_3, TEXTUREPERSPECTIVE,
                   GET_MASK(parameter, NV097_SET_CONTROL0_TEXTUREPERSPECTIVE)); //XXX: Not emulated
        SET_PG_REG(pg, CONTROL_0, Z_PERSPECTIVE_ENABLE,
                   GET_MASK(parameter, NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE)); //XXX: Not emulated
        SET_PG_REG(pg, SETUPRASTER, Z_FORMAT,
                   GET_MASK(parameter, NV097_SET_CONTROL0_Z_FORMAT)); //XXX: Not emulated
        SET_PG_REG(pg, CONTROL_0, STENCIL_WRITE_ENABLE,
                   GET_MASK(parameter, NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE));
        /* We use the stencil mask to turn off the stencil writes */
        pg->dirty.stencil_mask = true;
        break;
    case NV097_SET_DEPTH_MASK:
        SET_PG_REG(pg, CONTROL_0, ZWRITEENABLE, parameter);
        pg->dirty.depth_mask = true;
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
        pg->FIXME_REMOVEME_comp_src = 2;
        break;

    CASE_4(NV097_SET_TEXTURE_MATRIX_ENABLE, 4):
        slot = (class_method - NV097_SET_TEXTURE_MATRIX_ENABLE) / 4;
        pg->texture_matrix_enable[slot] = parameter;
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

        //FIXME: Why is this happening?! Is it documented anywhere?
        pg->constants[59].data[slot] = parameter;
        pg->constants[59].dirty = true;
        break;

    case NV097_SET_COMBINER_FACTOR0 ...
            NV097_SET_COMBINER_FACTOR0 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR0) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR0 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_FACTOR1 ...
            NV097_SET_COMBINER_FACTOR1 + 28:
        slot = (class_method - NV097_SET_COMBINER_FACTOR1) / 4;
        pg->regs[NV_PGRAPH_COMBINEFACTOR1 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_ALPHA_OCW ...
            NV097_SET_COMBINER_ALPHA_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_ALPHA_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINEALPHAO0 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_COLOR_ICW ...
            NV097_SET_COMBINER_COLOR_ICW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_ICW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORI0 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_VIEWPORT_SCALE ...
            NV097_SET_VIEWPORT_SCALE + 12:

        slot = (class_method - NV097_SET_VIEWPORT_SCALE) / 4;
        //FIXME: Why is this happening? Same as NV097_SET_VIEWPORT_OFFSET!
        pg->constants[58].data[slot] = parameter;
        pg->constants[58].dirty = true;
        break;

    CASE_RANGE(NV097_SET_TRANSFORM_PROGRAM,32) {

        assert(pg->vertexshader_load_slot < (NV2A_GPU_MAX_VERTEXSHADER_LENGTH*4));
        vertexshader = &pg->vertexshader;
        //printf("Load prog slot %i (+%i?), 0x%08X\n",pg->vertexshader_load_slot,slot,parameter);
        vertexshader->program_data[pg->vertexshader_load_slot++] = parameter;
        vertexshader->dirty = true;
        break;
    }
    CASE_RANGE(NV097_SET_TRANSFORM_CONSTANT,32) {

        //printf("Setting c[%i].%i (c%i) = %f\n",pg->constant_load_slot/4,pg->constant_load_slot%4,pg->constant_load_slot/4-96,*(float*)&parameter);
        assert((pg->constant_load_slot/4) < NV2A_GPU_VERTEXSHADER_CONSTANTS);
        constant = &pg->constants[pg->constant_load_slot/4];
        if ((pg->constant_load_slot/4) == 58) {
            printf("SET_TRANSFORM_CONSTANT viewport_scale\n");
        }
        constant->data[pg->constant_load_slot%4] = parameter;
        pg->constant_load_slot++;
        constant->dirty = true;

        pg->FIXME_REMOVEME_comp_src = 1;
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX3F,3) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* vertex = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_POSITION];
        vertex[slot] = *(float*)&parameter;
        if (slot == 2) {
            pg->inline_buffer_length++;
            assert(pg->inline_buffer_length < NV2A_GPU_MAX_BATCH_LENGTH);
        }
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* vertex = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_POSITION];
        vertex[slot] = *(float*)&parameter;
        if (slot == 3) {
            pg->inline_buffer_length++;
            assert(pg->inline_buffer_length < NV2A_GPU_MAX_BATCH_LENGTH);
        }
        break;
    }
    CASE_RANGE(NV097_SET_NORMAL3F,3) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* normal = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_NORMAL];
        normal[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_DIFFUSE_COLOR4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* diffuse_color = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_DIFFUSE];
        diffuse_color[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_DIFFUSE_COLOR3F,3) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* diffuse_color = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_DIFFUSE];
        diffuse_color[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_DIFFUSE_COLOR4UB,1) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        uint32_t* diffuse_color = (uint32_t*)entry->v[NV2A_GPU_VERTEX_ATTR_DIFFUSE];
        diffuse_color[0] = parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD0_2F,2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord0 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE0];
        texcoord0[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD0_4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord0 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE0];
        texcoord0[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD1_2F,2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord1 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE1];
        texcoord1[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD1_4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord1 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE1];
        texcoord1[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD2_2F,2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord2 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE2];
        texcoord2[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD2_4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord2 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE2];
        texcoord2[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD3_2F,2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord3 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE3];
        texcoord3[slot] = *(float*)&parameter;
        break;
    }
    CASE_RANGE(NV097_SET_TEXCOORD3_4F,4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* texcoord3 = (float*)entry->v[NV2A_GPU_VERTEX_ATTR_TEXTURE3];
        texcoord3[slot] = *(float*)&parameter;
        break;
    }

    CASE_RANGE(NV097_SET_VERTEX_DATA_ARRAY_FORMAT,16) {

        vertex_attribute = &pg->vertex_attributes[slot];

        vertex_attribute->format =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE);
        vertex_attribute->count =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE);
        vertex_attribute->stride =
            GET_MASK(parameter, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE);

        debugger_message("Setting attributes for %d, count=%d, stride=%d, format=0x%x",slot,vertex_attribute->count,vertex_attribute->stride,vertex_attribute->format);

        vertex_attribute->gl_size = vertex_attribute->count;
        switch (vertex_attribute->format) {
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
            vertex_attribute->gl_size = GL_BGRA; // http://www.opengl.org/registry/specs/ARB/vertex_array_bgra.txt
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            assert(vertex_attribute->count == 4);
            vertex_attribute->needs_conversion = false; //FIXME: Remove once the new system works
            vertex_attribute->converter = NULL;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
            vertex_attribute->gl_type = GL_UNSIGNED_BYTE;
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 1;
            vertex_attribute->needs_conversion = false; //FIXME: Remove once the new system works
            vertex_attribute->converter = NULL;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
            vertex_attribute->gl_type = GL_SHORT;
//            vertex_attribute->gl_normalize = GL_TRUE; //(slot == 0)?GL_FALSE:GL_TRUE; //FIXME: Not sure, but wreckless uses this for texcoords and needs it normalized, the dashboards uses it for pos and needs it unnormalized
            vertex_attribute->gl_normalize = GL_TRUE;
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false; //FIXME: Remove once the new system works
            vertex_attribute->converter = NULL;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->size = 4;
            vertex_attribute->needs_conversion = false; //FIXME: Remove once the new system works
            vertex_attribute->converter = NULL;
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
            vertex_attribute->gl_type = GL_SHORT;
            vertex_attribute->gl_normalize = GL_FALSE; //(slot == 0)?GL_FALSE:GL_TRUE; //FIXME: see note in _S1
            vertex_attribute->size = 2;
            vertex_attribute->needs_conversion = false; //FIXME: Remove once the new system works
            vertex_attribute->converter = NULL;
            assert(0); //Untested
            break;
        case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
            /* "3 signed, normalized components packed in 32-bits. (11,11,10)" */
            vertex_attribute->size = 4;
            vertex_attribute->gl_type = GL_FLOAT;
            vertex_attribute->gl_normalize = GL_FALSE;
            vertex_attribute->needs_conversion = true; //FIXME: Remove once the new system works
            vertex_attribute->converted_size = 4;
            vertex_attribute->converted_count = 3 * vertex_attribute->count;
            vertex_attribute->converter = convert_cmp_to_f;
            break;
        default:
            printf("Unknown vertex format: %d\n", vertex_attribute->format);
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
    }
    CASE_RANGE(NV097_SET_VERTEX_DATA_ARRAY_OFFSET,16) {

        pg->vertex_attributes[slot].dma_select =
            parameter & 0x80000000;
        pg->vertex_attributes[slot].offset =
            parameter & 0x7fffffff;

        pg->vertex_attributes[slot].converted_elements = 0;

        break;
    }
    case NV097_SET_BEGIN_END: {

        /* Stencil and Depth test would disable stencil / depth */
        bool writeDepth =  GET_PG_REG(pg, CONTROL_0, ZWRITEENABLE) &&
                           GET_PG_REG(pg, CONTROL_0, ZENABLE);
        bool writeStencil = GET_PG_REG(pg, CONTROL_0, STENCIL_WRITE_ENABLE) &&
                            GET_PG_REG(pg, CONTROL_1, STENCIL_TEST_ENABLE) &&
                            GET_PG_REG(pg, CONTROL_1, STENCIL_MASK_WRITE);
        bool writeZeta = writeDepth || writeStencil;
        bool writeColor = GET_PG_REG(pg, CONTROL_0, ALPHA_WRITE_ENABLE) ||
                          GET_PG_REG(pg, CONTROL_0, RED_WRITE_ENABLE) ||
                          GET_PG_REG(pg, CONTROL_0, GREEN_WRITE_ENABLE) ||
                          GET_PG_REG(pg, CONTROL_0, BLUE_WRITE_ENABLE);

        if (parameter != NV097_SET_BEGIN_END_OP_END) {

            assert(parameter <= NV097_SET_BEGIN_END_OP_POLYGON);

            /* Debug output */
            {
                char buffer[128];
                sprintf(buffer,"NV2A: BEGIN_END 0x%X", parameter);
                debugger_push_group(buffer);
            }

            /* Upload the surface to the GPU */
            pgraph_update_surfaces(d, true, writeZeta, writeColor);
            

            /* FIXME: Skip most stuff if writeZeta and writeColor are both false */
            /* FIXME: Use simplified shader for "zeta only" (color masked) writes? */

#ifdef FORCE_NOXY
            CHECKXY(pg)
#else
            //FIXME: Use offset?
            assert(pg->clip_x == 0);
            assert(pg->clip_y == 0);
#endif
            unsigned int w;
            unsigned int h;
            get_surface_dimensions(pg, &w, &h);

            glViewport(0, 0, w, h);
            static unsigned int draw_call = 0;
            draw_call++;
            float zclip_max = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMAX];
            float zclip_min = *(float*)&pg->regs[NV_PGRAPH_ZCLIPMIN];
            VertexAttribute* v0 = &pg->vertex_attributes[0];
            constant = &pg->constants[58];
#if 1
            debugger_message("frame: %i call: %i: clip: %ix%i [%.3f;%.3f] VP: %.3fx%.3fx%.3f AA: %i, PL: %s, v0 { count: %i, .type: %i, .norm: %i }",
            debugger_frame, draw_call,
            pg->clip_width,pg->clip_height, zclip_min, zclip_max,
            *(float*)&constant->data[0],*(float*)&constant->data[1],*(float*)&constant->data[2],
            pg->surface_anti_aliasing,
            GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],NV_PGRAPH_CSV0_D_MODE)?"VSH":"FFP",
            v0->count,v0->gl_type,v0->gl_normalize);
#endif

            NV2A_GPU_DPRINTF("Frame %d, Draw call %d\n", debugger_frame, draw_call);

            pgraph_update_state(pg);

            pgraph_bind_shaders(pg);

            pgraph_bind_textures(d);


            pg->gl_primitive_mode = kelvin_primitive_map[parameter];

#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
            if (GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                                  NV_PGRAPH_CSV0_D_MODE) != 0) {
                debugger_begin_feedback(kelvin);
            }
#endif

            pg->inline_elements_length = 0;
            pg->inline_array_length = 0;
            pg->inline_buffer_length = 0;

        } else {

            if (pg->inline_buffer_length) {
                assert(!pg->inline_array_length);
                assert(!pg->inline_elements_length);

                for(i = 0; i < 16; i++) {
                    glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(pg->inline_buffer[0]), pg->inline_buffer[0].v[i]);
                    glEnableVertexAttribArray(i);
                }
  
//                assert(0); //FIXME: This code path needs a major rewrite..
                debugger_message("BAD!");
                debugger_message("DRAW: Inline Buffer");
                glDrawArrays(pg->gl_primitive_mode,
                             0, pg->inline_buffer_length);
            } else if (pg->inline_array_length) {
                assert(!pg->inline_buffer_length);
                assert(!pg->inline_elements_length);
                unsigned int vertex_size =
                    pgraph_bind_inline_array(d);
                unsigned int index_count =
                    pg->inline_array_length*4 / vertex_size;
                debugger_message("%d bytes of inline data, vertex is %d bytes",pg->inline_array_length*4,vertex_size);
                              
                pgraph_bind_converted_vertex_attributes(d, true, index_count);
                debugger_message("DRAW: Inline Array");
      
/*
                unsigned int p = pg->inline_array;
                if (vertex_size == ((4*2)*2)) {
                    glVertexAttribPointer(0,2,GL_FLOAT,(4*2)*2,p);
                    glVertexAttribPointer(9,2,GL_FLOAT,(4*2)*2,p+4*2);
                }
*/
                glDrawArrays(pg->gl_primitive_mode,
                             0, index_count);
            } else if (pg->inline_elements_length) {
                assert(!pg->inline_array_length);
                assert(!pg->inline_buffer_length);

                uint32_t max_element = 0;
                uint32_t min_element = (uint32_t)-1;
                for (i=0; i<pg->inline_elements_length; i++) {
                    max_element = MAX(pg->inline_elements[i], max_element);
                    min_element = MIN(pg->inline_elements[i], min_element);
                }

                pgraph_bind_vertex_attributes(d);
                pgraph_bind_converted_vertex_attributes(d, false,
                                                        max_element + 1);

#ifdef DEBUG_NV2A_GPU_EXPORT
                GLint prog;
                glGetIntegerv(GL_CURRENT_PROGRAM,&prog);
                char tmp[32];
                sprintf(tmp,"./buffer-%i.obj",prog);
                debugger_export_mesh(tmp,0,3,6,max_element+1,pg->inline_elements_length,pg->inline_elements,pg->gl_primitive_mode);
                sprintf(tmp,"./buffer-%i.vsh",prog);
                debugger_export_vertex_shader(tmp, kelvin, true);
#endif

                debugger_message("DRAW: Inline Elements");
                glDrawRangeElements(pg->gl_primitive_mode,
                                    min_element, max_element,
                                    pg->inline_elements_length,
                                    GL_UNSIGNED_INT,
                                    pg->inline_elements);
            } else {
                static unknown_draw = 0;
                debugger_message("DRAW: Unknown method %d?!",unknown_draw);
                printf("Unknown draw method %d?!\n",unknown_draw);
                unknown_draw++;
            }


#ifdef DEBUG_NV2A_GPU_SHADER_FEEDBACK
            if (GET_MASK(pg->regs[NV_PGRAPH_CSV0_D],
                         NV_PGRAPH_CSV0_D_MODE) != 0) {
                if (debugger_end_feedback(kelvin)) {
                    debugger_dump_feedback(0,10);
                }
            }
#endif

            if (writeZeta) {
                pg->surface_zeta.draw_dirty = true;
            }
            if (writeColor) {
                pg->surface_color.draw_dirty = true;
            }

            assert(glGetError() == GL_NO_ERROR);
            debugger_pop_group();


#if 1 // Dump all draw calls
            debugger_push_group("Drawdump");
//            glFinish();
//            pgraph_update_surfaces(d, false, true, true);
            debugger_pop_group();
            debugger_finish_frame();
#endif

        }
        break;
    }
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
        pg->textures[slot].log_depth =
            GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P);

        pg->textures[slot].dirty = true;
        pg->dirty.shaders = true; /* FORMAT_COLOR used in combiner */
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
        pg->textures[slot].alphakill =
            GET_MASK(parameter, NV097_SET_TEXTURE_CONTROL0_ALPHA_KILL_ENABLE);

        pg->textures[slot].dirty = true;
        pg->dirty.shaders = true; /* ALPHA_KILL_ENABLE used in combiner */
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
    case NV097_SET_POINT_SIZE: {
        float point_size = *(float*)&parameter; //FIXME: also exists in reg
        glPointSize(point_size); //FIXME: Defer..
        break;
    }
    case NV097_ARRAY_ELEMENT16:
        assert(pg->inline_elements_length < NV2A_GPU_MAX_BATCH_LENGTH);
        pg->inline_elements[
            pg->inline_elements_length++] = parameter & 0xFFFF;
        pg->inline_elements[
            pg->inline_elements_length++] = parameter >> 16;
        break;
    case NV097_ARRAY_ELEMENT32:
        assert(pg->inline_elements_length < NV2A_GPU_MAX_BATCH_LENGTH);
        pg->inline_elements[
            pg->inline_elements_length++] = parameter;
        break;
    case NV097_DRAW_ARRAYS: {
        /* This should only be callable between begin and end */

        pgraph_bind_vertex_attributes(d);

        unsigned int start = GET_MASK(parameter, NV097_DRAW_ARRAYS_START_INDEX);
        unsigned int count = GET_MASK(parameter, NV097_DRAW_ARRAYS_COUNT)+1;
        debugger_message("DRAW: Draw Arrays");
        glDrawArrays(pg->gl_primitive_mode, start, count);
        break;
    }
    case NV097_INLINE_ARRAY:
        assert(pg->inline_array_length < NV2A_GPU_MAX_BATCH_LENGTH);
        pg->inline_array[
            pg->inline_array_length++] = parameter;
        break;
    CASE_RANGE(NV097_SET_VERTEX_DATA2F_M,16*2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* data = entry->v[slot / 2];
        data[slot % 2] = *(float*)&parameter;
        debugger_message("0x%04x (2F): vert[%d].v%d.%c = %f",method,pg->inline_buffer_length,slot/2,'x'+slot%2,*(float*)&parameter);
        if ((slot / 2 == 0) && (slot % 2 == 1)) {
            //FIXME: Complete partial attributes [such as v3,v0,v0,v0 - v3 needs to be copied]
            //FIXME: Complete z,w or just get them from the previous attrib too?
      
            { // Hack to fake z,w completion
                data[2] = 0.0f;
                data[3] = 1.0f;
            }
            pg->inline_buffer_length++;
            debugger_message("Selecting next vertex");
        }
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX_DATA4F_M,16*4) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* data = entry->v[slot / 4];
        data[slot % 4] = *(float*)&parameter;
        if ((slot / 4 == 0) && (slot % 4 == 3)) {
            //FIXME: see NV097_SET_VERTEX_DATA2F_M
            pg->inline_buffer_length++;
        }
        assert(0); // Untested!
//FIXME: Used in public XDK API
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX_DATA4UB,16) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* data = entry->v[slot];
        data[0] = ((parameter >> 24) & 0xFF) / 255.0f; //FIXME: Ordering might be wrong! RGBA BGRA RGB BGR #findme
        data[1] = ((parameter >> 16) & 0xFF) / 255.0f;
        data[2] = ((parameter >> 8) & 0xFF) / 255.0f;
        data[3] = (parameter & 0xFF) / 255.0f;
        if (slot == 0) {
            //FIXME: see NV097_SET_VERTEX_DATA2F_M
            pg->inline_buffer_length++;
        }
//FIXME: Make sure this works!
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX_DATA2S,16) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* data = entry->v[slot];
        data[0] = ((int16_t*)&parameter)[0] / 32767.5f; //FIXME: Is this okay? We should probably map to inclusive [-1.0 to +1.0]
        data[1] = ((int16_t*)&parameter)[1] / 32767.5f;
        if (slot == 0) {
            //FIXME: see NV097_SET_VERTEX_DATA2F_M
            pg->inline_buffer_length++;
        }
        break;
    }
    CASE_RANGE(NV097_SET_VERTEX_DATA4S_M,16*2) {
        InlineVertexBufferEntry *entry =
            &pg->inline_buffer[pg->inline_buffer_length];
        float* data = entry->v[slot / 2];
        data[slot % 2 + 0] = ((int16_t*)&parameter)[0] / 32767.5f; //FIXME: Sync with NV097_SET_VERTEX_DATA2S once that is corret
        data[slot % 2 + 1] = ((int16_t*)&parameter)[1] / 32767.5f;
        if ((slot / 2 == 0) && (slot % 2 == 1)) {
            //FIXME: see NV097_SET_VERTEX_DATA2F_M
            pg->inline_buffer_length++;
        }
        assert(0); // Untested!
        break;
    }
    case NV097_SET_SEMAPHORE_OFFSET:
        kelvin->semaphore_offset = parameter;
        break;
    case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: {

        pgraph_update_surfaces(d, false, true, true);

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

        bool writeDepth = parameter & NV097_CLEAR_SURFACE_Z;
        bool writeStencil = parameter & NV097_CLEAR_SURFACE_STENCIL;
        bool writeZeta = writeDepth || writeStencil;
        bool writeColor = parameter & NV097_CLEAR_SURFACE_COLOR;

        /* Early out if we have nothing to clear */
        if (!(writeZeta || writeColor)) {
            break;
        }

        debugger_push_group("NV2A: CLEAR_SURFACE");

        GLbitfield gl_mask = 0;

        pgraph_update_surfaces(d, true, writeZeta, writeColor);

        if (writeZeta) {

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

            if (writeDepth) {
                gl_mask |= GL_DEPTH_BUFFER_BIT;
                glDepthMask(GL_TRUE);
                pg->dirty.depth_mask = true;
                glClearDepth(gl_clear_depth);
            }
            if (writeStencil) {
                gl_mask |= GL_STENCIL_BUFFER_BIT;
                glStencilMask(0xFF); /* We have 8 bits maximum anyway */
                pg->dirty.stencil_mask = true;
                glClearStencil(gl_clear_stencil);
            }

            pg->surface_zeta.draw_dirty = true;
        }

        if (writeColor) {

            gl_mask |= GL_COLOR_BUFFER_BIT;

            uint32_t clear_color = d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE];

            glColorMask((parameter & NV097_CLEAR_SURFACE_R)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_G)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_B)?GL_TRUE:GL_FALSE,
                        (parameter & NV097_CLEAR_SURFACE_A)?GL_TRUE:GL_FALSE);
            pg->dirty.color_mask = true;

            glClearColor( ((clear_color >> 16) & 0xFF) / 255.0f, /* red */
                          ((clear_color >> 8) & 0xFF) / 255.0f,  /* green */
                          (clear_color & 0xFF) / 255.0f,         /* blue */
                          ((clear_color >> 24) & 0xFF) / 255.0f);/* alpha */

            pg->surface_color.draw_dirty = true;
        }

        glEnable(GL_SCISSOR_TEST);

        unsigned int xmin = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMIN)*2; //FIXME: AA
        unsigned int xmax = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTX],
                NV_PGRAPH_CLEARRECTX_XMAX)*2; //FIXME: AA
        unsigned int ymin = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMIN)*2; //FIXME: AA
        unsigned int ymax = GET_MASK(d->pgraph.regs[NV_PGRAPH_CLEARRECTY],
                NV_PGRAPH_CLEARRECTY_YMAX)*2; //FIXME: AA
        glScissor(xmin, ymin, xmax-xmin + 1, ymax-ymin + 1);
        //FIXME: Is this being clipped? If not we need a special case of update_surface

        NV2A_GPU_DPRINTF("------------------CLEAR 0x%x %d,%d - %d,%d  %x---------------\n",
            parameter, xmin, ymin, xmax, ymax, d->pgraph.regs[NV_PGRAPH_COLORCLEARVALUE]);

        glClear(gl_mask);

        glDisable(GL_SCISSOR_TEST);

        debugger_pop_group();

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
        pg->dirty.shaders = true;
        break;

    CASE_RANGE(NV097_SET_EYE_VECTOR, 3) {
        pg->eye_vector[slot] = *(float*)&parameter;
        pg->dirty.eye_vector = true;
        break;
    }
    case NV097_SET_FOG_MODE: {
        uint32_t reg;
        switch(parameter) {
        case NV097_SET_FOG_MODE_LINEAR:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR;
            break;
        case NV097_SET_FOG_MODE_EXP:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP;
            break;
        case NV097_SET_FOG_MODE_EXP2:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2;
            break;
        case NV097_SET_FOG_MODE_EXP_ABS:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP_ABS;
            break;
        case NV097_SET_FOG_MODE_EXP2_ABS:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_EXP2_ABS;
            break;
        case NV097_SET_FOG_MODE_LINEAR_ABS:
            reg = NV_PGRAPH_CONTROL_3_FOG_MODE_LINEAR_ABS;
            break;
        default:
            assert(0);
        }
        SET_PG_REG(pg, CONTROL_3, FOG_MODE, reg);
        pg->dirty.fog_mode = true;
        break;
    }
    case NV097_SET_FOG_ENABLE:
        SET_PG_REG(pg, CONTROL_3, FOGENABLE, parameter);
        pg->dirty.fog = true;
        break;

    case NV097_SET_FOG_COLOR:
        SET_PG_REG(pg, FOGCOLOR, RED,
                   GET_MASK(parameter, NV097_SET_FOG_COLOR_RED));
        SET_PG_REG(pg, FOGCOLOR, GREEN,
                   GET_MASK(parameter, NV097_SET_FOG_COLOR_GREEN));
        SET_PG_REG(pg, FOGCOLOR, BLUE,
                   GET_MASK(parameter, NV097_SET_FOG_COLOR_BLUE));
        SET_PG_REG(pg, FOGCOLOR, ALPHA,
                   GET_MASK(parameter, NV097_SET_FOG_COLOR_ALPHA));
        pg->dirty.fog_color = true;
        break;

    case NV097_SET_SHADER_CLIP_PLANE_MODE:
        pg->regs[NV_PGRAPH_SHADERCLIPMODE] = parameter;
        break;

    case NV097_SET_COMBINER_COLOR_OCW ...
            NV097_SET_COMBINER_COLOR_OCW + 28:
        slot = (class_method - NV097_SET_COMBINER_COLOR_OCW) / 4;
        pg->regs[NV_PGRAPH_COMBINECOLORO0 + slot*4] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_COMBINER_CONTROL:
        pg->regs[NV_PGRAPH_COMBINECTL] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_SHADER_STAGE_PROGRAM:
        pg->regs[NV_PGRAPH_SHADERPROG] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_SHADER_OTHER_STAGE_INPUT:
        pg->regs[NV_PGRAPH_SHADERCTL] = parameter;
        pg->dirty.shaders = true;
        break;

    case NV097_SET_TRANSFORM_EXECUTION_MODE:
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_MODE,
                 GET_MASK(parameter, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE));
        SET_MASK(pg->regs[NV_PGRAPH_CSV0_D], NV_PGRAPH_CSV0_D_RANGE_MODE,
                 GET_MASK(parameter, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE));
        break;
    case NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN:
        pg->enable_vertex_program_write = parameter;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_LOAD:
        /* Parameter is [0,136], but we use the load_slot to address words in
           the instruction so we have to multiply it by 4 */
        assert(parameter < NV2A_GPU_MAX_VERTEXSHADER_LENGTH); //FIXME: Is this any useful? This just sets a register, it might be filled with crap unless it's actually used
        pg->vertexshader_load_slot = parameter * 4;
        break;
    case NV097_SET_TRANSFORM_PROGRAM_START:
        assert(parameter < NV2A_GPU_MAX_VERTEXSHADER_LENGTH);
        pg->vertexshader_start_slot = parameter;
        pg->vertexshader.dirty = true;
        break;
    case NV097_SET_TRANSFORM_CONSTANT_LOAD:
        /* Parameter is [0,192], but we use the load_slot to address components
           so we have to multiply it by 4 */
        assert(parameter < NV2A_GPU_VERTEXSHADER_CONSTANTS);
        pg->constant_load_slot = parameter*4;
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
        SET_PG_REG(pg, CONTROL_0, ALPHATESTENABLE, parameter);
        pg->dirty.alpha_test = true;
        break;
    case NV097_SET_BLEND_ENABLE:
        set_gl_state(GL_BLEND,kelvin->blend_enable = parameter);
        pg->dirty.blend = true;
        break;
    case NV097_SET_BLEND_FUNC_SFACTOR:
        set_gl_blend_func(kelvin->blend_func_sfactor = parameter,
                          kelvin->blend_func_dfactor);
        pg->dirty.blend_func = true;
        break;
    case NV097_SET_BLEND_FUNC_DFACTOR:
        set_gl_blend_func(kelvin->blend_func_sfactor,
                          kelvin->blend_func_dfactor = parameter);
        pg->dirty.blend_func = true;
        break;
    case NV097_SET_FRONT_FACE:
        set_gl_front_face(kelvin->front_face = parameter);
        pg->dirty.front_face = true;
        break;
    case NV097_SET_CULL_FACE_ENABLE:
        set_gl_state(GL_CULL_FACE,kelvin->cull_face_enable = parameter);
        pg->dirty.cull_face = true;
        break;
    case NV097_SET_CULL_FACE:
        set_gl_cull_face(kelvin->cull_face = parameter);
        pg->dirty.cull_face_mode = true;
        break;
#endif
#if 1
//FIXME: 2D seems to be gone because of stupid fixed function emu zbuffer calculation
    case NV097_SET_DEPTH_TEST_ENABLE:
        SET_PG_REG(pg, CONTROL_0, ZENABLE, parameter);
        pg->dirty.depth_test = true;
        break;
    case NV097_SET_DEPTH_FUNC:
        SET_PG_REG(pg, CONTROL_0, ZFUNC,
                   map_method_to_register_func(parameter));
        pg->dirty.depth_test_func = true;
        break;
#endif
#if 1
    case NV097_SET_ALPHA_FUNC:
        SET_PG_REG(pg, CONTROL_0, ALPHAFUNC,
                   map_method_to_register_func(parameter));
        pg->dirty.alpha_test_func = true;
        break;
    case NV097_SET_ALPHA_REF:
        SET_PG_REG(pg, CONTROL_0, ALPHAREF, parameter); //FIXME: is this parameter a float?
        pg->dirty.alpha_test_func = true;
        break;
    case NV097_SET_EDGE_FLAG:
        glEdgeFlag((kelvin->edge_flag = parameter)?GL_TRUE:GL_FALSE);
        pg->dirty.edge_flag = true;
        break;
#endif
    default:
        NV2A_GPU_DPRINTF("    unhandled  (0x%04x 0x%04x: 0x%08x)\n",
                     object->graphics_class, method, parameter);
        const char* method_name = "";
        uint32_t nmethod = 0;
        switch (object->graphics_class) {
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
        debugger_message("NV2A: unhandled method 0x%04x 0x%04x: 0x%08x (%s)",
                    object->graphics_class, method, parameter, method_name);
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
        //debugger_message("pgraph method (Class 0x%x): 0x%04x 0x%04x: 0x%x",graphics_class,subchannel,graphics_class,method,parameter);
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

#if 0 // Code to test GLSL stuff

    unsigned char seafloor_xvu[] = {
      0x78, 0x20, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0xc0, 0xef, 0x00,
      0x6c, 0x18, 0x36, 0x08, 0x00, 0x88, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00,
      0x1b, 0xe0, 0xef, 0x00, 0x6c, 0x18, 0x36, 0x08, 0x00, 0x48, 0x70, 0x20,
      0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0xf0, 0x00, 0x6c, 0x18, 0x36, 0x08,
      0x00, 0x28, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x20, 0xf0, 0x00,
      0x6c, 0x18, 0x36, 0x08, 0x00, 0x18, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00,
      0x1b, 0x06, 0xb1, 0x00, 0x6c, 0x18, 0x36, 0x08, 0xf8, 0x0f, 0x20, 0x28,
      0x00, 0x00, 0x00, 0x00, 0x1b, 0x40, 0x31, 0x00, 0x6c, 0x10, 0x36, 0x0c,
      0x18, 0xf8, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x51, 0x00,
      0x6c, 0x18, 0x36, 0x24, 0x20, 0xf8, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00,
      0x1b, 0x0c, 0x20, 0x00, 0x6c, 0x10, 0x36, 0x08, 0x48, 0xc8, 0x70, 0x20,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x51, 0x00, 0x6c, 0x10, 0x54, 0x0c,
      0xf8, 0x0f, 0x30, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x15, 0x80, 0x71, 0x00,
      0xa8, 0x12, 0x36, 0x34, 0x50, 0xc8, 0x70, 0x30, 0x00, 0x00, 0x00, 0x00,
      0x1b, 0x40, 0xf2, 0x00, 0x6c, 0x18, 0x36, 0x08, 0xf8, 0x0f, 0x40, 0x28,
      0x00, 0x00, 0x00, 0x00, 0x1b, 0x60, 0xf2, 0x00, 0x6c, 0x18, 0x36, 0x08,
      0xf8, 0x0f, 0x40, 0x24, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x80, 0xf2, 0x00,
      0x6c, 0x18, 0x36, 0x08, 0xf8, 0x0f, 0x50, 0x22, 0x00, 0x00, 0x00, 0x00,
      0xaa, 0x61, 0x91, 0x00, 0x54, 0x19, 0x54, 0xc5, 0x28, 0x88, 0x70, 0x30,
      0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x04, 0xa9, 0x12, 0x36, 0x08,
      0xf8, 0x0f, 0x12, 0x50, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x40, 0x47, 0x06,
      0xff, 0x1b, 0x36, 0xc4, 0x00, 0xe8, 0x18, 0x10, 0x00, 0x00, 0x00, 0x00,
      0x15, 0x00, 0x40, 0x00, 0x6c, 0x28, 0x54, 0x45, 0x58, 0xc8, 0x70, 0x20,
      0x00, 0x00, 0x00, 0x00, 0x1b, 0x60, 0x87, 0x00, 0x6c, 0x28, 0x00, 0xc4,
      0x01, 0xe8, 0x70, 0x30
    };


    QString *program_code_glsl = vsh_translate(VSH_VERSION_XVS,
                                               &seafloor_xvu[4],
                                               136*4);

    const char* program_code_str = qstring_get_str(program_code_glsl);

    printf("%s\n",program_code_str);
    exit(5);
#endif



    PCIDevice *dev = pci_create_simple(bus, devfn, "nv2a");
    NV2A_GPUState *d = NV2A_GPU_DEVICE(dev);
    nv2a_gpu_init_memory(d, ram);
}

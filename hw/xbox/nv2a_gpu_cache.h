//FIXME: License and move to nv2a_gpu_cache.c

#include "xxhash.h"

static inline void flip(
    const uint8_t *src_buf,
    unsigned int src_pitch,
    unsigned int width,
    unsigned int height,
    uint8_t *dst_buf,
    unsigned int dst_pitch,
    unsigned int bytes_per_pixel)
{
    int y;
    for (y = 0; y < height; y++) {
        uint8_t *src = src_buf + y * src_pitch;
        uint8_t *dst = dst_buf + (height - y - 1) * dst_pitch;
        memcpy(dst, src, width * bytes_per_pixel);
    }
}

typedef struct {
    hwaddr address; /* Physical source address */
    size_t size; /* Size of memory */
} MemoryBlock;

static inline void set_memory_dirty(const NV2A_GPUState* d, const MemoryBlock* memory_block)
{
    memory_region_set_dirty(d->vram, memory_block->address,
                                     memory_block->size);
}

static inline bool is_resource_memory_dirty(const NV2A_GPUState* d, const MemoryBlock* memory_block)
{
    return memory_region_get_dirty(d->vram, memory_block->address,
                                            memory_block->size,
                                            DIRTY_MEMORY_NV2A_GPU_RESOURCE);
}

static inline void set_resource_memory_clean(const NV2A_GPUState* d, const MemoryBlock* memory_block)
{
    memory_region_reset_dirty(d->vram,
                              memory_block->address,
                              memory_block->size,
                              DIRTY_MEMORY_NV2A_GPU_RESOURCE);
}

/* If any of the bytes is used by a and b this will return true */
static bool overlaps(const MemoryBlock* a, const MemoryBlock* b)
{
    hwaddr al = a->address;
    hwaddr ar = al + a->size - 1;
    hwaddr bl = b->address;
    hwaddr br = bl + b->size - 1;
    if (ar < bl) { return false; } // exclude [ al-ar   bl--br ]
    if (al > br) { return false; } // exclude [ bl--br   al-ar ]
    return true;
}

/* If all bytes of b are also used in a this will return true */
static bool contains(const MemoryBlock* a, const MemoryBlock* b)
{
    hwaddr al = a->address;
    hwaddr ar = al + a->size - 1;
    hwaddr bl = b->address;
    hwaddr br = bl + b->size - 1;
    if (bl < al) { return false; } // exclude [ bl---al--br-ar ]
    if (br > ar) { return false; } // exclude [ al---bl--ar-br ]
    return true;
}

#if 0
typedef struct {
    struct VertexshaderKey {
        uint32_t hash;
    } key;
    GLuint shader;
} Vertexshader;

typedef struct {
    struct FFPVertexshaderKey {
        //FIXME: Do this properly by shadowing the pgraph regs - this is just an empty placeholder
        bool textureMatrix[4];
    } key;
    GLuint shader;
} FFPVertexshader;

typedef struct {
    struct FragmentshaderKey {
        //FIXME: Do this properly by shadowing the pgraph regs - this is just a placeholder from _psh.h
        uint32_t combiner_control, uint32_t shader_stage_program,
        uint32_t other_stage_input,
        uint32_t rgb_inputs[8], uint32_t rgb_outputs[8],
        uint32_t alpha_inputs[8], uint32_t alpha_outputs[8],
        /*uint32_t constant_0[8], uint32_t constant_1[8],*/
        uint32_t final_inputs_0, uint32_t final_inputs_1,
        /*uint32_t final_constant_0, uint32_t final_constant_1,*/
        bool rect_tex[4], bool compare_mode[4][4],
        bool alphakill[4]
    } key;
    GLuint shader;
} Fragmentshader;

typedef struct {
    struct ShaderprogramKey {
        GLuint vertexshader, fragmentshader;
    } key;
    GLuint program;
} Shaderprogram;

#endif


typedef struct {
    struct PixelsKey {
        MemoryBlock memory_block; /* Used memory block */ //FIXME: calculate size from MAX(width*bytes_per_pixel,pitch)*height
        bool swizzled; /* Was the texture loaded from swizzled memory? */
        unsigned int bytes_per_pixel;
        unsigned int pitch;
        unsigned int data_width;  /* Can be NPOT, will only swizzle to.. */
        unsigned int data_height; /* ..next POT level                    */
    } key;
    GLuint gl_buffer;
    bool draw_dirty; // Content not from RAM / modified by GPU
    bool dirty; // Needs reupload from RAM
} Pixels;

typedef struct {
    struct Texture2DKey {
        hwaddr address;
        GLsizei width, height;
        unsigned int pitch;
        unsigned int format;
        unsigned int levels;
    } key;
    Pixels** buffer;
//    unsigned int min_mipmap_level;
//    unsigned int max_mipmap_level;
    GLuint gl_texture;
} Texture2D;

typedef struct Framebuffer {
    struct FramebufferKey {
        Texture2D* zeta_texture;
        bool has_stencil;
        Texture2D* color_texture;
    } key;
    GLuint gl_framebuffer;
    struct {
        bool zeta;
        bool color;
    } dirty;
} Framebuffer;


typedef struct {
    NV2A_GPUState* d;
    MemoryBlock* memory_block;
} NV2A_GPUStateMemoryBlock;

/* Checks if memory_block overlaps pixels and marks pixels dirty.
   If memory_block is NULL this will always mark the pixels dirty. */
static void mark_pixels_dirty(NV2A_GPUState* d, const MemoryBlock* memory_block, Pixels* pixels)
{
    bool overlapping = overlaps(&pixels->key.memory_block, memory_block);
    debugger_push_group("mark_pixels_dirty, %d = 0x%x - 0x%x: %s",
        pixels->gl_buffer,
        pixels->key.memory_block.address,
        pixels->key.memory_block.address+pixels->key.memory_block.size-1,
        overlapping?"Hit":"Miss");
    if ((memory_block == NULL) || overlapping) {
        debugger_message("Setting pixels dirty: %d", pixels->gl_buffer);
        pixels->dirty = true;
        debugger_message("Checking pixels dirty: %d", pixels->gl_buffer);
        debugger_message("Checking pixels draw dirty: %d", pixels->gl_buffer);
        assert(!(pixels->dirty && pixels->draw_dirty));
    }
    debugger_pop_group();
}

static void mark_all_pixels_dirty_callback(
    gpointer key,
    gpointer value,
    gpointer user_data)
{
    NV2A_GPUStateMemoryBlock* d_memory_block = (NV2A_GPUStateMemoryBlock*)user_data;
    Pixels* pixels = (Pixels*)key;
    mark_pixels_dirty(d_memory_block->d,
                      d_memory_block->memory_block,
                      pixels);
}

/* Checks if memory_block overlaps any resource and marks those resources dirty.
   If memory_block is NULL this will mark every resource dirty.
   The qemu resource dirty bits are clean for the used block aftwards */
static void sync_all_resources_memory_dirty(NV2A_GPUState* d, const MemoryBlock* memory_block) {
    debugger_push_group("NV2A: sync_all_resources_memory_dirty(0x%x - 0x%x)",
                        memory_block?
                            memory_block->address:
                            0x0,
                        memory_block?
                            memory_block->address+memory_block->size-1:
                            0xffffffff);
    NV2A_GPUStateMemoryBlock d_memory_block = {
        d, memory_block
    };
    memory_region_sync_dirty_bitmap(d->vram);     //FIXME: Optimally this should only happen once per draw call! so this should be moved into a wrapper functoin which prepares everyting memory related
    if ((memory_block == NULL) || is_resource_memory_dirty(d, memory_block)) {
        g_hash_table_foreach(d->pgraph.cache.pixels, mark_all_pixels_dirty_callback, (gpointer)&d_memory_block);
        //FIXME: Add other resources
        if (memory_block != NULL) {
            set_resource_memory_clean(d, memory_block);
        }
    }    
    debugger_pop_group();
}

static gboolean remove_pixels_from_texture_2d_cache_callback(
    gpointer key,
    gpointer value,
    gpointer user_data)
{
    Pixels* pixels = (Pixels*)user_data;
    Texture2D* texture_2d = (Texture2D*)key;
    unsigned int level;
    gboolean exists = FALSE;
    for(level = 0; level < texture_2d->key.levels; level++) {
        /* Remove texture level if this uses the deleted buffer */
        if (texture_2d->buffer[level] == pixels) {
            texture_2d->buffer[level] = NULL;
        }
        /* Check if any texture level exists to keep this in cache*/
        if (texture_2d->buffer[level] != NULL) {
            exists = TRUE;
        }
    }
    return exists;
}

static void remove_pixels_from_textures(PGRAPHState* pg, Pixels* pixels)
{
    //FIXME: If this is currently a rendertarget we have to write it back to memory, we also have to invalidate the framebuffer because the texture might be deleted!

    /* These are foreach_remove because textures without buffer will be destroyed */
    //FIXME: Add other caches..
    g_hash_table_foreach_remove(pg->cache.texture_2d,
                                remove_pixels_from_texture_2d_cache_callback,
                                pixels);
}

typedef struct {
    PGRAPHState* pg;
    MemoryBlock* memory_block;
} PGRAPHStateMemoryBlock;

static gboolean remove_memory_from_pixels_cache_callback(
    gpointer key,
    gpointer value,
    gpointer user_data)
{
    PGRAPHStateMemoryBlock* pg_memory_block = (PGRAPHStateMemoryBlock*)user_data;
    Pixels* pixels = (Pixels*)key;
    
    /* Remove the pixels if the memory block overlaps the given block */
    if (overlaps(&pixels->key.memory_block, pg_memory_block->memory_block)) {
        //FIXME: do this elsewhere?
        {
            debugger_message("Deleting pixels %d, draw dirty: %d", pixels->gl_buffer, pixels->draw_dirty);
            assert(!pixels->draw_dirty); //FIXME: Should be as simle as downloading them now..
            remove_pixels_from_textures(pg_memory_block->pg, pixels);
            glDeleteBuffersARB(1, &pixels->gl_buffer); 
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

static void remove_memory_from_pixels_cache(PGRAPHState* pg, const MemoryBlock* memory_block)
{
    PGRAPHStateMemoryBlock pg_memory_block = {
        pg, memory_block
    };
    g_hash_table_foreach_remove(pg->cache.pixels,
                                remove_memory_from_pixels_cache_callback,
                                &pg_memory_block);
}






guint pixels_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct PixelsKey), 0);
}

gboolean pixels_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct PixelsKey)) == 0;
}


   
#if 0
    //FIXME: Move upload elsewhere and make sure different levels are different texture objects
    switch(gl_target)
    case GL_TEXTURE_RECTANGLE: {
        assert(levels == 1);
        //FIXME
    case GL_TEXTURE_2D:
        //FIXME
        break;
    case GL_TEXTURE_3D:
        //FIXME: Upload
        break;
    case GL_TEXTURE_CUBE_MAP: {
        size_t face_size = 0;
        /* Upload each side */
        for(i = 0; i < 6; i++) {
            GLenum faces[6] = {
                GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
            };
            unsigned int level_width = width;
            unsigned int level_height = height;
            for(level = 0; level < levels; level++) {
                bool can_copy = false;
                //FIXME: try to find a GL_TEXTURE_2D (renderbuffer..)
                if (can_copy) {
                    glCopyImageSubData();
                } else {
                    glTexImage2D(data);
                }
                level_width /= 2;
                level_height /= 2;
            }
        }
        break;
    }
    default:
        assert(0);
    }
#endif

Pixels* create_pixels(PGRAPHState* pg,
    const MemoryBlock* memory_block,
    bool swizzled,
    unsigned int bytes_per_pixel,
    unsigned int pitch,
    unsigned int data_width,
    unsigned int data_height)
{

    Pixels* cache_pixels;
    Pixels pixels = {
        .key = {
            .memory_block = { memory_block->address, memory_block->size },
            .swizzled = swizzled,
            .bytes_per_pixel = bytes_per_pixel,
            .pitch = pitch,
            .data_width = data_width,
            .data_height = data_height
        }
    };
    if ((cache_pixels = g_hash_table_lookup(pg->cache.pixels, &pixels))) {
        return cache_pixels;
    }

    cache_pixels = g_memdup(&pixels, sizeof(pixels));

    assert(glGetError() == 0);

    glGenBuffers(1, &cache_pixels->gl_buffer);

    debugger_message("Creating pixels dirty: %d", cache_pixels->gl_buffer);
    debugger_message("Creating pixels draw clean: %d", cache_pixels->gl_buffer);
    cache_pixels->dirty = true;
    cache_pixels->draw_dirty = false;

    printf("Created new pixels buffer: 0x%x-0x%x, swizzled: %d (%dx%d) %dbpp, %d pitch\n",
           cache_pixels->key.memory_block.address,
           cache_pixels->key.memory_block.address+cache_pixels->key.memory_block.size-1,
           cache_pixels->key.swizzled,
           cache_pixels->key.data_width,
           cache_pixels->key.data_height,
           cache_pixels->key.bytes_per_pixel * 8,
           cache_pixels->key.pitch);

//    debugger_label(GL_BUFFER, cache_pixels->gl_buffer, "FIXME: Pixel buffer");

    /* Remove any old entry in the same region, then add new entry to cache */
    remove_memory_from_pixels_cache(pg, &cache_pixels->key.memory_block);
    g_hash_table_add(pg->cache.pixels, cache_pixels);
    return cache_pixels;
}






/* If a buffer-copy is still in progress you can use this to get a new buffer
   so you don't have to wait for the old buffer-copy to complete
   Note that this leaves the contents empty */
static void abort_framebuffer_to_pixels_download(
    Framebuffer* framebuffer,
    bool zeta,
    bool color)
{
//return; //XXX: PACKBUFFERHACK
    /* Clear zeta */
    if (zeta) {
        Pixels* pixels = framebuffer->key.zeta_texture->buffer[0];
        if (pixels) {
            glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, pixels->gl_buffer);
            glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB,
                            0,
                            NULL, GL_STREAM_DRAW_ARB);
        }
    }

    /* Clear color */
    if (color) {
        Pixels* pixels = framebuffer->key.color_texture->buffer[0];
        if (pixels) {
            glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, pixels->gl_buffer);
            glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB,
                            0,
                            NULL, GL_STREAM_DRAW_ARB);
        }
    }

}

static void mark_framebuffer_dirty(
    Framebuffer* framebuffer,
    bool zeta,
    bool color)
{
    //FIXME: !!! Find a good way to do this
    //       The pixels should know that they still have data to fetch

    return;
}

/* This will be called internally to attempt to download changes using DMA 
   while CPU / GPU do someting else. */
static void start_framebuffer_to_pixels_download(
    Framebuffer* framebuffer,
    bool zeta,
    bool color)
{
//return; //XXX: PACKBUFFERHACK
//FIXME: respect offset
//FIXME: respect pitch

    debugger_push_group("%s(framebuffer=%d, zeta=%d, color=%d)", __FUNCTION__, framebuffer->gl_framebuffer, zeta, color);

    //FIXME: Bind framebuffer? (or textures if we read from texture instead)

    /* Download Zeta */
    if (zeta) {
        //FIXME: Move this to start_texture_2d_to_pixels_download(unsigned int level) or something like that
        //       Maybe start_framebuffer_texture_to_pixels_download and store format in framebuffer?
        //       I'd really want to get rid of the explicit texture type naming in framebuffers anyway
        //       Ideally a framebuffer would only know the pixel pointer and format+type.
        //       Then bind it to the proper texture on creation so it's fed back instantly.
        Texture2D* texture = framebuffer->key.zeta_texture;
        if (texture != NULL) {
            Pixels* pixels = texture->buffer[0];
            const TextureFormatInfo* mapped_format =
                &kelvin_texture_format_map[texture->key.format];
            glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,
                            pixels->gl_buffer);
            debugger_push_group("downloading zeta surface");
/*
            glo_readpixels(mapped_format->gl_format, mapped_format->gl_type,
                           pixels->key.bytes_per_pixel,
                           pixels->key.data_width * pixels->key.bytes_per_pixel,
                           pixels->key.data_width, pixels->key.data_height, 0);
*/
            glReadPixels(0, 0, pixels->key.data_width, pixels->key.data_height,
                         mapped_format->gl_format, mapped_format->gl_type,
                         0);
            debugger_pop_group();
            debugger_message("Setting pixels draw dirty: %d",
                             pixels->gl_buffer);
            pixels->draw_dirty = true;
        }
    }

    /* Download Color */
    if (color) {
        Texture2D* texture = framebuffer->key.color_texture;
        if (texture != NULL) {
            Pixels* pixels = texture->buffer[0];
            const TextureFormatInfo* mapped_format =
                &kelvin_texture_format_map[texture->key.format];
            glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB,
                            pixels->gl_buffer);
            debugger_push_group("downloading color surface");
/*
            glo_readpixels(mapped_format->gl_format, mapped_format->gl_type,
                           pixels->key.bytes_per_pixel,
                           pixels->key.data_width * pixels->key.bytes_per_pixel,
                           pixels->key.data_width, pixels->key.data_height, 0);
*/
            glReadPixels(0, 0, pixels->key.data_width, pixels->key.data_height,
                         mapped_format->gl_format, mapped_format->gl_type,
                         0);

            debugger_pop_group();
            debugger_message("Setting pixels draw dirty: %d",
                             pixels->gl_buffer);
            pixels->draw_dirty = true;
        }
    }

    debugger_pop_group();

}

static void upload_memory_to_pixels(NV2A_GPUState *d, Pixels* pixels) {
    debugger_push_group("NV2A: upload_memory_to_pixels(%d)", pixels->gl_buffer);
    sync_all_resources_memory_dirty(d, &pixels->key.memory_block);
    if (pixels->dirty) {
        assert(!pixels->draw_dirty); /* Make sure we don't kill GPU results */
        glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pixels->gl_buffer);
        glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB,
                        pixels->key.memory_block.size,
                        NULL, GL_STREAM_DRAW_ARB);
        GLubyte* dst = (GLubyte*)glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB,
                                                GL_WRITE_ONLY_ARB);
        if (!dst) {
            assert(0);
            return;
        }

        if (pixels->key.swizzled) {
            assert(pixels->key.data_width * pixels->key.bytes_per_pixel == pixels->key.pitch); /* unswizzle_and_flip doesn't handle a dst_pitch yet */
            unswizzle_and_flip(d->vram_ptr + pixels->key.memory_block.address,
                               pixels->key.data_width,
                               pixels->key.data_height,
                               dst,
                               pixels->key.data_width * pixels->key.bytes_per_pixel, /* destination row-length is always same as width, we don't store unused pixels for swizzled textures */
                               pixels->key.bytes_per_pixel);
        } else {
            flip(d->vram_ptr + pixels->key.memory_block.address,
                 pixels->key.pitch,
                 pixels->key.data_width,
                 pixels->key.data_height,
                 dst,
                 pixels->key.data_width * pixels->key.bytes_per_pixel, /* destination row-length is always same as width, we don't store unused pixels for unswizzled textures */
                 pixels->key.bytes_per_pixel);
        }
        glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
        debugger_message("Setting pixels clean: %d", pixels->gl_buffer);
        /* Inform all users by removing the pixels from their buffer list,
           they'll have to recreate all pixels and hit this (updated) cache
           entry. */
        remove_pixels_from_textures(&d->pgraph, pixels);
        pixels->dirty = false;
    }
    debugger_pop_group();
}

static void download_pixels_to_memory(NV2A_GPUState* d, Pixels* pixels)
{
//return; //XXX: PACKBUFFERHACK
    if (pixels->draw_dirty) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, pixels->gl_buffer);
        GLubyte* src = (GLubyte*)glMapBufferARB(GL_PIXEL_PACK_BUFFER_ARB,
                                                GL_READ_ONLY_ARB);
        if (!src) {
            assert(0);
            return;
        }

        /* Assert that we don't overwrite changes from the CPU */
        debugger_message("Checking pixels dirty: %d", pixels->gl_buffer);
        assert(!pixels->dirty);
        assert(!is_resource_memory_dirty(d, &pixels->key.memory_block)); /* If this triggers we didn't sync properly! */

        if (pixels->key.swizzled) {
            assert(pixels->key.data_width * pixels->key.bytes_per_pixel == pixels->key.pitch); /* flip_and_swizzle doesn't handle a dst_pitch yet */
            flip_and_swizzle(src,
                             pixels->key.data_width,
                             pixels->key.data_height,
                             d->vram_ptr + pixels->key.memory_block.address,
                             pixels->key.data_width * pixels->key.bytes_per_pixel, /* source row-length is always same as width, we don't store unused pixels for swizzled textures */
                             pixels->key.bytes_per_pixel);
        } else {
            flip(src,
                 pixels->key.data_width * pixels->key.bytes_per_pixel, /* source row-length is always same as width, we don't store unused pixels for unswizzled textures */
                 pixels->key.data_width,
                 pixels->key.data_height,
                 d->vram_ptr + pixels->key.memory_block.address,
                 pixels->key.pitch,
                 pixels->key.bytes_per_pixel);
        }
        glUnmapBufferARB(GL_PIXEL_PACK_BUFFER_ARB);

        /* Notify everyone (VGA controller, KVM, ..) the memory is dirty */
        set_memory_dirty(d, &pixels->key.memory_block);
        pixels->draw_dirty = false; /* Nothing was done by the GPU at this point - we just wrote back all changes */
        debugger_message("Setting pixels draw clean: %d", pixels->gl_buffer);
        sync_all_resources_memory_dirty(d, &pixels->key.memory_block); /* Inform other resources using the same memory that it changed */
        pixels->dirty = false; /* The sync will set this, but we know this is fresh and clean */
        debugger_message("Setting pixels clean: %d", pixels->gl_buffer);
    }
}

static void download_all_pixels_to_memory_callback(
    gpointer key,
    gpointer value,
    gpointer user_data)
{
    download_pixels_to_memory((NV2A_GPUState*)user_data, (Pixels*)key);
}

static void download_all_pixels_to_memory(NV2A_GPUState *d)
{
    g_hash_table_foreach(d->pgraph.cache.pixels, download_all_pixels_to_memory_callback, (gpointer)d);
}

static guint texture_2d_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct Texture2DKey), 0);
}

static gboolean texture_2d_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct Texture2DKey)) == 0;
}


static Texture2D* bind_texture_2d(
    NV2A_GPUState *d,
    hwaddr address,
    unsigned int width, unsigned int height,
    unsigned int pitch,
    unsigned int format,
    unsigned int levels,
    unsigned int min_mipmap_level,
    unsigned int max_mipmap_level)
{

    Texture2D* cache_texture_2d;
    Texture2D texture_2d = {
        .key = {
            address,
            width, height,
            pitch,
            format,
            levels
        }
    };
    debugger_message("Will search texture_2d, %d entries in cache!",
                     g_hash_table_size(d->pgraph.cache.texture_2d));
    if ((cache_texture_2d = g_hash_table_lookup(d->pgraph.cache.texture_2d,
                                                    &texture_2d))) {
update_texture_2d:
/*
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL,
            cache_texture_2d->min_mipmap_level);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
            cache_texture_2d->max_mipmap_level);
*/
        debugger_push_group("%s from cache = %d", __FUNCTION__, cache_texture_2d->gl_texture);

        glBindTexture(GL_TEXTURE_2D, cache_texture_2d->gl_texture);

    #if 0
        //FIXME: Unhandled codepath because pitch / 2 != width / 2. No idea is swizzling supports pitch
            // Fact: width- and height-shift of a surface are only set for swizzled surfaces (on nv2a, here they are currently always valid as we need them for GL..)
            // Fact: pitch is always set, for both types of surfaces
        if (swizzled) {
            assert(pitch == (width * bytes_per_pixel)); //This does happen during startup anim already..
            assert(levels == 1);
        }
    #endif

        const TextureFormatInfo* mapped_format = &kelvin_texture_format_map[cache_texture_2d->key.format];
        bool swizzled = !mapped_format->linear;
        unsigned int bytes_per_pixel = mapped_format->bytes_per_pixel;
        GLenum gl_internal_format = mapped_format->gl_internal_format;
        GLenum gl_format = mapped_format->gl_format;
        GLenum gl_type = mapped_format->gl_type;


    //FIXME: Move to activate function
        unsigned int mipmap_width = cache_texture_2d->key.width;
        unsigned int mipmap_height = cache_texture_2d->key.height;
        unsigned int mipmap_pitch = cache_texture_2d->key.pitch;
        hwaddr mipmap_offset = 0;
        unsigned int level;
        for(level = 0; level < cache_texture_2d->key.levels; level++) {
            Pixels* pixels;
            MemoryBlock memory_block = {
                cache_texture_2d->key.address + mipmap_offset,
                swizzled?(mipmap_pitch * mipmap_height):
                         (mipmap_width * mipmap_height * bytes_per_pixel)
            };
            pixels = create_pixels(&d->pgraph,
                                   &memory_block,
                                   swizzled,
                                   bytes_per_pixel,
                                   mipmap_pitch,
                                   mipmap_width,
                                   mipmap_height);
    upload_memory_to_pixels(d, pixels); //FIXME: Do this in create_pixels or something?


            if (cache_texture_2d->buffer[level] != pixels) {
                //FIXME: if the old value is NULL the texture was invalidated or never existed
                //       if the old value was not NULL the pixels moved - FIXME: What would that mean? Rethink logic
                glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, pixels->gl_buffer); //FIXME: Done in upload op already
            } else {
                pixels = NULL; /* No update necessary! */
            }

            if ((gl_format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ||
                (gl_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ||
                (gl_format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) ||
                (gl_format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)) {

                unsigned int block_size;
                if (gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }
if (pixels) {
                glCompressedTexImage2D(GL_TEXTURE_2D, level, gl_internal_format,
                                       mipmap_width, mipmap_height, 0,
                                       mipmap_width/4 * mipmap_height/4 * block_size,
                                       0);
}
                mipmap_offset += mipmap_width/4 * mipmap_height/4 * block_size;

            } else {
if (pixels) {
                glTexImage2D(GL_TEXTURE_2D, level, gl_internal_format,
                             mipmap_width, mipmap_height, 0,
                             gl_format, gl_type,
                             0);
}
                mipmap_offset += mipmap_width * mipmap_height * bytes_per_pixel; //FIXME: Why no pitch?

            }
if (pixels) {
            cache_texture_2d->buffer[level] = pixels;
}
            mipmap_width /= 2;
            mipmap_height /= 2;
            mipmap_pitch /= 2; //FIXME: is this correct ???
        }

        debugger_label(GL_TEXTURE, cache_texture_2d->gl_texture,
                       "%d, 2D, 0x%x, pitch %d, format 0x%x%s, %d level(s) ([0] = %d%s)",
                       cache_texture_2d->gl_texture,
                       cache_texture_2d->key.address, 
                       cache_texture_2d->key.pitch,
                       cache_texture_2d->key.format,
                       swizzled ? " (Swizzled)" : "",
                       cache_texture_2d->key.levels,
                       cache_texture_2d->buffer[0]->gl_buffer,
                       cache_texture_2d->buffer[0]->draw_dirty?" (draw dirty)":""); //FIXME: This should never be true, line can be removed later once all bugs are gone

        debugger_pop_group();
        return cache_texture_2d;
    }

    cache_texture_2d = g_memdup(&texture_2d, sizeof(texture_2d));

    debugger_message("%s creating entry", __FUNCTION__);

    cache_texture_2d->buffer = g_malloc0(sizeof(Pixels*) * cache_texture_2d->key.levels);

    glGenTextures(1, &cache_texture_2d->gl_texture);

    g_hash_table_insert(d->pgraph.cache.texture_2d, cache_texture_2d, cache_texture_2d);
    goto update_texture_2d;
}

#if 0

typedef struct {
    Texture* buffer;
} TextureRectangle;

typedef struct {
    Texture* buffer[layers*levels];
} Texture3D;

typedef struct {
    Texture* buffer[6*levels];
} TextureCubemap;

#endif

#if 0

typedef struct {
    struct IndexbufferKey {
    //FIXME: Identify using hash
    } key;
    GLuint buffer;
} Indexbuffer;

typedef struct {
    struct VertexbufferKey {
        MemoryBlock block;
    } key;
    GLuint buffer;
} Vertexbuffer;

typedef struct {
    struct InlineVertexbufferKey {
        //FIXME: Identify using hash
    } key;
    GLuint buffer;
} InlineVertexbuffer;
#endif

static guint framebuffer_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct FramebufferKey), 0);
}

static gboolean framebuffer_equal(gconstpointer a, gconstpointer b)
{
#if 0
    const Framebuffer *as = a, *bs = b;
    /* For final we don't care if there are unused buffers attached */
    //FIXME: Use debug version if it's faster!
    if ((as->zeta_texture != 0) &&
        (as->zeta_texture != bs->zeta_texture)) {
        return false;
    }
    if ((as->color_texture != 0) && 
        (as->color_texture != bs->color_texture)) {
        return false;
    }
    return true;
#else
    /* For debugging we want to make sure we only bind used surfaces */
    return memcmp(a, b, sizeof(struct FramebufferKey)) == 0;
#endif
}

Framebuffer* bind_framebuffer(PGRAPHState* pg, Texture2D* zeta_texture, bool has_stencil, Texture2D* color_texture)
{

    Framebuffer* cache_framebuffer;
    Framebuffer framebuffer = {
        .key = {
            zeta_texture, has_stencil, color_texture
        }
    };
    if ((cache_framebuffer = g_hash_table_lookup(pg->cache.framebuffer,
                                                 &framebuffer))) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,
                             cache_framebuffer->gl_framebuffer);
return_framebuffer:
        debugger_message("%s from cache = %d", __FUNCTION__, cache_framebuffer->gl_framebuffer);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
                == GL_FRAMEBUFFER_COMPLETE_EXT);
        return cache_framebuffer;
    }

    cache_framebuffer = g_memdup(&framebuffer, sizeof(framebuffer));

    glGenFramebuffersEXT(1, &cache_framebuffer->gl_framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, cache_framebuffer->gl_framebuffer);

    if (cache_framebuffer->key.zeta_texture != NULL) {
        //FIXME:
        // - Find a format compatible backbuffer:
        //   - A cubemap face
        //   - A texture 2D
        // - Create a new one if none was found
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                                  GL_DEPTH_ATTACHMENT_EXT,
                                  GL_TEXTURE_2D,
                                  cache_framebuffer->key.zeta_texture->gl_texture,
                                  0);
        assert(glGetError() == 0);
        if (cache_framebuffer->key.has_stencil) {
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                                      GL_STENCIL_ATTACHMENT_EXT,
                                      GL_TEXTURE_2D,
                                      cache_framebuffer->key.zeta_texture->gl_texture,
                                      0);
            assert(glGetError() == 0);
        }
    }
    if (cache_framebuffer->key.color_texture != NULL) {
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                                  GL_COLOR_ATTACHMENT0_EXT,
                                  GL_TEXTURE_2D,
                                  cache_framebuffer->key.color_texture->gl_texture,
                                  0);
        assert(glGetError() == 0);
    }

    debugger_message("%s creating entry = %d", __FUNCTION__, cache_framebuffer->gl_framebuffer);

    g_hash_table_add(pg->cache.framebuffer, cache_framebuffer);
    goto return_framebuffer;
}

#if 0

guint vertexshader_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct VertexshaderKey), 0);
}

gboolean vertexshader_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct VertexshaderKey)) == 0;
}

Vertexshader* create_vertexshader(PGRAPHState* pg)
{

    Vertexshader* cache_vertexshader;
    Vertexshader vertexshader = {
        .key = {
            //FIXME
        }
    };
    if ((cache_vertexshader = g_hash_table_lookup(pg->cache.vertexshader,
                                                  &vertexshader))) {
        return cache_vertexshader;
    }

    cache_vertexshader = g_memdup(&vertexshader, sizeof(vertexshader));

    create_shader();

    g_hash_table_add(pg->cache.vertexshader, cache_vertexshader);
    return cache_vertexshader;
}

guint ffp_vertexshader_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct FFPVertexshaderKey), 0);
}

gboolean ffp_vertexshader_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct FFPVertexshaderKey)) == 0;
}

FFPVertexshader* create_ffp_vertexshader(PGRAPHState* pg)
{

    FFPVertexshader* cache_ffp_vertexshader;
    FFPVertexshader ffp_vertexshader = {
        .key = {
            //FIXME
        }
    };
    if ((cache_ffp_vertexshader = g_hash_table_lookup(pg->cache.ffp_vertexshader,
                                                      &ffp_vertexshader))) {
        return cache_ffp_vertexshader;
    }

    cache_ffp_vertexshader = g_memdup(&ffp_vertexshader, sizeof(ffp_vertexshader));

    create_shader();

    g_hash_table_add(pg->cache.ffp_vertexshader, cache_ffp_vertexshader);
    return cache_ffp_vertexshader;
}

guint fragmentshader_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct FragmentshaderKey), 0);
}

gboolean fragmentshader_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct FragmentshaderKey)) == 0;
}

Fragmentshader* create_fragmentshader(PGRAPHState* pg)
{

    Fragmentshader* cache_fragmentshader;
    Fragmentshader fragmentshader = {
        .key = {
            //FIXME
        }
    };
    if ((cache_fragmentshader = g_hash_table_lookup(pg->cache.fragmentshader,
                                                  &fragmentshader))) {
        return cache_fragmentshader;
    }

    cache_fragmentshader = g_memdup(&fragmentshader, sizeof(fragmentshader));

    create_shader();

    g_hash_table_add(pg->cache.fragmentshader, cache_fragmentshader);
    return cache_fragmentshader;
}

guint shaderprogram_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct ShaderprogramKey), 0);
}

gboolean shaderprogram_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct ShaderprogramKey)) == 0;
}

Shaderprogram* create_shaderprogram(PGRAPHState* pg, GLuint vertexshader, GLuint fragmentshader)
{

    Shaderprogram* cache_shaderprogram;
    Shaderprogram shaderprogram = {
        .key = {
            vertexshader, fragmentshader
        }
    };
    if ((cache_shaderprogram = g_hash_table_lookup(pg->cache.shaderprogram,
                                                   &shaderprogram))) {
        return cache_shaderprogram;
    }

    cache_shaderprogram = g_memdup(&shaderprogram, sizeof(shaderprogram));

    cache_shaderprogram->program = glCreateProgram();

    if (cache_shaderprogram->key.vertexshader != 0) {
        glAttachShader(program, cache_shaderprogram->key.vertexshader);
    }
    if (cache_shaderprogram->key.fragmentshader != 0) {
        glAttachShader(program, cache_shaderprogram->key.fragmentshader);
    }

    g_hash_table_add(pg->cache.shaderprogram, cache_shaderprogram);
    return cache_shaderprogram;
}

#endif

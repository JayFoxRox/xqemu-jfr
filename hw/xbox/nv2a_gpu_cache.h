//FIXME: License and move to nv2a_gpu_cache.c

typedef struct {
    struct RenderbufferKey {
        GLint width, height;
        GLenum format;
    } key;
    GLuint renderbuffer;
} Renderbuffer;

typedef struct {
    struct FramebufferKey {
        GLuint depth_renderbuffer, stencil_renderbuffer, color_renderbuffer;
    } key;
    GLuint framebuffer;
} Framebuffer;

guint renderbuffer_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct RenderbufferKey), 0);
}

gboolean renderbuffer_equal(gconstpointer a, gconstpointer b)
{
#if 0
    const Renderbuffer *as = a, *bs = b;
    /* For final we don't care if there are unused buffers attached */
    //FIXME: Use debug version if it's faster!
    if ((as->depth_renderbuffer != 0) &&
        (as->depth_renderbuffer != bs->depth_renderbuffer)) {
        return false;
    }
    if ((as->stencil_renderbuffer != 0) &&
        (as->stencil_renderbuffer != bs->stencil_renderbuffer)) {
        return false;
    }
    if ((as->color_renderbuffer != 0) && 
        (as->color_renderbuffer != bs->color_renderbuffer)) {
        return false;
    }
    return true;
#else
    /* For debugging we want to make sure we only bind used surfaces */
    return memcmp(a, b, sizeof(struct RenderbufferKey)) == 0;
#endif
}

Renderbuffer* bind_renderbuffer(PGRAPHState* pg, GLint width, GLint height, GLenum format)
{

    Renderbuffer* cache_renderbuffer;
    Renderbuffer renderbuffer = {
        .key = {
            width, height,
            format
        }
    };
    if ((cache_renderbuffer = g_hash_table_lookup(pg->cache.renderbuffer,
                                                  &renderbuffer))) {
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,
                              cache_renderbuffer->renderbuffer);
        return cache_renderbuffer;
    }

    cache_renderbuffer = g_memdup(&renderbuffer, sizeof(renderbuffer));

    glGenRenderbuffersEXT(1, &cache_renderbuffer->renderbuffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,
                          cache_renderbuffer->renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,
                             cache_renderbuffer->key.format,
                             cache_renderbuffer->key.width,
                             cache_renderbuffer->key.height);
    printf("Created new renderbuffer format 0x%x, %dx%d\n",
           cache_renderbuffer->key.format,
           cache_renderbuffer->key.width,
           cache_renderbuffer->key.height);

    g_hash_table_add(pg->cache.renderbuffer, cache_renderbuffer);
    return cache_renderbuffer;
}

gboolean framebuffer_equal(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(struct FramebufferKey)) == 0;
}

guint framebuffer_hash(gconstpointer key)
{
    return XXH32(key, sizeof(struct FramebufferKey), 0);
}

//FIXME: Return a state object instead?
Framebuffer* bind_framebuffer(PGRAPHState* pg, GLuint depth_renderbuffer, GLuint stencil_renderbuffer, GLuint color_renderbuffer)
{

    Framebuffer* cache_framebuffer;
    Framebuffer framebuffer = {
        .key = {
            depth_renderbuffer, stencil_renderbuffer, color_renderbuffer
        }
    };
    if ((cache_framebuffer = g_hash_table_lookup(pg->cache.framebuffer,
                                                 &framebuffer))) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,
                             cache_framebuffer->framebuffer);
        return cache_framebuffer;
    }

    cache_framebuffer = g_memdup(&framebuffer, sizeof(framebuffer));

    glGenFramebuffersEXT(1, &cache_framebuffer->framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, cache_framebuffer->framebuffer);

    if (cache_framebuffer->key.depth_renderbuffer != 0) {
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                     GL_DEPTH_ATTACHMENT_EXT,
                                     GL_RENDERBUFFER_EXT,
                                     cache_framebuffer->key.depth_renderbuffer);
    }
    if (cache_framebuffer->key.stencil_renderbuffer != 0) {
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                     GL_STENCIL_ATTACHMENT_EXT,
                                     GL_RENDERBUFFER_EXT,
                                     cache_framebuffer->key.stencil_renderbuffer);
    }
    if (cache_framebuffer->key.color_renderbuffer != 0) {
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
                                     GL_COLOR_ATTACHMENT0_EXT,
                                     GL_RENDERBUFFER_EXT,
                                     cache_framebuffer->key.color_renderbuffer);
    }

    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
            == GL_FRAMEBUFFER_COMPLETE_EXT);

    g_hash_table_add(pg->cache.framebuffer, cache_framebuffer);
    return cache_framebuffer;
}

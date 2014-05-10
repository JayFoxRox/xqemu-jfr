/*
 *  Offscreen OpenGL abstraction layer
 *
 *  Copyright (c) 2010 Intel
 *  Written by:
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
 *  Copyright (c) 2013 Wayo
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

#ifndef GLOFFSCREEN_H_
#define GLOFFSCREEN_H_

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* Used to hold data for the OpenGL context */
struct _GloContext;
typedef struct _GloContext GloContext;

/* Format flags for glo_surface_create */
#define GLO_FF_ALPHA_MASK  (0x0001)
#define GLO_FF_NOALPHA     (0x0000)
#define GLO_FF_ALPHA       (0x0001)

#define GLO_FF_BITS_MASK   (0x00F0)
#define GLO_FF_BITS_16     (0x0020)
#define GLO_FF_BITS_24     (0x0030)
#define GLO_FF_BITS_32     (0x0040)

#define GLO_FF_DEPTH_MASK   (0x0F00)
#define GLO_FF_DEPTH_16     (0x0100)
#define GLO_FF_DEPTH_24     (0x0200)
#define GLO_FF_DEPTH_32     (0x0300)

#define GLO_FF_STENCIL_MASK   (0xF000)
#define GLO_FF_STENCIL_8      (0x1000)

/* The only currently supported format */
#define GLO_FF_DEFAULT (GLO_FF_BITS_24|GLO_FF_DEPTH_24)

/* Change current context */
extern void glo_set_current(GloContext *context);

/* Check and get GL Extensions */
extern GLboolean glo_check_extension(
    const GLubyte *extName, const GLubyte *extString);
void* glo_get_extension_proc(const GLubyte *extProc);

/* Create an OpenGL context for a certain
 * pixel format. formatflags are from the
 * GLO_ constants */
extern GloContext *glo_context_create(int formatFlags);

/* Destroy a previouslu created OpenGL context */
extern void glo_context_destroy(GloContext *context);

/* Functions to decode the format flags */
extern int glo_flags_get_depth_bits(int formatFlags);
extern int glo_flags_get_stencil_bits(int formatFlags);
extern void glo_flags_get_rgba_bits(int formatFlags, int *rgba);
extern int glo_flags_get_bytes_per_pixel(int formatFlags);
/* Score how close the given format flags match. 0=great, >0 not so great */
extern int glo_flags_score(int formatFlagsExpected, int formatFlagsReal);

 /* Note that this is top-down, not bottom-up as glReadPixels would do. */
extern void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, void *data);
 
#endif /* GLOFFSCREEN_H_ */

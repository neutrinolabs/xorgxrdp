/*
Copyright 2018-2019 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

GLSL
EGL

*/

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include <glamor.h>

#include <gbm.h>
#include <drm_fourcc.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpMisc.h"
#include "rdpEgl.h"
#include "rdpReg.h"

struct rdp_egl
{
    GLuint quad_vao[1];
    GLuint quad_vbo[1];
    GLuint vertex_shader[2];
    GLuint fragment_shader[2];
    GLuint program[2];
    GLuint fb[1];
    GLint copy_tex_loc;
    GLint copy_tex_size_loc;
};

static const GLfloat g_vertices[] =
{
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};

static const GLchar g_vs[] =
"\
#version 330 core\n\
layout (location = 0) in vec2 position;\n\
void main(void)\n\
{\n\
    gl_Position = vec4(position.xy, 0.0, 1.0);\n\
}\n";
static const GLchar g_fs_copy[] =
"\
#version 330 core\n\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main()\n\
{\n\
    gl_FragColor = texture(tex, gl_FragCoord.xy / tex_size);\n\
}\n";

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/******************************************************************************/
void *
rdpEglCreate(ScreenPtr screen)
{
    struct rdp_egl *egl;
    GLint old_vertex_array;
    const GLchar *vsource;
    const GLchar *fsource;
    GLint vlength;
    GLint flength;
    GLint linked;
    GLint compiled;

    egl = g_new0(struct rdp_egl, 1);
    /* create vertex array */
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glGenVertexArrays(1, egl->quad_vao);
    glBindVertexArray(egl->quad_vao[0]);
    glGenBuffers(1, egl->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, egl->quad_vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(old_vertex_array);
    glGenFramebuffers(1, egl->fb);
    /* create copy shader */
    vsource = g_vs;
    fsource = g_fs_copy;
    egl->vertex_shader[0] = glCreateShader(GL_VERTEX_SHADER);
    egl->fragment_shader[0] = glCreateShader(GL_FRAGMENT_SHADER);
    vlength = strlen(vsource);
    flength = strlen(fsource);
    glShaderSource(egl->vertex_shader[0], 1, &vsource, &vlength);
    glShaderSource(egl->fragment_shader[0], 1, &fsource, &flength);
    glCompileShader(egl->vertex_shader[0]);
    glGetShaderiv(egl->vertex_shader[0], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: vertex_shader compiled %d", compiled));
    glCompileShader(egl->fragment_shader[0]);
    glGetShaderiv(egl->fragment_shader[0], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: fragment_shader compiled %d", compiled));
    egl->program[0] = glCreateProgram();
    glAttachShader(egl->program[0], egl->vertex_shader[0]);
    glAttachShader(egl->program[0], egl->fragment_shader[0]);
    glLinkProgram(egl->program[0]);
    glGetProgramiv(egl->program[0], GL_LINK_STATUS, &linked);
    LLOGLN(0, ("rdpEglCreate: linked %d", linked));
    egl->copy_tex_loc = glGetUniformLocation(egl->program[0], "tex");
    egl->copy_tex_size_loc = glGetUniformLocation(egl->program[0], "tex_size");
    LLOGLN(0, ("rdpEglCreate: copy_tex_loc %d copy_tex_size_loc %d",
           egl->copy_tex_loc, egl->copy_tex_size_loc));
    return egl;
}

/******************************************************************************/
int
rdpEglDestroy(void *eglptr)
{
    struct rdp_egl *egl;

    egl = (struct rdp_egl *) eglptr;
    if (egl == NULL)
    {
        return 0;
    }
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxYuvToRgb(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex)
{
    return 0;
}

/******************************************************************************/
static int
rdpEglOut(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr out_rects,
          int *num_out_rects, struct image_data *id, uint32_t tex,
          BoxPtr prect)
{
    GLuint fb[1];
    int x;
    int y;
    int lx;
    int ly;
    int dst_stride;
    int rcode;
    int out_rect_index;
    BoxRec rect;
    uint8_t *dst;
    int *pixels;

    glGenFramebuffers(1, fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    dst = id->shmem_pixels;
    dst_stride = clientCon->cap_stride_bytes;
    out_rect_index = 0;

    y = prect->y1;
    while (y < prect->y2)
    {
        x = prect->x1;
        while (x < prect->x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            LLOGLN(10, ("rdpEglOut: x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.y1, rect.x2, rect.y2));
            rcode = rdpRegionContainsRect(in_reg, &rect);
            if (rcode != rgnOUT)
            {
                pixels = (int *) (dst + (y << 8) * (dst_stride >> 8) + (x << 8));
                lx = x - prect->x1;
                ly = y - prect->y1;
                if (rcode == rgnPART)
                {
                    glReadPixels(lx, ly, 64, 64, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
                }
                else /* rgnIN */
                {
                    glReadPixels(lx, ly, 64, 64, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
                }
            }
            out_rects[out_rect_index] = rect;
            out_rect_index++;
            x += 64;
        }
        y += 64;
    }

    *num_out_rects = out_rect_index;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, fb);
    return 0;
}

/******************************************************************************/
Bool
rdpEglCaptureRfx(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    int width;
    int height;
    uint32_t tex;
    uint32_t yuv_tex;
    BoxRec extents_rect;
    BoxRec extents_rect1;
    ScreenPtr pScreen;
    PixmapPtr screen_pixmap;
    PixmapPtr pixmap;
    GCPtr copyGC;
    ChangeGCVal tmpval[1];
    rdpPtr dev;
    struct rdp_egl *egl;

    dev = clientCon->dev;
    pScreen = dev->pScreen;
    egl = (struct rdp_egl *) (dev->egl);
    screen_pixmap = pScreen->GetScreenPixmap(pScreen);
    if (screen_pixmap == NULL)
    {
        return FALSE;
    }
    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }
    extents_rect = *rdpRegionExtents(in_reg);
    extents_rect1.x1 = extents_rect.x1 & ~63;
    extents_rect1.y1 = extents_rect.y1 & ~63;
    extents_rect1.x2 = (extents_rect.x2 + 63) & ~63;
    extents_rect1.y2 = (extents_rect.y2 + 63) & ~63;
    width = extents_rect1.x2 - extents_rect1.x1;
    height = extents_rect1.y2 - extents_rect1.y1;
    LLOGLN(0, ("rdpEglCaptureRfx: width %d height %d", width, height));
    copyGC = GetScratchGC(dev->depth, pScreen);
    if (copyGC != NULL)
    {
        tmpval[0].val = GXcopy;
        ChangeGC(NullClient, copyGC, GCFunction, tmpval);
        pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                       pScreen->rootDepth,
                                       GLAMOR_CREATE_NO_LARGE);
        if (pixmap != NULL)
        {
            copyGC->ops->CopyArea(&(screen_pixmap->drawable),
                                  &(pixmap->drawable), copyGC,
                                  extents_rect1.x1, extents_rect1.y1,
                                  width, height, 0, 0);
            tex = glamor_get_pixmap_texture(pixmap);
            glGenTextures(1, &yuv_tex);
            LLOGLN(0, ("rdpEglCaptureRfx: tex 0x%8.8x yuv_tex 0x%8.8x", tex, yuv_tex));
            rdpEglRfxYuvToRgb(egl, tex, yuv_tex);
            rdpEglOut(clientCon, in_reg, *out_rects, num_out_rects, id, tex, &extents_rect1);
            glDeleteTextures(1, &yuv_tex);
            pScreen->DestroyPixmap(pixmap);
        }
        else
        {
            LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
        }
        FreeScratchGC(copyGC);
    }
    else
    {
        LLOGLN(0, ("rdpEglCaptureRfx: GetScratchGC failed"));
    }
    return TRUE;
}

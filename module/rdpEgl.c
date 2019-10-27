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
    GLint yuv_tex_loc;
    GLint yuv_tex_size_loc;
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
static const GLchar g_fs_rfx_rgb_to_yuv[] =
"\
#version 330 core\n\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
void main()\n\
{\n\
    vec4 ymath;\n\
    vec4 umath;\n\
    vec4 vmath;\n\
    vec4 pixel;\n\
    vec4 pixel1;\n\
    vec4 pixel2;\n\
    ymath = vec4( 0.299000,  0.587000,  0.114000, 1.0);\n\
    umath = vec4(-0.168935, -0.331665,  0.500590, 1.0);\n\
    vmath = vec4( 0.499813, -0.418531, -0.081282, 1.0);\n\
    pixel = texture(tex, gl_FragCoord.xy / tex_size);\n\
    ymath = ymath * pixel;\n\
    umath = umath * pixel;\n\
    vmath = vmath * pixel;\n\
    pixel1 = vec4(ymath.r + ymath.g + ymath.b,\n\
                  umath.r + umath.g + umath.b + 0.5,\n\
                  vmath.r + vmath.g + vmath.b + 0.5,\n\
                  pixel.a);\n\
    pixel2 = clamp(pixel1, 0.0, 1.0);\n\
    gl_FragColor = pixel2;\n\
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
    /* create yuv shader */
    vsource = g_vs;
    fsource = g_fs_rfx_rgb_to_yuv;
    egl->vertex_shader[1] = glCreateShader(GL_VERTEX_SHADER);
    egl->fragment_shader[1] = glCreateShader(GL_FRAGMENT_SHADER);
    vlength = strlen(vsource);
    flength = strlen(fsource);
    glShaderSource(egl->vertex_shader[1], 1, &vsource, &vlength);
    glShaderSource(egl->fragment_shader[1], 1, &fsource, &flength);
    glCompileShader(egl->vertex_shader[1]);
    glGetShaderiv(egl->vertex_shader[1], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: vertex_shader compiled %d", compiled));
    glCompileShader(egl->fragment_shader[1]);
    glGetShaderiv(egl->fragment_shader[1], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: fragment_shader compiled %d", compiled));
    egl->program[1] = glCreateProgram();
    glAttachShader(egl->program[1], egl->vertex_shader[1]);
    glAttachShader(egl->program[1], egl->fragment_shader[1]);
    glLinkProgram(egl->program[1]);
    glGetProgramiv(egl->program[1], GL_LINK_STATUS, &linked);
    LLOGLN(0, ("rdpEglCreate: linked %d", linked));
    egl->yuv_tex_loc = glGetUniformLocation(egl->program[1], "tex");
    egl->yuv_tex_size_loc = glGetUniformLocation(egl->program[1], "tex_size");
    LLOGLN(0, ("rdpEglCreate: yuv_tex_loc %d yuv_tex_size_loc %d",
           egl->yuv_tex_loc, egl->yuv_tex_size_loc));
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
rdpEglRfxRgbToYuv(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
                  GLint dst_width, GLint dst_height)
{
    GLint old_vertex_array;
    int status;

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxYuvToRgb: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, dst_width, dst_height);
    glUseProgram(egl->program[1]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->yuv_tex_loc, 0);
    glUniform2f(egl->yuv_tex_size_loc, dst_width, dst_height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpCopyBox_ayuv_to_yuvalp(int dstx, int dsty, int srcx, int srcy,
                          const uint8_t *src, int src_stride,
                          uint8_t *dst, int dst_stride,
                          BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    uint8_t *yptr;
    uint8_t *uptr;
    uint8_t *vptr;
    uint8_t *aptr;
    const uint32_t *s32;
    int index;
    int jndex;
    int kndex;
    int width;
    int height;
    uint32_t pixel;
    uint8_t a;
    int y;
    int u;
    int v;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * 64;
        d8 += box->x1 - dstx;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        for (jndex = 0; jndex < height; jndex++)
        {
            s32 = (const uint32_t *) s8;
            yptr = d8;
            uptr = yptr + 64 * 64;
            vptr = uptr + 64 * 64;
            aptr = vptr + 64 * 64;
            kndex = 0;
            while (kndex < width)
            {
                pixel = *(s32++);
                a = (pixel >> 24) & 0xff;
                y = (pixel >> 16) & 0xff;
                u = (pixel >>  8) & 0xff;
                v = (pixel >>  0) & 0xff;
                *(yptr++) = y;
                *(uptr++) = u;
                *(vptr++) = v;
                *(aptr++) = a;
                kndex++;
            }
            d8 += dst_stride;
            s8 += src_stride;
        }
    }
    return 0;
}

/******************************************************************************/
static int
rdpEglOut(rdpClientCon *clientCon, struct rdp_egl *egl, RegionPtr in_reg,
          BoxPtr out_rects, int *num_out_rects, struct image_data *id,
          uint32_t tex, BoxPtr tile_extents_rect)
{
    int x;
    int y;
    int lx;
    int ly;
    int dst_stride;
    int rcode;
    int out_rect_index;
    int status;
    int num_rects;
    BoxRec rect;
    uint8_t *dst;
    uint8_t *tile_dst;
    void* pixel_data;
    int crc_offset;
    int crc_stride;
    int crc;
    int num_crcs;
    RegionRec tile_reg;
    BoxPtr rects;

    pixel_data = malloc(64 * 64 * 4);
    if (pixel_data == NULL)
    {
        return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglOut: glCheckFramebufferStatus error"));
    }
    dst = id->shmem_pixels;
    dst_stride = clientCon->cap_stride_bytes;

    crc_stride = (clientCon->dev->width + 63) / 64;
    num_crcs = crc_stride * ((clientCon->dev->height + 63) / 64);
    if (num_crcs != clientCon->num_rfx_crcs_alloc)
    {
        /* resize the crc list */
        clientCon->num_rfx_crcs_alloc = num_crcs;
        free(clientCon->rfx_crcs);
        clientCon->rfx_crcs = g_new0(int, num_crcs);
    }

    out_rect_index = 0;
    y = tile_extents_rect->y1;
    while (y < tile_extents_rect->y2)
    {
        x = tile_extents_rect->x1;
        while (x < tile_extents_rect->x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            LLOGLN(10, ("rdpEglOut: x1 %d y1 %d x2 %d y2 %d",
                   rect.x1, rect.y1, rect.x2, rect.y2));
            rcode = rdpRegionContainsRect(in_reg, &rect);
            if (rcode != rgnOUT)
            {
                crc = crc_start();
                lx = x - tile_extents_rect->x1;
                ly = y - tile_extents_rect->y1;
                tile_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
                LLOGLN(10, ("rdpEglOut: lx %d ly %d", lx, ly));
                if (rcode == rgnPART)
                {
                    memset(tile_dst, 0, 64 * 64 * 4);
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionIntersect(&tile_reg, in_reg, &tile_reg);
                    rects = REGION_RECTS(&tile_reg);
                    num_rects = REGION_NUM_RECTS(&tile_reg);
                    crc = crc_process_data(crc, rects,
                                           num_rects * sizeof(BoxRec));
                    glReadPixels(lx, ly, 64, 64, GL_BGRA,
                                 GL_UNSIGNED_INT_8_8_8_8_REV, pixel_data);
                    rdpCopyBox_ayuv_to_yuvalp(x, y, x, y, pixel_data, 64 * 4,
                                              tile_dst, 64, rects, num_rects);
                    rdpRegionUninit(&tile_reg);
                }
                else /* rgnIN */
                {
                    glReadPixels(lx, ly, 64, 64, GL_BGRA,
                                 GL_UNSIGNED_INT_8_8_8_8_REV, pixel_data);
                    rdpCopyBox_ayuv_to_yuvalp(x, y, x, y, pixel_data, 64 * 4,
                                              tile_dst, 64, &rect, 1);
                }
                crc = crc_process_data(crc, tile_dst, 64 * 64 * 4);
                crc = crc_end(crc);
                crc_offset = (y / 64) * crc_stride + (x / 64);
                LLOGLN(10, ("rdpEglOut: crc 0x%8.8x 0x%8.8x",
                       crc, clientCon->rfx_crcs[crc_offset]));
                if (crc == clientCon->rfx_crcs[crc_offset])
                {
                    LLOGLN(10, ("rdpEglOut: crc skip at x %d y %d", x, y));
                }
                else
                {
                    clientCon->rfx_crcs[crc_offset] = crc;
                    out_rects[out_rect_index] = rect;
                    if (out_rect_index < RDP_MAX_TILES)
                    {
                        out_rect_index++;
                    }
                    else
                    {
                        LLOGLN(0, ("rdpEglOut: too many out rects %d",
                               out_rect_index));
                    }
                }

            }
            x += 64;
        }
        y += 64;
    }
    *num_out_rects = out_rect_index;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    free(pixel_data);
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
    BoxRec tile_extents_rect;
    ScreenPtr pScreen;
    PixmapPtr screen_pixmap;
    PixmapPtr pixmap;
    PixmapPtr yuv_pixmap;
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
    tile_extents_rect.x1 = extents_rect.x1 & ~63;
    tile_extents_rect.y1 = extents_rect.y1 & ~63;
    tile_extents_rect.x2 = (extents_rect.x2 + 63) & ~63;
    tile_extents_rect.y2 = (extents_rect.y2 + 63) & ~63;
    width = tile_extents_rect.x2 - tile_extents_rect.x1;
    height = tile_extents_rect.y2 - tile_extents_rect.y1;
    LLOGLN(10, ("rdpEglCaptureRfx: width %d height %d", width, height));
    copyGC = GetScratchGC(dev->depth, pScreen);
    if (copyGC != NULL)
    {
        tmpval[0].val = GXcopy;
        ChangeGC(NullClient, copyGC, GCFunction, tmpval);
        ValidateGC(&(screen_pixmap->drawable), copyGC);
        pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                       pScreen->rootDepth,
                                       GLAMOR_CREATE_NO_LARGE);
        if (pixmap != NULL)
        {
            tex = glamor_get_pixmap_texture(pixmap);
            yuv_pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                               pScreen->rootDepth,
                                               GLAMOR_CREATE_NO_LARGE);
            if (yuv_pixmap != NULL)
            {
                yuv_tex = glamor_get_pixmap_texture(yuv_pixmap);
                copyGC->ops->CopyArea(&(screen_pixmap->drawable),
                                      &(pixmap->drawable), copyGC,
                                      tile_extents_rect.x1,
                                      tile_extents_rect.y1,
                                      width, height, 0, 0);
                LLOGLN(10, ("rdpEglCaptureRfx: tex 0x%8.8x yuv_tex 0x%8.8x",
                       tex, yuv_tex));
                rdpEglRfxRgbToYuv(egl, tex, yuv_tex, width, height);
                rdpEglOut(clientCon, egl, in_reg, *out_rects, num_out_rects,
                          id, yuv_tex, &tile_extents_rect);
                pScreen->DestroyPixmap(yuv_pixmap);
            }
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

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
Bool
rdpEglCaptureRfx(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    int width;
    int height;
    int out_rect_index;
    uint32_t tex;
    BoxRec extents_rect;
    BoxRec extents_rect1;
    ScreenPtr pScreen;
    PixmapPtr screen_pixmap;
    PixmapPtr pixmap;
    GCPtr copyGC;
    ChangeGCVal tmpval[1];

    pScreen = clientCon->dev->pScreen;
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
    out_rect_index = 0;
    extents_rect = *rdpRegionExtents(in_reg);
    extents_rect1.x1 = extents_rect.x1 & ~63;
    extents_rect1.y1 = extents_rect.y1 & ~63;
    extents_rect1.x2 = (extents_rect.x2 + 63) & ~63;
    extents_rect1.y2 = (extents_rect.y2 + 63) & ~63;
    width = extents_rect1.x2 - extents_rect1.x1;
    height = extents_rect1.y2 - extents_rect1.y1;
    LLOGLN(0, ("rdpEglCaptureRfx: width %d height %d", width, height));
    copyGC = GetScratchGC(clientCon->dev->depth, pScreen);
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
            LLOGLN(0, ("rdpEglCaptureRfx: tex 0x%8.8x", tex));
            pScreen->DestroyPixmap(pixmap);
        }
        else
        {
            LLOGLN(0, ("rdpEglCaptureRfx: GetScratchGC failed"));
        }
        FreeScratchGC(copyGC);
    }
    else
    {
        LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
    }
    *num_out_rects = out_rect_index;
    return TRUE;
}

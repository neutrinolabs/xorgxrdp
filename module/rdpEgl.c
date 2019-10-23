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
#include "rdpMisc.h"
#include "rdpEgl.h"

struct rdp_egl
{
    GLuint quad_vao[1];
    GLuint quad_vbo[1];
    GLuint vertex_shader[2];
    GLuint fragment_shader[2];
    GLuint program[2];
    GLuint fb[1];
    GLint crc_tex_loc;
    GLint crc_tex_size_loc;
    GLint crc_num_coords_loc;
    GLint crc_coords_loc;
    GLint copy_tex_loc;
    GLint copy_tex_size_loc;
};

/******************************************************************************/
void *
rdpEglCreate(ScreenPtr screen)
{
    struct rdp_egl *egl;

    egl = g_new0(struct rdp_egl, 1);
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

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

#define XRDP_CRC_CHECK 0

struct rdp_egl
{
    GLuint quad_vao[1];
    GLuint quad_vbo[1];
    GLuint vertex_shader[4];
    GLuint fragment_shader[4];
    GLuint program[4];
    GLuint fb[1];
    GLint tex_loc[4];
    GLint tex_size_loc[4];
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
static const GLchar g_fs_rfx_yuv_to_yuvlp[] =
"\
#version 330 core\n\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
vec4 getpixel(int x1, int y1, int offset)\n\
{\n\
    int x;\n\
    int y;\n\
    vec2 xy;\n\
    x = x1 + offset % 64;\n\
    y = y1 + offset / 64;\n\
    xy.x = x + 0.5;\n\
    xy.y = y + 0.5;\n\
    return texture(tex, xy / tex_size);\n\
}\n\
void main()\n\
{\n\
    int x;\n\
    int y;\n\
    int x1;\n\
    int y1;\n\
    int x2;\n\
    int y2;\n\
    int offset;\n\
    vec4 pixel1;\n\
    x = int(gl_FragCoord.x);\n\
    y = int(gl_FragCoord.y);\n\
    x1 = x & ~63;\n\
    y1 = y & ~63;\n\
    x2 = x - x1;\n\
    y2 = y - y1;\n\
    offset = y2 * 64 + x2;\n\
    if (offset < 1024)\n\
    {\n\
        pixel1.b = getpixel(x1, y1, offset * 4 + 0).r;\n\
        pixel1.g = getpixel(x1, y1, offset * 4 + 1).r;\n\
        pixel1.r = getpixel(x1, y1, offset * 4 + 2).r;\n\
        pixel1.a = getpixel(x1, y1, offset * 4 + 3).r;\n\
    }\n\
    else if (offset < 2048)\n\
    {\n\
        offset -= 1024;\n\
        pixel1.b = getpixel(x1, y1, offset * 4 + 0).g;\n\
        pixel1.g = getpixel(x1, y1, offset * 4 + 1).g;\n\
        pixel1.r = getpixel(x1, y1, offset * 4 + 2).g;\n\
        pixel1.a = getpixel(x1, y1, offset * 4 + 3).g;\n\
    }\n\
    else if (offset < 3072)\n\
    {\n\
        offset -= 2048;\n\
        pixel1.b = getpixel(x1, y1, offset * 4 + 0).b;\n\
        pixel1.g = getpixel(x1, y1, offset * 4 + 1).b;\n\
        pixel1.r = getpixel(x1, y1, offset * 4 + 2).b;\n\
        pixel1.a = getpixel(x1, y1, offset * 4 + 3).b;\n\
    }\n\
    else\n\
    {\n\
        offset -= 3072;\n\
        pixel1.b = getpixel(x1, y1, offset * 4 + 0).a;\n\
        pixel1.g = getpixel(x1, y1, offset * 4 + 1).a;\n\
        pixel1.r = getpixel(x1, y1, offset * 4 + 2).a;\n\
        pixel1.a = getpixel(x1, y1, offset * 4 + 3).a;\n\
    }\n\
    gl_FragColor = pixel1;\n\
}\n";
static const GLchar g_fs_rfx_crc[] =
"\
#version 330 core\n\
uniform sampler2D tex;\n\
uniform vec2 tex_size;\n\
const int g_crc_table[256] = int[256](\n\
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,\n\
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,\n\
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,\n\
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,\n\
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,\n\
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,\n\
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,\n\
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,\n\
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,\n\
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,\n\
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,\n\
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,\n\
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,\n\
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,\n\
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,\n\
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,\n\
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,\n\
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,\n\
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,\n\
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,\n\
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,\n\
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,\n\
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,\n\
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,\n\
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,\n\
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,\n\
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,\n\
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,\n\
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,\n\
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,\n\
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,\n\
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,\n\
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,\n\
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,\n\
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,\n\
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,\n\
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,\n\
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,\n\
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,\n\
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,\n\
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,\n\
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,\n\
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d);\n\
#define CRC_START(in_crc) (in_crc) = 0xFFFFFFFF\n\
#define CRC_PASS(in_pixel, in_crc) (in_crc) = g_crc_table[((in_crc) ^ (in_pixel)) & 0xff] ^ ((in_crc) >> 8)\n\
#define CRC_END(in_crc) (in_crc) = ((in_crc) ^ 0xFFFFFFFF)\n\
vec4 getpixel(int x1, int y1, int offset)\n\
{\n\
    int x;\n\
    int y;\n\
    vec2 xy;\n\
    x = x1 + offset % 64;\n\
    y = y1 + offset / 64;\n\
    xy.x = x + 0.5;\n\
    xy.y = y + 0.5;\n\
    return texture(tex, xy / tex_size);\n\
}\n\
void main()\n\
{\n\
    int x;\n\
    int y;\n\
    int x1;\n\
    int y1;\n\
    int index;\n\
    int crc;\n\
    int red;\n\
    int grn;\n\
    int blu;\n\
    int alp;\n\
    vec4 pixel1;\n\
    x = int(gl_FragCoord.x);\n\
    y = int(gl_FragCoord.y);\n\
    x1 = x * 64;\n\
    y1 = y * 64;\n\
    CRC_START(crc);\n\
    for (index = 0; index < 4096; index++)\n\
    {\n\
        pixel1 = getpixel(x1, y1, index);\n\
        blu = clamp(int(pixel1.b * 255.0), 0, 255);\n\
        CRC_PASS(blu, crc);\n\
        grn = clamp(int(pixel1.g * 255.0), 0, 255);\n\
        CRC_PASS(grn, crc);\n\
        red = clamp(int(pixel1.r * 255.0), 0, 255);\n\
        CRC_PASS(red, crc);\n\
        alp = clamp(int(pixel1.a * 255.0), 0, 255);\n\
        CRC_PASS(alp, crc);\n\
    }\n\
    CRC_END(crc);\n\
    gl_FragColor = vec4(((crc >> 16) & 0xFF) / 255.0,\n\
                        ((crc >>  8) & 0xFF) / 255.0,\n\
                        ((crc >>  0) & 0xFF) / 255.0,\n\
                        ((crc >> 24) & 0xFF) / 255.0);\n\
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
    egl->tex_loc[0] = glGetUniformLocation(egl->program[0], "tex");
    egl->tex_size_loc[0] = glGetUniformLocation(egl->program[0], "tex_size");
    LLOGLN(0, ("rdpEglCreate: copy_tex_loc %d copy_tex_size_loc %d",
           egl->tex_loc[0], egl->tex_size_loc[0]));
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
    egl->tex_loc[1] = glGetUniformLocation(egl->program[1], "tex");
    egl->tex_size_loc[1] = glGetUniformLocation(egl->program[1], "tex_size");
    LLOGLN(0, ("rdpEglCreate: yuv_tex_loc %d yuv_tex_size_loc %d",
           egl->tex_loc[1], egl->tex_size_loc[1]));
    /* create yuvlp shader */
    vsource = g_vs;
    fsource = g_fs_rfx_yuv_to_yuvlp;
    egl->vertex_shader[2] = glCreateShader(GL_VERTEX_SHADER);
    egl->fragment_shader[2] = glCreateShader(GL_FRAGMENT_SHADER);
    vlength = strlen(vsource);
    flength = strlen(fsource);
    glShaderSource(egl->vertex_shader[2], 1, &vsource, &vlength);
    glShaderSource(egl->fragment_shader[2], 1, &fsource, &flength);
    glCompileShader(egl->vertex_shader[2]);
    glGetShaderiv(egl->vertex_shader[2], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: vertex_shader compiled %d", compiled));
    glCompileShader(egl->fragment_shader[2]);
    glGetShaderiv(egl->fragment_shader[2], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: fragment_shader compiled %d", compiled));
    egl->program[2] = glCreateProgram();
    glAttachShader(egl->program[2], egl->vertex_shader[2]);
    glAttachShader(egl->program[2], egl->fragment_shader[2]);
    glLinkProgram(egl->program[2]);
    glGetProgramiv(egl->program[2], GL_LINK_STATUS, &linked);
    LLOGLN(0, ("rdpEglCreate: linked %d", linked));
    egl->tex_loc[2] = glGetUniformLocation(egl->program[2], "tex");
    egl->tex_size_loc[2] = glGetUniformLocation(egl->program[2], "tex_size");
    LLOGLN(0, ("rdpEglCreate: yuvlp_tex_loc %d yuvlp_tex_size_loc %d",
           egl->tex_loc[2], egl->tex_size_loc[2]));
    /* create crc shader */
    vsource = g_vs;
    fsource = g_fs_rfx_crc;
    egl->vertex_shader[3] = glCreateShader(GL_VERTEX_SHADER);
    egl->fragment_shader[3] = glCreateShader(GL_FRAGMENT_SHADER);
    vlength = strlen(vsource);
    flength = strlen(fsource);
    glShaderSource(egl->vertex_shader[3], 1, &vsource, &vlength);
    glShaderSource(egl->fragment_shader[3], 1, &fsource, &flength);
    glCompileShader(egl->vertex_shader[3]);
    glGetShaderiv(egl->vertex_shader[3], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: vertex_shader compiled %d", compiled));
    glCompileShader(egl->fragment_shader[3]);
    glGetShaderiv(egl->fragment_shader[3], GL_COMPILE_STATUS, &compiled);
    LLOGLN(0, ("rdpEglCreate: fragment_shader compiled %d", compiled));
    egl->program[3] = glCreateProgram();
    glAttachShader(egl->program[3], egl->vertex_shader[3]);
    glAttachShader(egl->program[3], egl->fragment_shader[3]);
    glLinkProgram(egl->program[3]);
    glGetProgramiv(egl->program[3], GL_LINK_STATUS, &linked);
    LLOGLN(0, ("rdpEglCreate: linked %d", linked));
    egl->tex_loc[3] = glGetUniformLocation(egl->program[3], "tex");
    egl->tex_size_loc[3] = glGetUniformLocation(egl->program[3], "tex_size");
    LLOGLN(0, ("rdpEglCreate: crc_tex_loc %d crc_tex_size_loc %d",
           egl->tex_loc[3], egl->tex_size_loc[3]));
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
                  GLint width, GLint height)
{
    GLint old_vertex_array;
    int status;

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxRgbToYuv: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, width, height);
    glUseProgram(egl->program[1]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[1], 0);
    glUniform2f(egl->tex_size_loc[1], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxYuvToYuvlp(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
                    GLint width, GLint height)
{
    GLint old_vertex_array;
    int status;

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxYuvToYuvlp: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, width, height);
    glUseProgram(egl->program[2]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[2], 0);
    glUniform2f(egl->tex_size_loc[2], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxCrc(struct rdp_egl *egl, GLuint src_tex, GLuint dst_tex,
             GLint width, GLint height, int *crcs)
{
    GLint old_vertex_array;
    int status;
    int w_div_64;
    int h_div_64;

    w_div_64 = width / 64;
    h_div_64 = height / 64;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, dst_tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglRfxCrc: glCheckFramebufferStatus error"));
    }
    glViewport(0, 0, w_div_64, h_div_64);
    glUseProgram(egl->program[3]);
    glBindVertexArray(egl->quad_vao[0]);
    glUniform1i(egl->tex_loc[3], 0);
    glUniform2f(egl->tex_size_loc[3], width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, w_div_64, h_div_64, GL_BGRA,
                 GL_UNSIGNED_INT_8_8_8_8_REV, crcs);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(old_vertex_array);
    return 0;
}

/******************************************************************************/
static int
rdpEglOut(rdpClientCon *clientCon, struct rdp_egl *egl, RegionPtr in_reg,
          BoxPtr out_rects, int *num_out_rects, struct image_data *id,
          uint32_t tex, BoxPtr tile_extents_rect, int *crcs)
{
    int x;
    int y;
    int lx;
    int ly;
    int dst_stride;
    int rcode;
    int out_rect_index;
    int status;
    BoxRec rect;
    RegionRec tile_reg;
    uint8_t *dst;
    uint8_t *tile_dst;
    int crc_offset;
    int crc_stride;
    int crc;
    int num_crcs;
    int tile_extents_stride;
    int mon_index;

    mon_index = (id->flags >> 28) & 0xF;
    glBindFramebuffer(GL_FRAMEBUFFER, egl->fb[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LLOGLN(0, ("rdpEglOut: glCheckFramebufferStatus error"));
    }
    dst = id->shmem_pixels;
    dst_stride = ((id->width + 63) & ~63) * 4;
    /* check crc list size */
    crc_stride = (id->width + 63) / 64;
    num_crcs = crc_stride * ((id->height + 63) / 64);
    if (num_crcs != clientCon->num_rfx_crcs_alloc[mon_index])
    {
        LLOGLN(0, ("rdpEglOut: resize the crc list was %d now %d",
               clientCon->num_rfx_crcs_alloc[mon_index], num_crcs));
        /* resize the crc list */
        clientCon->num_rfx_crcs_alloc[mon_index] = num_crcs;
        free(clientCon->rfx_crcs[mon_index]);
        clientCon->rfx_crcs[mon_index] = g_new0(int, num_crcs);
    }
    tile_extents_stride = (tile_extents_rect->x2 - tile_extents_rect->x1) / 64;
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
            if (rcode == rgnOUT)
            {
                LLOGLN(10, ("rdpEglOut: rgnOUT"));
                rdpRegionInit(&tile_reg, &rect, 0);
                rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                rdpRegionUninit(&tile_reg);
            }
            else
            {
                lx = x - tile_extents_rect->x1;
                ly = y - tile_extents_rect->y1;
                tile_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
#if XRDP_CRC_CHECK
                /* check if the gpu calculated the crcs right */
                glReadPixels(lx, ly, 64, 64, GL_BGRA,
                             GL_UNSIGNED_INT_8_8_8_8_REV, tile_dst);
                crc = crc_start();
                crc = crc_process_data(crc, tile_dst, 64 * 64 * 4);
                crc = crc_end(crc);
                if (crc != crcs[(ly / 64) * tile_extents_stride + (lx / 64)])
                {
                    LLOGLN(0, ("rdpEglOut: error crc no match 0x%8.8x 0x%8.8x",
                           crc,
                           crcs[(ly / 64) * tile_extents_stride + (lx / 64)]));
                }
#endif
                crc = crcs[(ly / 64) * tile_extents_stride + (lx / 64)];
                crc_offset = (y / 64) * crc_stride + (x / 64);
                if (crc == clientCon->rfx_crcs[mon_index][crc_offset])
                {
                    LLOGLN(10, ("rdpEglOut: crc skip at x %d y %d", x, y));
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                    rdpRegionUninit(&tile_reg);
                }
                else
                {
                    glReadPixels(lx, ly, 64, 64, GL_BGRA,
                                 GL_UNSIGNED_INT_8_8_8_8_REV, tile_dst);
                    clientCon->rfx_crcs[mon_index][crc_offset] = crc;
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
            x += XRDP_RFX_ALIGN;
        }
        y += XRDP_RFX_ALIGN;
    }
    *num_out_rects = out_rect_index;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

/******************************************************************************/
static int
rdpEglRfxClear(GCPtr rfxGC, PixmapPtr yuv_pixmap, BoxPtr tile_extents_rect,
               RegionPtr in_reg)
{
    RegionPtr reg;
    xRectangle rect;

    reg = rdpRegionCreate(tile_extents_rect, 0);
    rdpRegionSubtract(reg, reg, in_reg);
    rdpRegionTranslate(reg, -tile_extents_rect->x1, -tile_extents_rect->y1);
    /* rfxGC takes ownership of reg */
    rfxGC->funcs->ChangeClip(rfxGC, CT_REGION, reg, 0);
    rect.x = 0;
    rect.y = 0;
    rect.width = tile_extents_rect->x2 - tile_extents_rect->x1;
    rect.height = tile_extents_rect->y2 - tile_extents_rect->y1;
    rfxGC->ops->PolyFillRect(&(yuv_pixmap->drawable), rfxGC, 1, &rect);
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
    uint32_t crc_tex;
    BoxRec extents_rect;
    BoxRec tile_extents_rect;
    ScreenPtr pScreen;
    PixmapPtr screen_pixmap;
    PixmapPtr pixmap;
    PixmapPtr yuv_pixmap;
    PixmapPtr crc_pixmap;
    GCPtr rfxGC;
    ChangeGCVal tmpval[2];
    rdpPtr dev;
    struct rdp_egl *egl;
    int *crcs;

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

    rdpRegionTranslate(in_reg, -id->left, -id->top);

    extents_rect = *rdpRegionExtents(in_reg);
    tile_extents_rect.x1 = extents_rect.x1 & ~63;
    tile_extents_rect.y1 = extents_rect.y1 & ~63;
    tile_extents_rect.x2 = (extents_rect.x2 + 63) & ~63;
    tile_extents_rect.y2 = (extents_rect.y2 + 63) & ~63;
    width = tile_extents_rect.x2 - tile_extents_rect.x1;
    height = tile_extents_rect.y2 - tile_extents_rect.y1;
    LLOGLN(10, ("rdpEglCaptureRfx: width %d height %d", width, height));
    crcs = g_new(int, (width / 64) * (height / 64));
    if (crcs == NULL)
    {
        free(out_rects);
        return FALSE;
    }
    rfxGC = GetScratchGC(dev->depth, pScreen);
    if (rfxGC != NULL)
    {
        tmpval[0].val = GXcopy;
        tmpval[1].val = 0;
        ChangeGC(NullClient, rfxGC, GCFunction | GCForeground, tmpval);
        ValidateGC(&(screen_pixmap->drawable), rfxGC);
        pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                       pScreen->rootDepth,
                                       GLAMOR_CREATE_NO_LARGE);
        if (pixmap != NULL)
        {
            tex = glamor_get_pixmap_texture(pixmap);
            crc_pixmap = pScreen->CreatePixmap(pScreen, width / 64,
                                               height / 64,
                                               pScreen->rootDepth,
                                               GLAMOR_CREATE_NO_LARGE);
            if (crc_pixmap != NULL)
            {
                crc_tex = glamor_get_pixmap_texture(crc_pixmap);
                yuv_pixmap = pScreen->CreatePixmap(pScreen, width, height,
                                                   pScreen->rootDepth,
                                                   GLAMOR_CREATE_NO_LARGE);
                if (yuv_pixmap != NULL)
                {
                    yuv_tex = glamor_get_pixmap_texture(yuv_pixmap);
                    rfxGC->ops->CopyArea(&(screen_pixmap->drawable),
                                         &(pixmap->drawable), rfxGC,
                                         tile_extents_rect.x1 + id->left,
                                         tile_extents_rect.y1 + id->top,
                                         width, height, 0, 0);
                    rdpEglRfxRgbToYuv(egl, tex, yuv_tex, width, height);
                    rdpEglRfxClear(rfxGC, yuv_pixmap, &tile_extents_rect,
                                   in_reg);
                    rdpEglRfxYuvToYuvlp(egl, yuv_tex, tex, width, height);
                    rdpEglRfxCrc(egl, tex, crc_tex, width, height, crcs);
                    rdpEglOut(clientCon, egl, in_reg, *out_rects,
                              num_out_rects, id, tex, &tile_extents_rect,
                              crcs);
                    pScreen->DestroyPixmap(yuv_pixmap);
                }
                else
                {
                    LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
                }
                pScreen->DestroyPixmap(crc_pixmap);
            }
            else
            {
                LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
            }
            pScreen->DestroyPixmap(pixmap);
        }
        else
        {
            LLOGLN(0, ("rdpEglCaptureRfx: CreatePixmap failed"));
        }
        FreeScratchGC(rfxGC);
    }
    else
    {
        LLOGLN(0, ("rdpEglCaptureRfx: GetScratchGC failed"));
    }
    free(crcs);
    return TRUE;
}

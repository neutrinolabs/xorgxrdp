/*
Copyright 2014 Laxmikant Rashinkar
Copyright 2014-2017 Jay Sorg

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

capture

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

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpReg.h"
#include "rdpMisc.h"
#include "rdpCapture.h"

#include "wyhash.h"
/* hex digits of pi as a 64 bit int */
#define WYHASH_SEED 0x3243f6a8885a308dull

#if defined(XORGXRDP_GLAMOR)
#include "rdpEgl.h"
#include <glamor.h>
#endif

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

#define RGB_SPLIT(A, R, G, B, pixel) \
    A = (pixel >> 24) & UCHAR_MAX; \
    R = (pixel >> 16) & UCHAR_MAX; \
    G = (pixel >>  8) & UCHAR_MAX; \
    B = (pixel >>  0) & UCHAR_MAX;

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a8r8g8b8(rdpClientCon *clientCon,
                                const uint8_t *src, int src_stride, int srcx, int srcy,
                                uint8_t *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int jndex;
    int bytes;
    int height;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 4;
        bytes = box->x2 - box->x1;
        bytes *= 4;
        height = box->y2 - box->y1;
        for (jndex = 0; jndex < height; jndex++)
        {
            g_memcpy(d8, s8, bytes);
            d8 += dst_stride;
            s8 += src_stride;
        }
    }
    return 0;
}

/******************************************************************************/
static int
rdpFillBox_yuvalp(int ax, int ay,
                  uint8_t *dst, int dst_stride)
{
    dst = dst + (ay << 8) * (dst_stride >> 8) + (ax << 8);
    g_memset(dst, 0, 64 * 64 * 4);
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking
 * convert ARGB32 to 64x64 linear planar YUVA */
/* http://msdn.microsoft.com/en-us/library/ff635643.aspx
 * 0.299   -0.168935    0.499813
 * 0.587   -0.331665   -0.418531
 * 0.114    0.50059    -0.081282
   y = r *  0.299000 + g *  0.587000 + b *  0.114000;
   u = r * -0.168935 + g * -0.331665 + b *  0.500590;
   v = r *  0.499813 + g * -0.418531 + b * -0.081282; */
/* 19595  38470   7471
  -11071 -21736  32807
   32756 -27429  -5327 */
int
a8r8g8b8_to_yuvalp_box(const uint8_t *s8, int src_stride,
                       uint8_t *d8, int dst_stride,
                       int width, int height)
{
    uint8_t *yptr;
    uint8_t *uptr;
    uint8_t *vptr;
    uint8_t *aptr;
    const uint32_t *s32;
    int jndex;
    int kndex;
    uint32_t pixel;
    uint8_t a;
    int r;
    int g;
    int b;
    int y;
    int u;
    int v;

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
            RGB_SPLIT(a, r, g, b, pixel);
            y = (r *  19595 + g *  38470 + b *   7471) >> 16;
            u = (r * -11071 + g * -21736 + b *  32807) >> 16;
            v = (r *  32756 + g * -27429 + b *  -5327) >> 16;
            u = u + 128;
            v = v + 128;
            y = RDPCLAMP(y, 0, UCHAR_MAX);
            u = RDPCLAMP(u, 0, UCHAR_MAX);
            v = RDPCLAMP(v, 0, UCHAR_MAX);
            *(yptr++) = y;
            *(uptr++) = u;
            *(vptr++) = v;
            *(aptr++) = a;
            kndex++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
static int
rdpCopyBox_a8r8g8b8_to_yuvalp(rdpClientCon *clientCon, int ax, int ay,
                              const uint8_t *src, int src_stride,
                              uint8_t *dst, int dst_stride,
                              BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int width;
    int height;
    BoxPtr box;

    dst = dst + (ay << 8) * (dst_stride >> 8) + (ax << 8);
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + box->y1 * src_stride;
        s8 += box->x1 * 4;
        d8 = dst + (box->y1 - ay) * 64;
        d8 += box->x1 - ax;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        clientCon->dev->a8r8g8b8_to_yuvalp_box(s8, src_stride,
                                               d8, 64,
                                               width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_a8b8g8r8_box(const uint8_t *s8, int src_stride,
                         uint8_t *d8, int dst_stride,
                         int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    const uint32_t *s32;
    uint32_t *d32;

    for (index = 0; index < height; index++)
    {
        s32 = (const uint32_t *) s8;
        d32 = (uint32_t *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d32 = COLOR24(red, green, blue);
            s32++;
            d32++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a8b8g8r8(rdpClientCon *clientCon,
                                const uint8_t *src, int src_stride, int srcx, int srcy,
                                uint8_t *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = clientCon->dev->a8r8g8b8_to_a8b8g8r8_box;
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 4;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_r5g6b5_box(const uint8_t *s8, int src_stride,
                       uint8_t *d8, int dst_stride,
                       int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    const uint32_t *s32;
    uint16_t *d16;

    for (index = 0; index < height; index++)
    {
        s32 = (const uint32_t *) s8;
        d16 = (uint16_t *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d16 = COLOR16(red, green, blue);
            s32++;
            d16++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_r5g6b5(rdpClientCon *clientCon,
                              const uint8_t *src, int src_stride, int srcx, int srcy,
                              uint8_t *dst, int dst_stride, int dstx, int dsty,
                              BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_r5g6b5_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 2;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_a1r5g5b5_box(const uint8_t *s8, int src_stride,
                         uint8_t *d8, int dst_stride,
                         int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    const uint32_t *s32;
    uint16_t *d16;

    for (index = 0; index < height; index++)
    {
        s32 = (const uint32_t *) s8;
        d16 = (uint16_t *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *d16 = COLOR15(red, green, blue);
            s32++;
            d16++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_a1r5g5b5(rdpClientCon *clientCon,
                                const uint8_t *src, int src_stride, int srcx, int srcy,
                                uint8_t *dst, int dst_stride, int dstx, int dsty,
                                BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_a1r5g5b5_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 2;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_r3g3b2_box(const uint8_t *s8, int src_stride,
                       uint8_t *d8, int dst_stride,
                       int width, int height)
{
    int index;
    int jndex;
    int red;
    int green;
    int blue;
    const uint32_t *s32;
    uint8_t *ld8;

    for (index = 0; index < height; index++)
    {
        s32 = (const uint32_t *) s8;
        ld8 = (uint8_t *) d8;
        for (jndex = 0; jndex < width; jndex++)
        {
            SPLITCOLOR32(red, green, blue, *s32);
            *ld8 = COLOR8(red, green, blue);
            s32++;
            ld8++;
        }
        d8 += dst_stride;
        s8 += src_stride;
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_r3g3b2(rdpClientCon *clientCon,
                              const uint8_t *src, int src_stride, int srcx, int srcy,
                              uint8_t *dst, int dst_stride, int dstx, int dsty,
                              BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8;
    int index;
    int width;
    int height;
    BoxPtr box;
    copy_box_proc copy_box;

    copy_box = a8r8g8b8_to_r3g3b2_box; /* TODO, simd */
    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8 = dst + (box->y1 - dsty) * dst_stride;
        d8 += (box->x1 - dstx) * 1;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        copy_box(s8, src_stride, d8, dst_stride, width, height);
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_nv12_box(const uint8_t *s8, int src_stride,
                     uint8_t *d8_y, int dst_stride_y,
                     uint8_t *d8_uv, int dst_stride_uv,
                     int width, int height)
{
    int index;
    int jndex;
    int R;
    int G;
    int B;
    int Y;
    int U;
    int V;
    int U_sum;
    int V_sum;
    int pixel;
    const uint32_t *s32a;
    const uint32_t *s32b;
    uint8_t *d8ya;
    uint8_t *d8yb;
    uint8_t *d8uv;

    for (jndex = 0; jndex < height; jndex += 2)
    {
        s32a = (const uint32_t *) (s8 + src_stride * jndex);
        s32b = (const uint32_t *) (s8 + src_stride * (jndex + 1));
        d8ya = d8_y + dst_stride_y * jndex;
        d8yb = d8_y + dst_stride_y * (jndex + 1);
        d8uv = d8_uv + dst_stride_uv * (jndex / 2);
        for (index = 0; index < width; index += 2)
        {
            U_sum = 0;
            V_sum = 0;

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) +  16;
            U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            d8uv[0] = (U_sum + 2) / 4;
            d8uv++;
            d8uv[0] = (V_sum + 2) / 4;
            d8uv++;
        }
    }
    return 0;
}

/******************************************************************************/
int
a8r8g8b8_to_nv12_709fr_box(const uint8_t *s8, int src_stride,
                           uint8_t *d8_y, int dst_stride_y,
                           uint8_t *d8_uv, int dst_stride_uv,
                           int width, int height)
{
    int index;
    int jndex;
    int R;
    int G;
    int B;
    int Y;
    int U;
    int V;
    int U_sum;
    int V_sum;
    int pixel;
    const uint32_t *s32a;
    const uint32_t *s32b;
    uint8_t *d8ya;
    uint8_t *d8yb;
    uint8_t *d8uv;

    for (jndex = 0; jndex < height; jndex += 2)
    {
        s32a = (const uint32_t *) (s8 + src_stride * jndex);
        s32b = (const uint32_t *) (s8 + src_stride * (jndex + 1));
        d8ya = d8_y + dst_stride_y * jndex;
        d8yb = d8_y + dst_stride_y * (jndex + 1);
        d8uv = d8_uv + dst_stride_uv * (jndex / 2);
        for (index = 0; index < width; index += 2)
        {
            U_sum = 0;
            V_sum = 0;

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
            U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
            V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
            U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
            V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
            U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
            V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
            U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
            V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            d8uv[0] = (U_sum + 2) / 4;
            d8uv++;
            d8uv[0] = (V_sum + 2) / 4;
            d8uv++;
        }
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_nv12(rdpClientCon *clientCon,
                            const uint8_t *src, int src_stride, int srcx, int srcy,
                            uint8_t *dst_y, int dst_stride_y,
                            uint8_t *dst_uv, int dst_stride_uv,
                            int dstx, int dsty,
                            BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8_y;
    uint8_t *d8_uv;
    int index;
    int width;
    int height;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8_y = dst_y + (box->y1 - dsty) * dst_stride_y;
        d8_y += (box->x1 - dstx) * 1;
        d8_uv = dst_uv + ((box->y1 - dsty) / 2) * dst_stride_uv;
        d8_uv += (box->x1 - dstx) * 1;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        clientCon->dev->a8r8g8b8_to_nv12_box(s8, src_stride,
                                             d8_y, dst_stride_y,
                                             d8_uv, dst_stride_uv,
                                             width, height);
    }
    return 0;
}

/******************************************************************************/
/* copy rects with no error checking */
static int
rdpCopyBox_a8r8g8b8_to_nv12_709fr(rdpClientCon *clientCon,
                                  const uint8_t *src, int src_stride,
                                  int srcx, int srcy,
                                  uint8_t *dst_y, int dst_stride_y,
                                  uint8_t *dst_uv, int dst_stride_uv,
                                  int dstx, int dsty,
                                  BoxPtr rects, int num_rects)
{
    const uint8_t *s8;
    uint8_t *d8_y;
    uint8_t *d8_uv;
    int index;
    int width;
    int height;
    BoxPtr box;

    for (index = 0; index < num_rects; index++)
    {
        box = rects + index;
        s8 = src + (box->y1 - srcy) * src_stride;
        s8 += (box->x1 - srcx) * 4;
        d8_y = dst_y + (box->y1 - dsty) * dst_stride_y;
        d8_y += (box->x1 - dstx) * 1;
        d8_uv = dst_uv + ((box->y1 - dsty) / 2) * dst_stride_uv;
        d8_uv += (box->x1 - dstx) * 1;
        width = box->x2 - box->x1;
        height = box->y2 - box->y1;
        clientCon->dev->a8r8g8b8_to_nv12_709fr_box(s8, src_stride,
                                                   d8_y, dst_stride_y,
                                                   d8_uv, dst_stride_uv,
                                                   width, height);
    }
    return 0;
}

/******************************************************************************/
static Bool
rdpCopyBoxList(rdpClientCon *clientCon, PixmapPtr dstPixmap,
               BoxPtr out_rects, int num_out_rects,
               int srcx, int srcy,
               int dstx, int dsty)
{
    PixmapPtr hwPixmap;
    BoxPtr pbox;
    ScreenPtr pScreen;
    GCPtr copyGC;
    ChangeGCVal tmpval[1];
    int count;
    int index;
    int left;
    int top;
    int width;
    int height;
    char pix1[16];
    rdpPtr dev;

    LLOGLN(10, ("rdpCopyBoxList:"));

    dev = clientCon->dev;
    pScreen = dev->pScreen;
    hwPixmap = pScreen->GetScreenPixmap(pScreen);
    copyGC = GetScratchGC(dev->depth, pScreen);
    if (copyGC == NULL)
    {
        return FALSE;
    }
    tmpval[0].val = GXcopy;
    ChangeGC(NullClient, copyGC, GCFunction, tmpval);
    ValidateGC(&(hwPixmap->drawable), copyGC);
    count = num_out_rects;
    pbox = out_rects;
    for (index = 0; index < count; index++)
    {
        left = pbox[index].x1;
        top = pbox[index].y1;
        width = pbox[index].x2 - pbox[index].x1;
        height = pbox[index].y2 - pbox[index].y1;
        if ((width > 0) && (height > 0))
        {
            copyGC->ops->CopyArea(&(hwPixmap->drawable),
                                    &(dstPixmap->drawable), copyGC,
                                    left - srcx, top - srcy,
                                    width, height,
                                    left - dstx, top - dsty);
        }
    }
    FreeScratchGC(copyGC);
    pScreen->GetImage(&(dstPixmap->drawable), 0, 0, 1, 1, ZPixmap,
                          0xffffffff, pix1);

    return TRUE;
}

/******************************************************************************/
static Bool
isShmStatusActive(enum shared_memory_status status) {
    switch (status) {
        case SHM_ACTIVE:
        case SHM_RFX_ACTIVE:
        case SHM_H264_ACTIVE:
            return TRUE;
        default:
            return FALSE;
    }
}

/******************************************************************************/
/* copy rects with no error checking */
static uint64_t
wyhash_rfx_tile(const uint8_t *src, int src_stride, int x, int y, uint64_t seed)
{
    int row;
    uint64_t hash;
    const uint8_t *s8;
    hash = seed;
    s8 = src + (y * src_stride) + (x * 4);
    for(row = 0; row < 64; row++)
    {
        hash = wyhash((const void*)s8, 64 * 4, hash, _wyp);
        s8 += src_stride;
    }
    return hash;
}

/******************************************************************************/
static Bool
rdpCaptureSimple(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    int num_rects;
    int i;
    Bool rv;
    const uint8_t *src;
    uint8_t *dst;
    int src_stride;
    int dst_stride;
    int dst_format;

    LLOGLN(10, ("rdpCaptureSimple:"));

    if (!isShmStatusActive(clientCon->shmemstatus)) {
        LLOGLN(0, ("rdpCaptureSimple: WARNING -- Shared memory is not configured."
                   " Aborting capture!"));
        return FALSE;
    }

    rv = TRUE;


    num_rects = REGION_NUM_RECTS(in_reg);
    psrc_rects = REGION_RECTS(in_reg);

    if (num_rects < 1)
    {
        return FALSE;
    }

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects);
    for (i = 0; i < num_rects; i++)
    {
        rect = psrc_rects[i];
        (*out_rects)[i] = rect;
    }

    if (clientCon->dev->glamor || clientCon->dev->nvidia)
    {
        /* copy vmem to smem */
        if (!rdpCopyBoxList(clientCon, clientCon->dev->screenSwPixmap,
                            *out_rects, *num_out_rects,
                            0, 0, 0, 0))
        {
            return FALSE;
        }
    }

    src = id->pixels;
    dst = id->shmem_pixels;
    dst_format = clientCon->rdp_format;
    src_stride = id->lineBytes;
    dst_stride = clientCon->cap_stride_bytes;

    if (dst_format == XRDP_a8r8g8b8)
    {
        rdpCopyBox_a8r8g8b8_to_a8r8g8b8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, 0, 0,
                                        psrc_rects, num_rects);
    }
    else if (dst_format == XRDP_a8b8g8r8)
    {
        rdpCopyBox_a8r8g8b8_to_a8b8g8r8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, 0, 0,
                                        psrc_rects, num_rects);
    }
    else if (dst_format == XRDP_r5g6b5)
    {
        rdpCopyBox_a8r8g8b8_to_r5g6b5(clientCon,
                                      src, src_stride, 0, 0,
                                      dst, dst_stride, 0, 0,
                                      psrc_rects, num_rects);
    }
    else if (dst_format == XRDP_a1r5g5b5)
    {
        rdpCopyBox_a8r8g8b8_to_a1r5g5b5(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, 0, 0,
                                        psrc_rects, num_rects);
    }
    else if (dst_format == XRDP_r3g3b2)
    {
        rdpCopyBox_a8r8g8b8_to_r3g3b2(clientCon,
                                      src, src_stride, 0, 0,
                                      dst, dst_stride, 0, 0,
                                      psrc_rects, num_rects);
    }
    else
    {
        LLOGLN(0, ("rdpCaptureSimple: unimplemented color conversion"));
    }
    return rv;
}

/******************************************************************************/
/* make out_rects always multiple of 16 width and height */
static Bool
rdpCaptureSufA16(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    BoxRec srect;
    const uint8_t *src_rect;
    uint8_t *dst_rect;
    int num_rects;
    int src_bytespp;
    int dst_bytespp;
    int width;
    int height;
    int src_offset;
    int dst_offset;
    int index;
    int jndex;
    int kndex;
    int red;
    int green;
    int blue;
    int ex;
    int ey;
    Bool rv;
    const uint32_t *s32;
    uint32_t *d32;
    const uint8_t *src;
    uint8_t *dst;
    int src_stride;
    int dst_stride;
    int dst_format;

    LLOGLN(10, ("rdpCaptureSufA16:"));

    if (!isShmStatusActive(clientCon->shmemstatus)) {
        LLOGLN(0, ("rdpCaptureSufA16: WARNING -- Shared memory is not configured."
               " Aborting capture!"));
        return FALSE;
    }

    rv = TRUE;

    num_rects = REGION_NUM_RECTS(in_reg);
    psrc_rects = REGION_RECTS(in_reg);

    if (num_rects < 1)
    {
        return FALSE;
    }

    srect.x1 = clientCon->cap_left;
    srect.y1 = clientCon->cap_top;
    srect.x2 = clientCon->cap_left + clientCon->cap_width;
    srect.y2 = clientCon->cap_top + clientCon->cap_height;

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects * 4);
    index = 0;
    while (index < num_rects)
    {
        rect = psrc_rects[index];
        width = rect.x2 - rect.x1;
        height = rect.y2 - rect.y1;
        ex = ((width + 15) & ~15) - width;
        if (ex != 0)
        {
            rect.x2 += ex;
            if (rect.x2 > srect.x2)
            {
                rect.x1 -= rect.x2 - srect.x2;
                rect.x2 = srect.x2;
            }
            if (rect.x1 < srect.x1)
            {
                rect.x1 += 16;
            }
        }
        ey = ((height + 15) & ~15) - height;
        if (ey != 0)
        {
            rect.y2 += ey;
            if (rect.y2 > srect.y2)
            {
                rect.y1 -= rect.y2 - srect.y2;
                rect.y2 = srect.y2;
            }
            if (rect.y1 < srect.y1)
            {
                rect.y1 += 16;
            }
        }
        (*out_rects)[index] = rect;
        index++;
    }

    if (clientCon->dev->glamor || clientCon->dev->nvidia)
    {
        /* copy vmem to smem */
        if (!rdpCopyBoxList(clientCon, clientCon->dev->screenSwPixmap,
                            *out_rects, *num_out_rects,
                            0, 0, 0, 0))
        {
            return FALSE;
        }
    }

    src = id->pixels;
    dst = id->shmem_pixels;
    dst_format = clientCon->rdp_format;
    src_stride = id->lineBytes;
    dst_stride = clientCon->cap_stride_bytes;

    if (dst_format == XRDP_a8b8g8r8)
    {
        src_bytespp = 4;
        dst_bytespp = 4;

        for (index = 0; index < num_rects; index++)
        {
            /* get rect to copy */
            rect = (*out_rects)[index];

            /* get rect dimensions */
            width = rect.x2 - rect.x1;
            height = rect.y2 - rect.y1;

            /* point to start of each rect in respective memory */
            src_offset = rect.y1 * src_stride + rect.x1 * src_bytespp;
            dst_offset = rect.y1 * dst_stride + rect.x1 * dst_bytespp;
            src_rect = src + src_offset;
            dst_rect = dst + dst_offset;

            /* copy one line at a time */
            for (jndex = 0; jndex < height; jndex++)
            {
                s32 = (const uint32_t *) src_rect;
                d32 = (uint32_t *) dst_rect;
                for (kndex = 0; kndex < width; kndex++)
                {
                    SPLITCOLOR32(red, green, blue, *s32);
                    *d32 = COLOR24(red, green, blue);
                    s32++;
                    d32++;
                }
                src_rect += src_stride;
                dst_rect += dst_stride;
            }
        }
    }
    else
    {
        LLOGLN(0, ("rdpCaptureSufA16: unimplemented color conversion"));
    }
    return rv;
}

/******************************************************************************/
static Bool
rdpCaptureGfxPro(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                 int *num_out_rects, struct image_data *id)
{
    int x;
    int y;
    int out_rect_index;
    int num_rects;
    int rcode;
    BoxRec rect;
    BoxRec extents_rect;
    BoxPtr rects;
    RegionRec tile_reg;
    const uint8_t *src;
    uint8_t *dst;
    uint8_t *crc_dst;
    int src_stride;
    int dst_stride;
    int crc_offset;
    int crc_stride;
    uint64_t crc;
    int num_crcs;
    int mon_index;

    LLOGLN(10, ("rdpCaptureGfxPro:"));

    if (!isShmStatusActive(clientCon->shmemstatus))
    {
        LLOGLN(0, ("rdpCaptureGfxPro: WARNING -- Shared memory is not configured"
                   " for RFX. Aborting capture!"));
        return FALSE;
    }

    if (clientCon->dev->glamor || clientCon->dev->nvidia)
    {
        /* copy vmem to smem */
        if (!rdpCopyBoxList(clientCon, clientCon->dev->screenSwPixmap,
                            REGION_RECTS(in_reg), REGION_NUM_RECTS(in_reg),
                            0, 0, 0, 0))
        {
            return FALSE;
        }
    }

    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }
    out_rect_index = 0;

    rdpRegionTranslate(in_reg, -id->left, -id->top);

    src = id->pixels;
    dst = id->shmem_pixels;
    src_stride = id->lineBytes;
    dst_stride = ((id->width + 63) & ~63) * 4;

    src = src + src_stride * id->top + id->left * 4;

    mon_index = (id->flags >> 28) & 0xF;
    crc_stride = (id->width + 63) / 64;
    num_crcs = crc_stride * ((id->height + 63) / 64);
    if (num_crcs != clientCon->num_rfx_crcs_alloc[mon_index])
    {
        LLOGLN(0, ("rdpCaptureGfxPro: resize the crc list was %d now %d",
               clientCon->num_rfx_crcs_alloc[mon_index], num_crcs));
        /* resize the crc list */
        clientCon->num_rfx_crcs_alloc[mon_index] = num_crcs;
        free(clientCon->rfx_crcs[mon_index]);
        clientCon->rfx_crcs[mon_index] = g_new0(uint64_t, num_crcs);
    }

    extents_rect = *rdpRegionExtents(in_reg);
    y = extents_rect.y1 & ~63;
    while (y < extents_rect.y2)
    {
        x = extents_rect.x1 & ~63;
        while (x < extents_rect.x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + XRDP_RFX_ALIGN;
            rect.y2 = rect.y1 + XRDP_RFX_ALIGN;
            rcode = rdpRegionContainsRect(in_reg, &rect);
            LLOGLN(10, ("rdpCaptureGfxPro: rcode %d", rcode));

            if (rcode == rgnOUT)
            {
                LLOGLN(10, ("rdpCaptureGfxPro: rgnOUT"));
                rdpRegionInit(&tile_reg, &rect, 0);
                rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                rdpRegionUninit(&tile_reg);
            }
            else
            {
                /* hex digits of pi as a 64 bit int */
                crc = WYHASH_SEED;
                if (rcode == rgnPART)
                {
                    LLOGLN(10, ("rdpCaptureGfxPro: rgnPART"));
                    rdpFillBox_yuvalp(x, y, dst, dst_stride);
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionIntersect(&tile_reg, in_reg, &tile_reg);
                    rects = REGION_RECTS(&tile_reg);
                    num_rects = REGION_NUM_RECTS(&tile_reg);
                    crc = wyhash((const void*)rects, num_rects * sizeof(BoxRec), crc, _wyp);
                    rdpCopyBox_a8r8g8b8_to_yuvalp(clientCon, x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  rects, num_rects);
                    crc_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
                    crc = wyhash((const void*)crc_dst, 64 * 64 * 4, crc, _wyp);
                    rdpRegionUninit(&tile_reg);
                }
                else /* rgnIN */
                {
                    LLOGLN(10, ("rdpCaptureGfxPro: rgnIN"));
                    crc = wyhash_rfx_tile(src, src_stride, x, y, crc);
                }
                crc_offset = (y / XRDP_RFX_ALIGN) * crc_stride
                             + (x / XRDP_RFX_ALIGN);
                LLOGLN(10, ("rdpCaptureGfxPro: crc 0x%" PRIx64 " 0x%" PRIx64,
                       crc, clientCon->rfx_crcs[mon_index][crc_offset]));
                if (crc == clientCon->rfx_crcs[mon_index][crc_offset])
                {
                    LLOGLN(10, ("rdpCaptureGfxPro: crc skip at x %d y %d", x, y));
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionSubtract(in_reg, in_reg, &tile_reg);
                    rdpRegionUninit(&tile_reg);
                }
                else
                {
                    /* lazily only do this if hash wasn't identical */
                    if (rcode != rgnPART)
                    {
                        rdpCopyBox_a8r8g8b8_to_yuvalp(clientCon, x, y,
                                src, src_stride,
                                dst, dst_stride,
                                &rect, 1);
                    }
                    clientCon->rfx_crcs[mon_index][crc_offset] = crc;
                    (*out_rects)[out_rect_index] = rect;
                    out_rect_index++;
                    if (out_rect_index >= RDP_MAX_TILES)
                    {
                        free(*out_rects);
                        *out_rects = NULL;
                        return FALSE;
                    }
                }
            }
            x += XRDP_RFX_ALIGN;
        }
        y += XRDP_RFX_ALIGN;
    }
    *num_out_rects = out_rect_index;
    return TRUE;
}

/******************************************************************************/
/* make out_rects always multiple of 2 width and height */
static Bool
rdpCaptureSufA2(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                int *num_out_rects, struct image_data *id)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    int num_rects;
    int index;
    uint8_t *dst_uv;
    Bool rv;
    const uint8_t *src;
    uint8_t *dst;
    int src_stride;
    int dst_stride;
    int dst_format;
    int monitor_index;

    LLOGLN(10, ("rdpCaptureSufA2:"));

    if (!isShmStatusActive(clientCon->shmemstatus))
    {
        LLOGLN(0, ("rdpCaptureSufA2: WARNING -- Shared memory is not configured."
               " Aborting capture!"));
        return FALSE;
    }

    rv = TRUE;

    num_rects = REGION_NUM_RECTS(in_reg);
    psrc_rects = REGION_RECTS(in_reg);

    if (num_rects < 1)
    {
        return FALSE;
    }

    monitor_index = (id->flags >> 28) & 0xF;
    if (clientCon->accelAssistPixmaps[monitor_index] != NULL)
    {
        /* copy vmem to vmem */
        rv = rdpCopyBoxList(clientCon,
                            clientCon->accelAssistPixmaps[monitor_index],
                            *out_rects, *num_out_rects,
                            0, 0, id->left, id->top);
        id->flags |= 1;
        return rv;
        /* accel assist will do the rest */
    }
    else if (clientCon->dev->glamor || clientCon->dev->nvidia)
    {
        /* copy vmem to smem */
        rv = rdpCopyBoxList(clientCon, clientCon->dev->screenSwPixmap,
                            *out_rects, *num_out_rects,
                            0, 0, id->left, id->top);
    }

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects * 4);
    index = 0;
    while (index < num_rects)
    {
        rect = psrc_rects[index];
        LLOGLN(10, ("old x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.x2,
               rect.x2, rect.y2));
        rect.x1 -= rect.x1 & 1;
        rect.y1 -= rect.y1 & 1;
        rect.x2 += rect.x2 & 1;
        rect.y2 += rect.y2 & 1;
        LLOGLN(10, ("new x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.x2,
               rect.x2, rect.y2));
        (*out_rects)[index] = rect;
        index++;
    }

    src = id->pixels;
    dst = id->shmem_pixels;
    dst_format = clientCon->rdp_format;
    src_stride = id->lineBytes;
    dst_stride = clientCon->cap_stride_bytes;

    if (dst_format == XRDP_a8r8g8b8)
    {
        rdpCopyBox_a8r8g8b8_to_a8r8g8b8(clientCon,
                                        src, src_stride, 0, 0,
                                        dst, dst_stride, 0, 0,
                                        *out_rects, num_rects);
    }
    else if (dst_format == XRDP_nv12)
    {
        dst_uv = dst;
        dst_uv += clientCon->cap_width * clientCon->cap_height;
        rdpCopyBox_a8r8g8b8_to_nv12(clientCon,
                                    src, src_stride, 0, 0,
                                    dst, dst_stride,
                                    dst_uv, dst_stride,
                                    0, 0,
                                    *out_rects, num_rects);
    }
    else
    {
        LLOGLN(0, ("rdpCaptureSufA2: unimplemented color conversion"));
    }

    return rv;
}

/******************************************************************************/
/* make out_rects always multiple of 2 width and height */
static Bool
rdpCaptureGfxA2(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
                int *num_out_rects, struct image_data *id)
{
    BoxPtr psrc_rects;
    BoxRec rect;
    int num_rects;
    int index;
    uint8_t *dst_uv;
    Bool rv;
    const uint8_t *src;
    uint8_t *dst;
    int src_stride;
    int dst_stride;
    int dst_format;
    int monitor_index;

    LLOGLN(10, ("rdpCaptureGfxA2:"));

    if (!isShmStatusActive(clientCon->shmemstatus))
    {
        LLOGLN(0, ("rdpCaptureGfxA2: WARNING -- Shared memory is not configured."
               " Aborting capture!"));
        return FALSE;
    }

    rdpRegionTranslate(in_reg, -id->left, -id->top);

    num_rects = REGION_NUM_RECTS(in_reg);
    psrc_rects = REGION_RECTS(in_reg);

    if (num_rects < 1)
    {
        return FALSE;
    }

    *num_out_rects = num_rects;

    *out_rects = g_new(BoxRec, num_rects * 4);
    index = 0;
    while (index < num_rects)
    {
        rect = psrc_rects[index];
        LLOGLN(10, ("old x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.y1,
               rect.x2, rect.y2));
        rect.x1 -= rect.x1 & 1;
        rect.y1 -= rect.y1 & 1;
        rect.x2 += rect.x2 & 1;
        rect.y2 += rect.y2 & 1;
        if (rect.x2 > id->width)
        {
            rect.x2 = id->width & ~1;
        }
        if (rect.y2 > id->height)
        {
            rect.y2 = id->height & ~1;
        }
        LLOGLN(10, ("new x1 %d y1 %d x2 %d y2 %d", rect.x1, rect.y1,
               rect.x2, rect.y2));
        (*out_rects)[index] = rect;
        index++;
    }
    rv = TRUE;
    monitor_index = (id->flags >> 28) & 0xF;
    if (clientCon->accelAssistPixmaps[monitor_index] != NULL)
    {
        LLOGLN(10, ("rdpCaptureGfxA2: a monitor_index %d left %d top %d",
               monitor_index, id->left, id->top));
        /* copy vmem to vmem */
        rv = rdpCopyBoxList(clientCon,
                            clientCon->accelAssistPixmaps[monitor_index],
                            *out_rects, num_rects,
                            -id->left, -id->top, 0, 0);
        id->flags |= 1;
        return rv;
        /* accel assist will do the rest */
    }
    else if (clientCon->dev->glamor || clientCon->dev->nvidia)
    {
        LLOGLN(10, ("rdpCaptureGfxA2: b monitor_index %d left %d top %d",
               monitor_index, id->left, id->top));
        /* copy vmem to smem */
        if (!rdpCopyBoxList(clientCon,
                            clientCon->dev->screenSwPixmap,
                            *out_rects, num_rects,
                            -id->left, -id->top, -id->left, -id->top))
        {
            return FALSE;
        }
    }

    src = id->pixels;
    dst = id->shmem_pixels;
    dst_format = clientCon->rdp_format;
    src_stride = id->lineBytes;
    dst_stride = id->width;

    if (dst_format == XRDP_nv12_709fr)
    {
        dst_uv = dst;
        dst_uv += id->width * id->height;
        rdpCopyBox_a8r8g8b8_to_nv12_709fr(clientCon,
                                          src, src_stride,
                                          -id->left, -id->top,
                                          dst, dst_stride,
                                          dst_uv, dst_stride,
                                          0, 0,
                                          *out_rects, num_rects);
    }
    else
    {
        LLOGLN(0, ("rdpCaptureGfxA2: unimplemented color conversion"));
    }

    return rv;
}

/**
 * Copy an array of rectangles from one memory area to another
 *****************************************************************************/
Bool
rdpCapture(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
           int *num_out_rects, struct image_data *id)
{
    enum xrdp_capture_code mode;

    LLOGLN(10, ("rdpCapture:"));
    mode = clientCon->client_info.capture_code;
    switch (mode)
    {
        case CC_SIMPLE:
            return rdpCaptureSimple(clientCon, in_reg, out_rects, num_out_rects, id);
        case CC_SUF_A16:
            return rdpCaptureSufA16(clientCon, in_reg, out_rects, num_out_rects, id);
        case CC_SUF_RFX: /* surface command RFX */
            /* FALLTHROUGH */
        case CC_GFX_PRO: /* GFX progressive */
            return rdpCaptureGfxPro(clientCon, in_reg, out_rects, num_out_rects, id);
        case CC_SUF_A2: /* surface command h264 */
            /* used for even align capture */
            return rdpCaptureSufA2(clientCon, in_reg, out_rects, num_out_rects, id);
        case CC_GFX_A2: /* GFX h264 */
            /* used for even align capture */
            return rdpCaptureGfxA2(clientCon, in_reg, out_rects, num_out_rects, id);
        default:
            LLOGLN(0, ("rdpCapture: mode %d not implemented", mode));
            break;
    }
    return FALSE;
}

/**
 * Reset any capture state fields following a memory resize
 *****************************************************************************/
void
rdpCaptureResetState(rdpClientCon *clientCon)
{
    int mode;
    int i;

    LLOGLN(10, ("rdpCapReset:"));
    mode = clientCon->client_info.capture_code;
    switch (mode)
    {
        case 2:
        case 4:
            for (i = 0 ; i < 16; ++i)
            {
                free(clientCon->rfx_crcs[i]);
                clientCon->rfx_crcs[i] = NULL;
                clientCon->num_rfx_crcs_alloc[i] = 0;
                clientCon->send_key_frame[i] = 1;
            }
            break;
        default:
            break;
    }
}

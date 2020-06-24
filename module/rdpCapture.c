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
static int
rdpCopyBox_a8r8g8b8_to_yuvalp(int ax, int ay,
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
    int r;
    int g;
    int b;
    int y;
    int u;
    int v;
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
                r = (pixel >> 16) & 0xff;
                g = (pixel >>  8) & 0xff;
                b = (pixel >>  0) & 0xff;
                y = (r *  19595 + g *  38470 + b *   7471) >> 16;
                u = (r * -11071 + g * -21736 + b *  32807) >> 16;
                v = (r *  32756 + g * -27429 + b *  -5327) >> 16;
                u = u + 128;
                v = v + 128;
                y = RDPCLAMP(y, 0, 255);
                u = RDPCLAMP(u, 0, 255);
                v = RDPCLAMP(v, 0, 255);
                *(yptr++) = y;
                *(uptr++) = u;
                *(vptr++) = v;
                *(aptr++) = a;
                kndex++;
            }
            d8 += 64;
            s8 += src_stride;
        }
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
static void
wyhash_rfx_tile_rows(const uint8_t *src, int src_stride, int x, int y,
    uint64_t *row_hashes, int nrows)
{
    int row;
    const uint8_t *s8;

    for(row = 0; row < nrows; row++)
    {
        s8 = src + (y+row) * src_stride + x * 4;
        row_hashes[row] = wyhash((const void*)s8, 64 * 4, WYHASH_SEED, _wyp);
    }
}

/******************************************************************************/
static uint64_t
wyhash_rfx_tile_from_rows(const uint64_t *tile_rows, int tile_row_stride, int x, int y)
{
    const uint64_t *row_hashes;
    row_hashes = tile_rows + (x / 64) * tile_row_stride + y;
    return wyhash((const void*)row_hashes, 64*sizeof(uint64_t), WYHASH_SEED, _wyp);
}

/******************************************************************************/
static void
wyhash_count_offsets(const uint64_t *row_hashes, uint64_t target_hash, uint16_t *offset_histogram, int num_offsets, int neutral_offset)
{
    int offset;
    /* if a tile as the same as the previous frame with no offset we wouldn't
       need to encode it anyways and also it might be a repeating/blank
       pattern that would cause false positives in our detection */
    uint64_t neutral_hash = wyhash((const void*)(row_hashes+neutral_offset), 64*sizeof(uint64_t), WYHASH_SEED, _wyp);
    if(neutral_hash == target_hash)
    {
        return;
    }

    for(offset = 0; offset < num_offsets; offset++)
    {
        if(offset != neutral_offset) {
            uint64_t offset_tile_hash = wyhash((const void*)(row_hashes+offset), 64*sizeof(uint64_t), WYHASH_SEED, _wyp);
            if(offset_tile_hash == target_hash)
            {
                offset_histogram[offset] += 1;
            }
        }
    }
}

/******************************************************************************/
static Bool
rdpCapture0(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
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

    LLOGLN(10, ("rdpCapture0:"));

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
        LLOGLN(0, ("rdpCapture0: unimplemented color conversion"));
    }
    return rv;
}

/******************************************************************************/
/* make out_rects always multiple of 16 width and height */
static Bool
rdpCapture1(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
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

    LLOGLN(10, ("rdpCapture1:"));

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
        LLOGLN(0, ("rdpCapture1: unimplemented color conversion"));
    }
    return rv;
}

/******************************************************************************/
static int
find_scroll_rect(const uint8_t *map, int map_width, int map_height, 
                 int seed_y, BoxPtr foundRect)
{
    int x1, x2, y1, y2, x, y;
    /* find x extents at seed */
    x1 = 0;
    x2 = 0; /* exclusive */
    for(x = 0; x < map_width; x++)
    {
        uint8_t v = map[seed_y*map_width+x];
        if(v == 0 && x1 != x2) break; /* already found a span and it ended */
        if(v != 0)
        {
            if(x1 == x2) x1 = x; /* didn't have a span so start one */
            x2 = x+1;
        }
    }

    /* extend rectangle up and down */
    y1 = 0;
    y2 = 0; /* exclusive */
    for(y = 0; y < map_height; y++)
    {
        uint8_t v = 1;
        for(x = x1; x < x2; x++)
        {
            if(map[y*map_width+x] == 0)
            {
                v = 0;
                break;
            }
        }

        if(v == 0 && y1 != y2) break; /* already found a span and it ended */
        if(v != 0)
        {
            if(y1 == y2) y1 = y; /* didn't have a span so start one */
            y2 = y+1;
        }
    }

    if(x2 > x1 && y2 > y1)
    {
        foundRect->x1 = x1;
        foundRect->x2 = x2;
        foundRect->y1 = y1;
        foundRect->y2 = y2;
        return 1;
    }

    return 0;
}

/******************************************************************************/
#define SCROLL_SEARCH_DIST 256
#define NUM_SCROLL_OFFSETS (SCROLL_SEARCH_DIST*2+1)
#define SCROLL_DET_MIN_H (NUM_SCROLL_OFFSETS+64)
#define SCROLL_DET_MIN_W SCROLL_DET_MIN_H
#define SCROLL_DET_MIN_FIRST_ROW_CHANGES 20
static Bool
rdpCapture2(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
            int *num_out_rects, BoxPtr scroll_rect, short *scroll_offset,
            struct image_data *id)
{
    int x;
    int y;
    int out_rect_index;
    int num_rects;
    int rcode;
    int in_scroll_rect;
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
    int num_skips;
    int tile_row_stride, crc_height;
    int i, best_offset, num_diff_first_rows;
    int xn, px, yn, py;
    int num_matched, num_checked;
    int seed_y, found, tiles_scrolled;
    uint64_t *row_hashes;
    uint64_t old_first_row, new_first_row;
    uint16_t *offset_histogram;
    uint16_t max_count;
    uint8_t *scrolled_map;
    uint64_t old_hash, new_hash;
    uint64_t target_hash;
    BoxRec foundRect;

    LLOGLN(10, ("rdpCapture2:"));
    extents_rect = *rdpRegionExtents(in_reg);

    src = id->pixels;
    dst = id->shmem_pixels;
    src_stride = id->lineBytes;
    dst_stride = clientCon->cap_stride_bytes;

    crc_stride = (clientCon->dev->width + 63) / 64;
    /* tile rows are column-major */
    tile_row_stride = ((clientCon->dev->height + 63) / 64) * 64;
    crc_height = ((clientCon->dev->height + 63) / 64);
    num_crcs = crc_stride * crc_height;
    if (num_crcs != clientCon->num_rfx_crcs_alloc)
    {
        /* resize the hash list */
        clientCon->num_rfx_crcs_alloc = num_crcs;
        free(clientCon->rfx_crcs);
        free(clientCon->rfx_tile_row_hashes);
        clientCon->rfx_crcs = g_new0(uint64_t, num_crcs);
        clientCon->rfx_tile_row_hashes = g_new0(uint64_t, num_crcs * 64);
    }

    /* update the tile row hashes. Column major order to be kind to 
       prefetchers even though it shouldn't matter much */
    num_diff_first_rows = 0;
    x = extents_rect.x1 & ~63;
    while (x < extents_rect.x2)
    {
        y = extents_rect.y1 & ~63;
        while (y < extents_rect.y2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            rcode = rdpRegionContainsRect(in_reg, &rect);
            if (rcode == rgnIN)
            {
                row_hashes = clientCon->rfx_tile_row_hashes + 
                    (x / 64) * tile_row_stride + y;
                old_first_row = row_hashes[0];
                wyhash_rfx_tile_rows(src, src_stride, x, y, row_hashes, 64);
                new_first_row = row_hashes[0];
                num_diff_first_rows += (old_first_row != new_first_row);
            }
            y += 64;
        }
        x += 64;
    }

    *scroll_offset = 0;
    scroll_rect->x1 = 0;
    scroll_rect->x2 = 0;
    scroll_rect->y1 = 0;
    scroll_rect->y2 = 0;

    /* only try to detect a scroll rectangle if area is big enough */
    if(extents_rect.y2 - extents_rect.y1 >= SCROLL_DET_MIN_H &&
       extents_rect.x2 - extents_rect.x1 >= SCROLL_DET_MIN_W &&
       num_diff_first_rows >= SCROLL_DET_MIN_FIRST_ROW_CHANGES)
    {
        /* probe for candidate scroll offsets */
        offset_histogram = g_new0(uint16_t, NUM_SCROLL_OFFSETS);
        xn = 0;
        px = 0;
        yn = 0;
        py = 0;
        y = extents_rect.y1 & ~63;
        while (y < extents_rect.y2)
        {
            x = extents_rect.x1 & ~63;
            while (x < extents_rect.x2)
            {
                rect.x1 = x;
                rect.y1 = y - SCROLL_SEARCH_DIST;
                rect.x2 = rect.x1 + 64;
                rect.y2 = rect.y1 + 64 + SCROLL_SEARCH_DIST;
                rcode = rdpRegionContainsRect(in_reg, &rect);
                if (rcode == rgnIN)
                {
                    /* only use a grid of spaced rects for scroll detection */
                    if(x != px) xn += 1;
                    if(y != py) yn += 1;
                    px = x;
                    py = y;
                    if(xn % 3 == (yn % 3) && yn % 5 == 0)
                    {
                        /* check scroll offsets */
                        crc_offset = (y / 64) * crc_stride + (x / 64);
                        target_hash = clientCon->rfx_crcs[crc_offset];
                        row_hashes = clientCon->rfx_tile_row_hashes + 
                            (rect.x1 / 64) * tile_row_stride + rect.y1;
                        wyhash_count_offsets(row_hashes, target_hash, 
                            offset_histogram, NUM_SCROLL_OFFSETS, 
                            SCROLL_SEARCH_DIST);
                    }
                }
                x += 64;
            }
            y += 64;
        }

        /* find the best candidate scroll offset */
        best_offset = 0;
        max_count = 0;
        for(i = 0; i < NUM_SCROLL_OFFSETS; i++)
        {
            if(offset_histogram[i] >= max_count)
            {
                best_offset = i;
                max_count = offset_histogram[i];
            }
        }
        free(offset_histogram);

        /* positive means we found the old contents below its old location */
        best_offset -= SCROLL_SEARCH_DIST;

        if(max_count >= 7) {
            num_matched = 0;
            num_checked = 0;
            scrolled_map = g_new0(uint8_t, num_crcs);
            /* make bitmap of tiles with that offset */
            x = extents_rect.x1 & ~63;
            while (x < extents_rect.x2)
            {
                y = 0;
                while (y < clientCon->dev->height)
                {
                    rect.x1 = x;
                    rect.y1 = y + best_offset;
                    rect.x2 = rect.x1 + 64;
                    rect.y2 = rect.y1 + 64 + best_offset;
                    rcode = rdpRegionContainsRect(in_reg, &rect);
                    if (rcode == rgnIN)
                    {
                        num_checked += 1;
                        crc_offset = (y / 64) * crc_stride + (x / 64);
                        old_hash = clientCon->rfx_crcs[crc_offset];
                        new_hash = wyhash_rfx_tile_from_rows(
                            clientCon->rfx_tile_row_hashes, tile_row_stride, 
                            rect.x1, rect.y1);
                        if(old_hash == new_hash)
                        {
                            scrolled_map[crc_offset] = 1;
                            num_matched += 1;
                        }
                    }
                    y += 64;
                }
                x += 64;
            }

            /* find large rectangle from bitmap */
            seed_y = ((extents_rect.y2 + extents_rect.y1) / 2) / 64;
            found = find_scroll_rect(scrolled_map, crc_stride,
                crc_height, seed_y, &foundRect);
            if(found)
            {
                tiles_scrolled = (foundRect.x2-foundRect.x1)*
                    (foundRect.y2-foundRect.y1);
                if(tiles_scrolled > 10)
                {
                    /* we copy to the scroll rect from the old frame at the
                       scroll rect plus the y offset best_offset maps from old
                       to new so we need to invert it to map new to old */
                    *scroll_offset = -best_offset;
                    /* transform old tile rect to new scroll rect */
                    scroll_rect->x1 = foundRect.x1*64;
                    scroll_rect->x2 = foundRect.x2*64;
                    scroll_rect->y1 = foundRect.y1*64+best_offset;
                    scroll_rect->y2 = foundRect.y2*64+best_offset;

                }
            }

            free(scrolled_map);
        }
    }

    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }
    out_rect_index = 0;

    y = extents_rect.y1 & ~63;
    num_skips = 0;
    while (y < extents_rect.y2)
    {
        x = extents_rect.x1 & ~63;
        while (x < extents_rect.x2)
        {
            rect.x1 = x;
            rect.y1 = y;
            rect.x2 = rect.x1 + 64;
            rect.y2 = rect.y1 + 64;
            rcode = rdpRegionContainsRect(in_reg, &rect);
            LLOGLN(10, ("rdpCapture2: rcode %d", rcode));

            in_scroll_rect = (rect.x1 >= scroll_rect->x1) 
                && (rect.x2 <= scroll_rect->x2) 
                && (rect.y1 >= scroll_rect->y1) 
                && (rect.y2 <= scroll_rect->y2);

            if (rcode != rgnOUT)
            {
                /* hex digits of pi as a 64 bit int */
                if (rcode == rgnPART)
                {
                    LLOGLN(10, ("rdpCapture2: rgnPART"));
                    rdpFillBox_yuvalp(x, y, dst, dst_stride);
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionIntersect(&tile_reg, in_reg, &tile_reg);
                    rects = REGION_RECTS(&tile_reg);
                    num_rects = REGION_NUM_RECTS(&tile_reg);
                    crc = wyhash((const void*)rects, num_rects * sizeof(BoxRec), 
                        crc, _wyp);
                    rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  rects, num_rects);
                    crc_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
                    crc = 0x3243f6a8885a308dull;
                    crc = wyhash((const void*)crc_dst, 64 * 64 * 4, crc, _wyp);
                    rdpRegionUninit(&tile_reg);
                }
                else /* rgnIN */
                {
                    LLOGLN(10, ("rdpCapture2: rgnIN"));
                    crc = wyhash_rfx_tile_from_rows(
                        clientCon->rfx_tile_row_hashes, tile_row_stride, x, y);

                }
                crc_offset = (y / 64) * crc_stride + (x / 64);
                if (crc == clientCon->rfx_crcs[crc_offset])
                {
                    LLOGLN(10, ("rdpCapture2: crc skip at x %d y %d", x, y));
                    num_skips += 1;
                }
                else
                {
                    clientCon->rfx_crcs[crc_offset] = crc;
                    if(!in_scroll_rect)
                    {
                        /* lazily only do this if hash wasn't identical */
                        if (rcode != rgnPART)
                        {
                            rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                  src, src_stride,
                                  dst, dst_stride,
                                  &rect, 1);
                        }
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
            }
            x += 64;
        }
        y += 64;
    }
    *num_out_rects = out_rect_index;
    return TRUE;
}

/******************************************************************************/
/* make out_rects always multiple of 2 width and height */
static Bool
rdpCapture3(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
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

    LLOGLN(10, ("rdpCapture3:"));

    rv = TRUE;

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
        LLOGLN(0, ("rdpCapture3: unimplemented color conversion"));
    }

    return rv;
}

#if defined(XORGXRDP_GLAMOR)
/******************************************************************************/
static int
copy_vmem(rdpPtr dev, RegionPtr in_reg)
{
    PixmapPtr hwPixmap;
    PixmapPtr swPixmap;
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

    /* copy the dirty area from the screen hw pixmap to a sw pixmap
       this should do a dma */
    pScreen = dev->pScreen;
    hwPixmap = pScreen->GetScreenPixmap(pScreen);
    swPixmap = dev->screenSwPixmap;
    copyGC = GetScratchGC(dev->depth, pScreen);
    if (copyGC != NULL)
    {
        tmpval[0].val = GXcopy;
        ChangeGC(NullClient, copyGC, GCFunction, tmpval);
        ValidateGC(&(hwPixmap->drawable), copyGC);
        count = REGION_NUM_RECTS(in_reg);
        pbox = REGION_RECTS(in_reg);
        for (index = 0; index < count; index++)
        {
            left = pbox[index].x1;
            top = pbox[index].y1;
            width = pbox[index].x2 - pbox[index].x1;
            height = pbox[index].y2 - pbox[index].y1;
            if ((width > 0) && (height > 0))
            {
                LLOGLN(10, ("copy_vmem: hwPixmap tex 0x%8.8x "
                       "swPixmap tex 0x%8.8x",
                       glamor_get_pixmap_texture(hwPixmap),
                       glamor_get_pixmap_texture(swPixmap)));
                 copyGC->ops->CopyArea(&(hwPixmap->drawable),
                                       &(swPixmap->drawable),
                                       copyGC, left, top,
                                       width, height, left, top);
            }
        }
        FreeScratchGC(copyGC);
    }
    else
    {
        return 1;
    }
    return 0;
}
#endif

/**
 * Copy an array of rectangles from one memory area to another
 *****************************************************************************/
Bool
rdpCapture(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
           int *num_out_rects, BoxPtr scroll_rect, short *scroll_offset,
           struct image_data *id)
{
    int mode;

    LLOGLN(10, ("rdpCapture:"));
    mode = clientCon->client_info.capture_code;
    if (clientCon->dev->glamor)
    {
#if defined(XORGXRDP_GLAMOR)
        if (mode == 2)
        {
            return rdpEglCaptureRfx(clientCon, in_reg, out_rects,
                                    num_out_rects, id);
        }
        copy_vmem(clientCon->dev, in_reg);
#endif
    }
    switch (mode)
    {
        case 0:
            *scroll_offset = 0;
            return rdpCapture0(clientCon, in_reg, out_rects, num_out_rects, id);
        case 1:
            *scroll_offset = 0;
            return rdpCapture1(clientCon, in_reg, out_rects, num_out_rects, id);
        case 2:
            /* used for remotefx capture */
            return rdpCapture2(clientCon, in_reg, out_rects, num_out_rects,
                scroll_rect, scroll_offset, id);
        case 3:
            *scroll_offset = 0;
            /* used for even align capture */
            return rdpCapture3(clientCon, in_reg, out_rects, num_out_rects, id);
        default:
            LLOGLN(0, ("rdpCapture: mode %d not implemented", mode));
            break;
    }
    return FALSE;
}

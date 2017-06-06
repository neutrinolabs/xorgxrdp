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

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

#define RDP_MAX_TILES 4096

static const unsigned int g_crc_table[256] =
{
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

#define CRC_START(in_crc) (in_crc) = 0xFFFFFFFF
#define CRC_PASS(in_pixel, in_crc) \
    (in_crc) = g_crc_table[((in_crc) ^ (in_pixel)) & 0xff] ^ ((in_crc) >> 8)
#define CRC_END(in_crc) (in_crc) = ((in_crc) ^ 0xFFFFFFFF)

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
rdpCaptureCrcMem(const uint8_t *in_mem, int in_num_bytes, int in_crc)
{
    int index;

    index = 0;
    while (index < in_num_bytes)
    {
        CRC_PASS(in_mem[index], in_crc);
        index++;
    }
    return in_crc;
}

/******************************************************************************/
static Bool
rdpCapture2(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
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
    int crc;
    int num_crcs;

    LLOGLN(10, ("rdpCapture2:"));

    *out_rects = g_new(BoxRec, RDP_MAX_TILES);
    if (*out_rects == NULL)
    {
        return FALSE;
    }
    out_rect_index = 0;

    src = id->pixels;
    dst = id->shmem_pixels;
    src_stride = id->lineBytes;
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

    extents_rect = *rdpRegionExtents(in_reg);
    y = extents_rect.y1 & ~63;
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

            if (rcode != rgnOUT)
            {
                CRC_START(crc);
                if (rcode == rgnPART)
                {
                    LLOGLN(10, ("rdpCapture2: rgnPART"));
                    rdpFillBox_yuvalp(x, y, dst, dst_stride);
                    rdpRegionInit(&tile_reg, &rect, 0);
                    rdpRegionIntersect(&tile_reg, in_reg, &tile_reg);
                    rects = REGION_RECTS(&tile_reg);
                    num_rects = REGION_NUM_RECTS(&tile_reg);
                    crc = rdpCaptureCrcMem((uint8_t *) rects,
                                           num_rects * sizeof(BoxRec),
                                           crc);
                    rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  rects, num_rects);
                    rdpRegionUninit(&tile_reg);
                }
                else /* rgnIN */
                {
                    LLOGLN(10, ("rdpCapture2: rgnIN"));
                    rdpCopyBox_a8r8g8b8_to_yuvalp(x, y,
                                                  src, src_stride,
                                                  dst, dst_stride,
                                                  &rect, 1);
                }
                crc_dst = dst + (y << 8) * (dst_stride >> 8) + (x << 8);
                crc = rdpCaptureCrcMem(crc_dst, 64 * 64 * 4, crc);
                CRC_END(crc);
                crc_offset = (y / 64) * crc_stride + (x / 64);
                LLOGLN(10, ("rdpCapture2: crc 0x%8.8x 0x%8.8x",
                       crc, clientCon->rfx_crcs[crc_offset]));
                if (crc == clientCon->rfx_crcs[crc_offset])
                {
                    LLOGLN(10, ("rdpCapture2: crc skip at x %d y %d", x, y));
                }
                else
                {
                    clientCon->rfx_crcs[crc_offset] = crc;
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

/**
 * Copy an array of rectangles from one memory area to another
 *****************************************************************************/
Bool
rdpCapture(rdpClientCon *clientCon, RegionPtr in_reg, BoxPtr *out_rects,
           int *num_out_rects, struct image_data *id)
{
    int mode;

    LLOGLN(10, ("rdpCapture:"));
    mode = clientCon->client_info.capture_code;
    switch (mode)
    {
        case 0:
            return rdpCapture0(clientCon, in_reg, out_rects, num_out_rects, id);
        case 1:
            return rdpCapture1(clientCon, in_reg, out_rects, num_out_rects, id);
        case 2:
            /* used for remotefx capture */
            return rdpCapture2(clientCon, in_reg, out_rects, num_out_rects, id);
        case 3:
            /* used for even align capture */
            return rdpCapture3(clientCon, in_reg, out_rects, num_out_rects, id);
        default:
            LLOGLN(0, ("rdpCapture: mode %d not implemented", mode));
            break;
    }
    return FALSE;
}

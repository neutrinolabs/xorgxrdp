/*
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

yuv to rgb speed testing

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#if defined(USE_SIMD_AMD64)
#define a8r8g8b8_to_nv12_box_accel a8r8g8b8_to_nv12_box_amd64_sse2
#endif

#if defined(USE_SIMD_X86)
#define a8r8g8b8_to_nv12_box_accel a8r8g8b8_to_nv12_box_x86_sse2
#endif

/******************************************************************************/
//Y = ( (  66 * R + 129 * G +  25 * B + 128) >> 8) +  16
//U = ( ( -38 * R -  74 * G + 112 * B + 128) >> 8) + 128
//V = ( ( 112 * R -  94 * G -  18 * B + 128) >> 8) + 128

//C = Y - 16
//D = U - 128
//E = V - 128
//R = clip(( 298 * C           + 409 * E + 128) >> 8)
//G = clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)
//B = clip(( 298 * C + 516 * D           + 128) >> 8)

/******************************************************************************/
#define RDPCLAMP(_val, _lo, _hi) \
    (_val) < (_lo) ? (_lo) : (_val) > (_hi) ? (_hi) : (_val)

// floating point
#define YUV2RGB1(_Y, _U, _V, _R, _G, _B) \
  _Y  = (0.257 * _R) + (0.504 * _G) + (0.098 * _B) +  16; \
  _U = -(0.148 * _R) - (0.291 * _G) + (0.439 * _B) + 128; \
  _V =  (0.439 * _R) - (0.368 * _G) - (0.071 * _B) + 128;

#define YUV2RGB3(_Y, _U, _V, _R, _G, _B) \
  _Y = (( 1053 * _R + 2064 * _G +  401 * _B) >> 12) +  16; \
  _U = (( -606 * _R - 1192 * _G + 1798 * _B) >> 12) + 128; \
  _V = (( 1798 * _R - 1507 * _G -  291 * _B) >> 12) + 128;

#define YUV2RGB2(_Y, _U, _V, _R, _G, _B) \
  _Y = (( 16843 * _R + 33030 * _G +  6423 * _B) >> 16) +  16; \
  _U = (( -9699 * _R - 19071 * _G + 28770 * _B) >> 16) + 128; \
  _V = (( 28770 * _R - 24117 * _G -  4653 * _B) >> 16) + 128;

// original
#define YUV2RGB(_Y, _U, _V, _R, _G, _B) \
  _Y = (( 66 * _R + 129 * _G +  25 * _B + 128) >> 8) +  16; \
  _U = ((-38 * _R -  74 * _G + 112 * _B + 128) >> 8) + 128; \
  _V = ((112 * _R -  94 * _G -  18 * _B + 128) >> 8) + 128;

#define YUV2RGB4(_Y, _U, _V, _R, _G, _B) \
  _Y = ( ((1053 * ((_R) << 4)) >> 16) + ((2064 * ((_G) << 4)) >> 16) +  (( 401 * ((_B) << 4)) >> 16)) +  16; \
  _U = ( ((1798 * ((_B) << 4)) >> 16) - (( 606 * ((_R) << 4)) >> 16) -  ((1192 * ((_G) << 4)) >> 16)) + 128; \
  _V = ( ((1798 * ((_R) << 4)) >> 16) - ((1507 * ((_G) << 4)) >> 16) -  (( 291 * ((_B) << 4)) >> 16)) + 128;

/******************************************************************************/
static int
a8r8g8b8_to_nv12_box(char *s8, int src_stride,
                     char *d8_y, int dst_stride_y,
                     char *d8_uv, int dst_stride_uv,
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
    int lwidth;
    int lheight;
    int *s32a;
    int *s32b;
    char *d8ya;
    char *d8yb;
    char *d8uv;

    /* must be even */
    lwidth = width & ~1;
    lheight = height & ~1;
    for (jndex = 0; jndex < lheight; jndex += 2)
    {
        s32a = (int *) (s8 + src_stride * jndex);
        s32b = (int *) (s8 + src_stride * (jndex + 1));
        d8ya = d8_y + dst_stride_y * jndex;
        d8yb = d8_y + dst_stride_y * (jndex + 1);
        d8uv = d8_uv + dst_stride_uv * (jndex / 2);
        for (index = 0; index < lwidth; index += 2)
        {
            U_sum = 0;
            V_sum = 0;

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            YUV2RGB(Y, U, V, R, G, B);
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32a[0];
            s32a++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            YUV2RGB(Y, U, V, R, G, B);
            d8ya[0] = RDPCLAMP(Y, 0, 255);
            d8ya++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            YUV2RGB(Y, U, V, R, G, B);
            d8yb[0] = RDPCLAMP(Y, 0, 255);
            d8yb++;
            U_sum += RDPCLAMP(U, 0, 255);
            V_sum += RDPCLAMP(V, 0, 255);

            pixel = s32b[0];
            s32b++;
            R = (pixel >> 16) & 0xff;
            G = (pixel >>  8) & 0xff;
            B = (pixel >>  0) & 0xff;
            YUV2RGB(Y, U, V, R, G, B);
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

int output_params(void)
{
    return 0;
}

void hexdump(const void* p, int len)
{
    const unsigned char* line;
    int i;
    int thisline;
    int offset;

    line = (const unsigned char *)p;
    offset = 0;

    while (offset < len)
    {
        printf("%04x ", offset);
        thisline = len - offset;

        if (thisline > 16)
        {
            thisline = 16;
        }

        for (i = 0; i < thisline; i++)
        {
            printf("%02x ", line[i]);
        }

        for (; i < 16; i++)
        {
            printf("   ");
        }

        for (i = 0; i < thisline; i++)
        {
            printf("%c", (line[i] >= 0x20 && line[i] < 0x7f) ? line[i] : '.');
        }

        printf("\n");
        offset += thisline;
        line += thisline;
    }
}

int lmemcmp(const void* data1, const void* data2, int bytes, int* offset)
{
    int index;
    int diff;
    const unsigned char* ldata1;
    const unsigned char* ldata2;

    ldata1 = (const unsigned char*)data1;
    ldata2 = (const unsigned char*)data2;

    for (index = 0; index < bytes; index++)
    {
        diff = ldata1[index] - ldata2[index];
        if (abs(diff) > 0)
        {
            *offset = index;
            return 1;
        }
    }
    return 0;
}

int get_mstime(void)
{
    struct timeval tp;

    gettimeofday(&tp, 0);
    return (tp.tv_sec * 1000) + (tp.tv_usec / 1000);
}

int
a8r8g8b8_to_nv12_box_x86_sse2(char *s8, int src_stride,
                              char *d8_y, int dst_stride_y,
                              char *d8_uv, int dst_stride_uv,
                              int width, int height);
int
a8r8g8b8_to_nv12_box_amd64_sse2(char *s8, int src_stride,
                                char *d8_y, int dst_stride_y,
                                char *d8_uv, int dst_stride_uv,
                                int width, int height);

#define AL(_ptr) ((char*)((((size_t)_ptr) + 15) & ~15))

int main(int argc, char** argv)
{
    int index;
    int offset;
    int fd;
    int data_bytes;
    int stime;
    int etime;
    int ret = 0;
    char* rgb_data;
    char* yuv_data1;
    char* yuv_data2;
    char* al_rgb_data;
    char* al_yuv_data1;
    char* al_yuv_data2;

    if (argc == 1)
    {
        return output_params();
    }
    fd = open("/dev/urandom", O_RDONLY);
    data_bytes = 1920 * 1080 * 4;
    rgb_data = (char*)malloc(data_bytes + 16);
    al_rgb_data = AL(rgb_data);
    if (read(fd, al_rgb_data, data_bytes) != data_bytes)
    {
        printf("error\n");
    }
    close(fd);
    data_bytes = 1920 * 1080 * 2;
    yuv_data1 = (char*)malloc(data_bytes + 16);
    yuv_data2 = (char*)malloc(data_bytes + 16);
    al_yuv_data1 = AL(yuv_data1);
    al_yuv_data2 = AL(yuv_data2);
    stime = get_mstime();
    for (index = 0; index < 100; index++)
    {
        a8r8g8b8_to_nv12_box(al_rgb_data, 1920 * 4,
                             al_yuv_data1, 1920,
                             al_yuv_data1 + 1920 * 1080,
                             1920, 1920, 1080);
    }
    etime = get_mstime();
    printf("a8r8g8b8_to_nv12_box took %d\n", etime - stime);
    stime = get_mstime();
    for (index = 0; index < 100; index++)
    {
        a8r8g8b8_to_nv12_box_accel(al_rgb_data, 1920 * 4,
                                   al_yuv_data2, 1920,
                                   al_yuv_data2 + 1920 * 1080, 1920,
                                   1920, 1080);
    }
    etime = get_mstime();
    printf("a8r8g8b8_to_nv12_box_x86_sse2 took %d\n", etime - stime);
    if (lmemcmp(al_yuv_data1, al_yuv_data2, 1920 * 1080 * 3 / 2, &offset) != 0)
    {
        ret = 1;
        printf("no match at offset %d\n", offset);
        printf("first\n");
        hexdump(al_yuv_data1 + offset, 16);
        printf("second\n");
        hexdump(al_yuv_data2 + offset, 16);
    }
    else
    {
        printf("match\n");
    }
    free(rgb_data);
    free(yuv_data1);
    free(yuv_data2);
    return ret;
}

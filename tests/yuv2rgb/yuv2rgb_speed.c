/*
Copyright 2014-2015 Jay Sorg

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

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        return output_params();
    }
    return 0; 
}

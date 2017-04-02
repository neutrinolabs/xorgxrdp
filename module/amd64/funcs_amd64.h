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

amd64 asm functions

*/

#ifndef __FUNCS_AMD64_H
#define __FUNCS_AMD64_H

int
cpuid_amd64(int eax_in, int ecx_in, int *eax, int *ebx, int *ecx, int *edx);
int
yv12_to_rgb32_amd64_sse2(const uint8_t *yuvs, int width, int height, int *rgbs);
int
i420_to_rgb32_amd64_sse2(const uint8_t *yuvs, int width, int height, int *rgbs);
int
yuy2_to_rgb32_amd64_sse2(const uint8_t *yuvs, int width, int height, int *rgbs);
int
uyvy_to_rgb32_amd64_sse2(const uint8_t *yuvs, int width, int height, int *rgbs);
int
a8r8g8b8_to_a8b8g8r8_box_amd64_sse2(const uint8_t *s8, int src_stride,
                                    uint8_t *d8, int dst_stride,
                                    int width, int height);
int
a8r8g8b8_to_nv12_box_amd64_sse2(const uint8_t *s8, int src_stride,
                                uint8_t *d8_y, int dst_stride_y,
                                uint8_t *d8_uv, int dst_stride_uv,
                                int width, int height);

#endif


/*
 * Exercise the assembly ARGB-to-ABGR conversion independently of Xorg.
 * The assembly contract preserves alpha, unlike the software capture helper.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(USE_SIMD_AMD64)
#include "../../module/amd64/funcs_amd64.h"
#define convert a8r8g8b8_to_a8b8g8r8_box_amd64_sse2
#elif defined(USE_SIMD_X86)
#include "../../module/x86/funcs_x86.h"
#define convert a8r8g8b8_to_a8b8g8r8_box_x86_sse2
#else
#error A SIMD architecture must be selected
#endif

#define MAX_WIDTH 257
#define MAX_HEIGHT 7
#define MAX_PADDING 31
#define GUARD 64
#define BUFFER_SIZE ((MAX_WIDTH * 4 + MAX_PADDING) * MAX_HEIGHT + 2 * GUARD + 16)

static uint32_t random_state = 0x7e513f29;
static unsigned int cases;

static uint32_t
random_value(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static int
run_case(int width, int height, int src_alignment, int dst_alignment,
         int src_padding, int dst_padding, int reverse_src, int reverse_dst)
{
    uint8_t source_storage[BUFFER_SIZE + 15];
    uint8_t actual_storage[BUFFER_SIZE + 15];
    uint8_t source_copy[BUFFER_SIZE];
    uint8_t expected[BUFFER_SIZE];
    uint8_t *source;
    uint8_t *actual;
    int src_stride = width * 4 + src_padding;
    int dst_stride = width * 4 + dst_padding;
    int src_offset = GUARD + src_alignment;
    int dst_offset = GUARD + dst_alignment;
    int src_bytes = src_offset + src_stride * height + GUARD;
    int dst_bytes = dst_offset + dst_stride * height + GUARD;
    int index;
    int x;
    int y;
    int result;

    source = (uint8_t *) (((uintptr_t) source_storage + 15) & ~(uintptr_t) 15);
    actual = (uint8_t *) (((uintptr_t) actual_storage + 15) & ~(uintptr_t) 15);
    for (index = 0; index < src_bytes; ++index)
    {
        source[index] = (uint8_t) random_value();
    }
    memcpy(source_copy, source, src_bytes);
    memset(actual, 0xa5, dst_bytes);
    memset(expected, 0xa5, dst_bytes);
    if (reverse_src && height > 0)
    {
        src_offset += src_stride * (height - 1);
        src_stride = -src_stride;
    }
    if (reverse_dst && height > 0)
    {
        dst_offset += dst_stride * (height - 1);
        dst_stride = -dst_stride;
    }

    for (y = 0; y < height; ++y)
    {
        for (x = 0; x < width; ++x)
        {
            const uint8_t *pixel = source + src_offset + y * src_stride + x * 4;
            uint8_t *output = expected + dst_offset + y * dst_stride + x * 4;

            output[0] = pixel[2];
            output[1] = pixel[1];
            output[2] = pixel[0];
            output[3] = pixel[3];
        }
    }
    result = convert(source + src_offset, src_stride,
                     actual + dst_offset, dst_stride, width, height);
    ++cases;
    if (result != 0 || memcmp(expected, actual, dst_bytes) != 0 ||
            memcmp(source_copy, source, src_bytes) != 0)
    {
        fprintf(stderr, "ARGB conversion failed: %dx%d align=%d/%d "
                "stride=%d/%d result=%d\n", width, height,
                src_alignment, dst_alignment, src_stride, dst_stride, result);
        return 1;
    }
    return 0;
}

int
main(void)
{
    static const int widths[] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 257};
    static const int padding[][2] = {{0, 0}, {4, 12}, {1, 7}};
    unsigned int w;
    unsigned int p;
    int src_alignment;
    int dst_alignment;
    int height;
    int index;

    /* Empty boxes must not touch either pointer, even for a zero height. */
    if (convert(NULL, 0, NULL, 0, 0, 1) != 0 ||
            convert(NULL, 0, NULL, 0, 8, 0) != 0 ||
            convert(NULL, 0, NULL, 0, 0, 0) != 0 ||
            convert(NULL, 0, NULL, 0, -1, 1) != 0 ||
            convert(NULL, 0, NULL, 0, 8, -1) != 0)
    {
        fprintf(stderr, "ARGB conversion failed for an empty box\n");
        return 1;
    }

    for (src_alignment = 0; src_alignment < 16; ++src_alignment)
    {
        for (dst_alignment = 0; dst_alignment < 16; ++dst_alignment)
        {
            for (w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w)
            {
                for (p = 0; p < sizeof(padding) / sizeof(padding[0]); ++p)
                {
                    for (height = 0; height <= 3; ++height)
                    {
                        if (run_case(widths[w], height,
                                     src_alignment, dst_alignment,
                                     padding[p][0], padding[p][1], 0, 0))
                        {
                            return 1;
                        }
                    }
                }
            }
        }
    }

    /* Odd row strides exercise changing alignments; reversed rows exercise
     * the signed stride arguments. Alpha and all padding bytes are checked. */
    for (index = 0; index < 2000; ++index)
    {
        int width = random_value() % (MAX_WIDTH + 1);
        int rows = random_value() % (MAX_HEIGHT + 1);
        int src_alignment = random_value() % 16;
        int dst_alignment = random_value() % 16;
        int src_padding = random_value() % (MAX_PADDING + 1);
        int dst_padding = random_value() % (MAX_PADDING + 1);
        int reverse_src = random_value() & 1;
        int reverse_dst = random_value() & 1;

        if (run_case(width, rows, src_alignment, dst_alignment,
                     src_padding, dst_padding, reverse_src, reverse_dst))
        {
            return 1;
        }
    }
    printf("ARGB-to-ABGR: %u byte-exact cases passed\n", cases);
    return 0;
}

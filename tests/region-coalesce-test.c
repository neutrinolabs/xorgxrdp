/*
 * Copyright 2026
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pixman.h>

#include "../module/rdpCoalesce.h"

/*****************************************************************************/
static uint64_t
regionArea(pixman_region16_t *region)
{
    pixman_box16_t *boxes;
    uint64_t area;
    int count;
    int index;

    area = 0;
    boxes = pixman_region_rectangles(region, &count);
    for (index = 0; index < count; ++index)
    {
        area += (uint64_t)(boxes[index].x2 - boxes[index].x1) *
                (boxes[index].y2 - boxes[index].y1);
    }
    return area;
}

/*****************************************************************************/
static uint64_t
checkRegion(pixman_region16_t *original, int limit)
{
    struct rdp_coalesce_box *input;
    struct rdp_coalesce_box *output;
    pixman_box16_t *rects;
    pixman_box16_t *result_rects;
    pixman_region16_t result;
    pixman_region16_t missing;
    pixman_box16_t extents;
    uint64_t area;
    int count;
    int out_count;
    int canonical_count;
    int index;

    rects = pixman_region_rectangles(original, &count);
    input = calloc(count + 1, sizeof(*input));
    output = calloc(limit + 1, sizeof(*output));
    result_rects = calloc(limit + 1, sizeof(*result_rects));
    assert(input != NULL && output != NULL && result_rects != NULL);
    for (index = 0; index < count; ++index)
    {
        input[index].x1 = rects[index].x1;
        input[index].y1 = rects[index].y1;
        input[index].x2 = rects[index].x2;
        input[index].y2 = rects[index].y2;
    }
    out_count = rdpCoalesceRects(input, count, output, limit);
    assert(out_count >= 0 && out_count <= limit);
    assert(output[limit].x1 == 0 && output[limit].y1 == 0 &&
           output[limit].x2 == 0 && output[limit].y2 == 0);
    for (index = 0; index < out_count; ++index)
    {
        result_rects[index].x1 = output[index].x1;
        result_rects[index].y1 = output[index].y1;
        result_rects[index].x2 = output[index].x2;
        result_rects[index].y2 = output[index].y2;
        if (index > 0)
        {
            if (output[index - 1].y1 == output[index].y1)
            {
                assert(output[index - 1].y2 == output[index].y2);
                assert(output[index - 1].x2 <= output[index].x1);
            }
            else
            {
                assert(output[index - 1].y2 <= output[index].y1);
            }
        }
    }
    assert(pixman_region_init_rects(&result, result_rects, out_count));
    pixman_region_rectangles(&result, &canonical_count);
    assert(canonical_count <= limit);
    pixman_region_init(&missing);
    assert(pixman_region_subtract(&missing, original, &result));
    assert(!pixman_region_not_empty(&missing));
    if (count > 0)
    {
        extents = *pixman_region_extents(original);
        assert(memcmp(&extents, pixman_region_extents(&result),
                      sizeof(extents)) == 0);
    }
    if (count <= limit)
    {
        assert(pixman_region_equal(original, &result));
    }
    area = regionArea(&result);

    /* Production uses the helper in place. Exercise that API as well. */
    assert(rdpCoalesceRects(input, count, input, limit) == out_count);
    assert(memcmp(input, output, out_count * sizeof(*input)) == 0);
    free(result_rects);
    free(output);
    free(input);
    pixman_region_fini(&missing);
    pixman_region_fini(&result);
    return area;
}

/*****************************************************************************/
static uint32_t
nextRandom(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

/*****************************************************************************/
int
main(void)
{
    pixman_region16_t region;
    pixman_box16_t boxes[256];
    uint32_t state;
    uint64_t area;
    int index;
    int trial;
    int count;
    int limit;
    int x;
    int y;

    assert(rdpCoalesceRects(NULL, 0, NULL, 15) == 0);
    assert(rdpCoalesceRects(NULL, -1, NULL, 15) == -1);
    assert(rdpCoalesceRects(NULL, 1, NULL, 15) == -1);
    assert(rdpCoalesceRects(NULL, 0, NULL, 0) == -1);
    pixman_region_init(&region);
    checkRegion(&region, 15);
    pixman_region_fini(&region);

    /* Sixteen small updates at opposite corners must stay sparse. */
    for (index = 0; index < 16; ++index)
    {
        x = index < 8 ? index * 16 : 3704 + (index - 8) * 16;
        y = index < 8 ? 0 : 2152;
        boxes[index] = (pixman_box16_t) {x, y, x + 8, y + 8};
    }
    assert(pixman_region_init_rects(&region, boxes, 16));
    area = checkRegion(&region, 15);
    assert(area == 16 * 64 + 8 * 8);
    printf("Sparse 4K damage: 1024 dirty pixels -> %llu capture pixels\n",
           (unsigned long long)area);
    checkRegion(&region, 1);
    pixman_region_fini(&region);

    /* An unfinished band must not overlap a previous vertically merged band. */
    for (index = 0; index < 15; ++index)
    {
        boxes[index] = (pixman_box16_t) {index, index * 2, index + 1, index * 2 + 1};
    }
    for (index = 15; index < 45; ++index)
    {
        x = (index - 15) * 4;
        boxes[index] = (pixman_box16_t) {x, 40, x + 1, 41};
    }
    assert(pixman_region_init_rects(&region, boxes, 45));
    for (limit = 1; limit <= 15; ++limit)
    {
        checkRegion(&region, limit);
    }
    pixman_region_fini(&region);

    /* Keep area arithmetic correct across the entire signed 16-bit range. */
    boxes[0] = (pixman_box16_t) {-32768, -32768, -32760, -32760};
    boxes[1] = (pixman_box16_t) {32759, 32759, 32767, 32767};
    assert(pixman_region_init_rects(&region, boxes, 2));
    assert(checkRegion(&region, 1) == UINT64_C(65535) * 65535);
    pixman_region_fini(&region);

    state = 1;
    for (trial = 0; trial < 1000; ++trial)
    {
        count = nextRandom(&state) % 256 + 1;
        for (index = 0; index < count; ++index)
        {
            x = (int)(nextRandom(&state) % 2000) - 1000;
            y = (int)(nextRandom(&state) % 2000) - 1000;
            boxes[index].x1 = x;
            boxes[index].y1 = y;
            boxes[index].x2 = x + nextRandom(&state) % 100 + 1;
            boxes[index].y2 = y + nextRandom(&state) % 100 + 1;
        }
        assert(pixman_region_init_rects(&region, boxes, count));
        checkRegion(&region, nextRandom(&state) % 15 + 1);
        pixman_region_fini(&region);
    }
    puts("Region coalescing coverage and rectangle-limit tests passed");
    return 0;
}

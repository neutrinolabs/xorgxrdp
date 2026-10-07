/*
 * Copyright 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rdpCoalesce.h"

/*****************************************************************************/
static uint64_t
boxArea(const struct rdp_coalesce_box *box)
{
    return (uint64_t)(box->x2 - box->x1) * (box->y2 - box->y1);
}

/*****************************************************************************/
static void
includeBox(struct rdp_coalesce_box *bounds,
           const struct rdp_coalesce_box *box)
{
    if (box->x1 < bounds->x1)
    {
        bounds->x1 = box->x1;
    }
    if (box->y1 < bounds->y1)
    {
        bounds->y1 = box->y1;
    }
    if (box->x2 > bounds->x2)
    {
        bounds->x2 = box->x2;
    }
    if (box->y2 > bounds->y2)
    {
        bounds->y2 = box->y2;
    }
}

/*****************************************************************************/
/* Find the merge which adds the fewest pixels per rectangle eliminated. */
static int
mergeBoxes(struct rdp_coalesce_box *boxes, int count, int band_complete)
{
    struct rdp_coalesce_box bounds;
    struct rdp_coalesce_box best_bounds;
    uint64_t area;
    uint64_t cost;
    uint64_t best_cost;
    int best_start;
    int best_end;
    int band_start;
    int band_end;
    int next_end;
    int index;

    best_start = 0;
    best_end = 0;
    best_cost = 0;
    best_bounds = boxes[0];
    band_start = 0;
    while (band_start < count)
    {
        band_end = band_start + 1;
        while (band_end < count && boxes[band_end].y1 == boxes[band_start].y1)
        {
            ++band_end;
        }

        /* Filling a horizontal gap preserves the existing band boundaries. */
        for (index = band_start; index + 1 < band_end; ++index)
        {
            bounds = boxes[index];
            bounds.x2 = boxes[index + 1].x2;
            cost = boxArea(&bounds) - boxArea(&boxes[index])
                   - boxArea(&boxes[index + 1]);
            if (best_end == best_start ||
                    cost * (best_end - best_start) < best_cost)
            {
                best_start = index;
                best_end = index + 1;
                best_cost = cost;
                best_bounds = bounds;
            }
        }

        if (band_end < count)
        {
            next_end = band_end + 1;
            while (next_end < count && boxes[next_end].y1 == boxes[band_end].y1)
            {
                ++next_end;
            }
            /*
             * Wait until the last input band is complete before extending it
             * upward. Otherwise later boxes from that band could overlap boxes
             * with different y bounds, and canonicalization could exceed the
             * rectangle limit again.
             */
            if (next_end < count || band_complete)
            {
                bounds = boxes[band_start];
                area = 0;
                for (index = band_start; index < next_end; ++index)
                {
                    includeBox(&bounds, &boxes[index]);
                    area += boxArea(&boxes[index]);
                }
                cost = boxArea(&bounds) - area;
                if (best_end == best_start ||
                        cost * (best_end - best_start) <
                        best_cost * (next_end - band_start - 1))
                {
                    best_start = band_start;
                    best_end = next_end - 1;
                    best_cost = cost;
                    best_bounds = bounds;
                }
            }
        }
        band_start = band_end;
    }

    boxes[best_start] = best_bounds;
    memmove(boxes + best_start + 1, boxes + best_end + 1,
            (count - best_end - 1) * sizeof(*boxes));
    return count - (best_end - best_start);
}

/*****************************************************************************/
int
rdpCoalesceRects(const struct rdp_coalesce_box *input, int num_rects,
                 struct rdp_coalesce_box *output, int max_rects)
{
    struct rdp_coalesce_box *boxes;
    struct rdp_coalesce_box bounds;
    int index;
    int count;
    int band_complete;

    if (num_rects < 0 || max_rects < 1 ||
            (num_rects > 0 && (input == NULL || output == NULL)))
    {
        return -1;
    }
    if (num_rects <= max_rects)
    {
        if (num_rects > 0 && input != output)
        {
            memcpy(output, input, num_rects * sizeof(*output));
        }
        return num_rects;
    }
    if (max_rects == 1)
    {
        bounds = input[0];
        for (index = 1; index < num_rects; ++index)
        {
            includeBox(&bounds, &input[index]);
        }
        output[0] = bounds;
        return 1;
    }
    if ((size_t)max_rects >= SIZE_MAX / sizeof(*boxes))
    {
        return -1;
    }
    boxes = malloc(((size_t)max_rects + 1) * sizeof(*boxes));
    if (boxes == NULL)
    {
        return -1;
    }

    count = 0;
    for (index = 0; index < num_rects; ++index)
    {
        boxes[count++] = input[index];
        if (count > max_rects)
        {
            band_complete = index + 1 == num_rects ||
                            input[index + 1].y1 != input[index].y1;
            count = mergeBoxes(boxes, count, band_complete);
        }
    }
    memcpy(output, boxes, count * sizeof(*output));
    free(boxes);
    return count;
}

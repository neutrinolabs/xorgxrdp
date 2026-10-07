/*
 * Copyright 2026
 * SPDX-License-Identifier: MIT
 */

#ifndef RDP_COALESCE_H
#define RDP_COALESCE_H

#include <stdint.h>

struct rdp_coalesce_box
{
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
};

/*
 * Enlarge a canonical, y-banded rectangle list to at most max_rects boxes.
 * The output remains y-banded, covers every input box, and stays within the
 * input extents. Input and output may be the same array. Returns the output
 * count, or -1 on allocation failure / invalid arguments.
 */
int
rdpCoalesceRects(const struct rdp_coalesce_box *input, int num_rects,
                 struct rdp_coalesce_box *output, int max_rects);

#endif

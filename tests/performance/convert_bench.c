/*
 * Copyright 2026 xorgxrdp contributors
 * SPDX-License-Identifier: MIT
 *
 * Compare the original and current AMD64 conversion in the same process.
 * Link the two assembly objects after renaming their exported function to
 * convert_before / convert_after with objcopy --redefine-sym.
 *
 * This measures conversion throughput on reused frame buffers, not RDP FPS.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int convert_before(const uint8_t *, int, uint8_t *, int, int, int);
extern int convert_after(const uint8_t *, int, uint8_t *, int, int, int);

typedef int (*convert_fn)(const uint8_t *, int, uint8_t *, int, int, int);

struct scenario
{
    const char *name;
    int width;
    int height;
    int source_stride;
    int destination_stride;
    int destination_offset;
};

static const struct scenario scenarios[] =
{
    {"1080p_aligned", 1920, 1080, 7680, 7680, 0},
    {"1080p_destination_plus4", 1920, 1080, 7680, 7680, 4},
    /* Different pitches that retain 16-byte alignment are a control. */
    {"1080p_different_strides", 1920, 1080, 8192, 7808, 0},
    /* Word-aligned rows cycle through matched and mismatched alignments. */
    {"1080p_rotating_alignment", 1920, 1080, 7684, 7692, 0},
    {"4k_aligned", 3840, 2160, 15360, 15360, 0},
    {"4k_destination_plus4", 3840, 2160, 15360, 15360, 4},
    {"4k_different_strides", 3840, 2160, 16384, 15488, 0},
    {"4k_rotating_alignment", 3840, 2160, 15364, 15372, 0}
};

struct buffers
{
    uint8_t *source_allocation;
    uint8_t *destination_allocation;
    uint8_t *source;
    uint8_t *destination;
    size_t source_bytes;
    size_t destination_bytes;
};

struct sample
{
    uint64_t elapsed_ns;
    int position;
};

static convert_fn converters[] = {convert_before, convert_after};
static const char *variant_names[] = {"before", "after"};

static void
fail(const char *message)
{
    fprintf(stderr, "convert_bench: %s\n", message);
    exit(1);
}

static void *
allocate(size_t bytes)
{
    void *result = NULL;
    if (posix_memalign(&result, 64, bytes) != 0)
    {
        fail("cannot allocate frame buffer");
    }
    return result;
}

static uint64_t
now_ns(void)
{
    struct timespec time;
#ifdef CLOCK_MONOTONIC_RAW
    const clockid_t clock = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clock = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clock, &time) != 0)
    {
        fail("clock_gettime failed");
    }
    return (uint64_t) time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static void
initialize(const struct scenario *scenario, struct buffers *buffers)
{
    const size_t guard = 64;
    uint32_t state = UINT32_C(0x7e513f29);
    size_t i;

    buffers->source_bytes = guard +
                            (size_t) scenario->source_stride * scenario->height +
                            guard;
    buffers->destination_bytes = guard + scenario->destination_offset +
                                 (size_t) scenario->destination_stride *
                                 scenario->height + guard;
    buffers->source_allocation = allocate(buffers->source_bytes);
    buffers->destination_allocation = allocate(buffers->destination_bytes);
    buffers->source = buffers->source_allocation + guard;
    buffers->destination = buffers->destination_allocation + guard +
                           scenario->destination_offset;

    for (i = 0; i < buffers->source_bytes; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        buffers->source_allocation[i] = (uint8_t) state;
    }
}

static int
convert(convert_fn fn, const struct scenario *scenario,
        const struct buffers *buffers)
{
    return fn(buffers->source, scenario->source_stride,
              buffers->destination, scenario->destination_stride,
              scenario->width, scenario->height);
}

static void
verify(const struct scenario *scenario, const struct buffers *buffers)
{
    uint8_t *expected = allocate(buffers->destination_bytes);
    uint8_t *source_copy = allocate(buffers->source_bytes);
    size_t destination_offset = buffers->destination -
                                buffers->destination_allocation;
    int x;
    int y;
    int variant;

    memcpy(source_copy, buffers->source_allocation, buffers->source_bytes);
    memset(expected, 0xa5, buffers->destination_bytes);
    for (y = 0; y < scenario->height; ++y)
    {
        for (x = 0; x < scenario->width; ++x)
        {
            const uint8_t *pixel = buffers->source +
                                   (size_t) y * scenario->source_stride + x * 4;
            uint8_t *output = expected + destination_offset +
                              (size_t) y * scenario->destination_stride + x * 4;
            output[0] = pixel[2];
            output[1] = pixel[1];
            output[2] = pixel[0];
            output[3] = pixel[3];
        }
    }
    for (variant = 0; variant < 2; ++variant)
    {
        memset(buffers->destination_allocation, 0xa5, buffers->destination_bytes);
        if (convert(converters[variant], scenario, buffers) != 0 ||
                memcmp(expected, buffers->destination_allocation,
                       buffers->destination_bytes) != 0 ||
                memcmp(source_copy, buffers->source_allocation,
                       buffers->source_bytes) != 0)
        {
            fprintf(stderr, "convert_bench: byte-exact verification failed: "
                    "%s %s\n", scenario->name, variant_names[variant]);
            exit(1);
        }
        printf("check,%s,%s,0,0,1,0,0,\n",
               scenario->name, variant_names[variant]);
    }
    free(expected);
    free(source_copy);
}

static uint64_t
measure(convert_fn fn, const struct scenario *scenario,
        const struct buffers *buffers, int frames)
{
    uint64_t start = now_ns();
    uint64_t elapsed;
    int result = 0;
    int frame;
    for (frame = 0; frame < frames; ++frame)
    {
        result |= convert(fn, scenario, buffers);
    }
    elapsed = now_ns() - start;
    if (result != 0 || elapsed == 0)
    {
        fail("conversion or timing failed");
    }
    return elapsed;
}

static int
compare_double(const void *left, const void *right)
{
    double a = *(const double *) left;
    double b = *(const double *) right;
    return (a > b) - (a < b);
}

static double
median(const struct sample *samples, int rounds, int frames)
{
    double *sorted = allocate((size_t) rounds * sizeof(*sorted));
    double result;
    int i;
    for (i = 0; i < rounds; ++i)
    {
        sorted[i] = (double) samples[i].elapsed_ns / frames;
    }
    qsort(sorted, rounds, sizeof(*sorted), compare_double);
    result = rounds % 2 ? sorted[rounds / 2] :
             (sorted[rounds / 2 - 1] + sorted[rounds / 2]) / 2;
    free(sorted);
    return result;
}

static void
benchmark(const struct scenario *scenario, const struct buffers *buffers,
          int rounds, int target_ms, unsigned int case_index)
{
    struct sample *samples[2];
    uint64_t calibration[2];
    double slower_frame_ns;
    double medians[2];
    int frames;
    int variant;
    int round;
    int position;

    for (variant = 0; variant < 2; ++variant)
    {
        int warmup;
        samples[variant] = allocate((size_t) rounds * sizeof(struct sample));
        for (warmup = 0; warmup < 4; ++warmup)
        {
            if (convert(converters[variant], scenario, buffers) != 0)
            {
                fail("warmup conversion failed");
            }
        }
        calibration[variant] = measure(converters[variant], scenario, buffers, 20);
    }
    slower_frame_ns = (double) (calibration[0] > calibration[1] ?
                               calibration[0] : calibration[1]) / 20;
    /* Identical work for both variants. Calibrate against the slower variant
     * to keep each before/after pair near or below 100 ms by default. */
    frames = (int) ((double) target_ms * 1000000 / slower_frame_ns + 0.5);
    if (frames < 20)
    {
        frames = 20;
    }
    if (frames > 10000)
    {
        frames = 10000;
    }
    for (round = 0; round < rounds; ++round)
    {
        int first = (round + case_index) % 2;
        for (position = 0; position < 2; ++position)
        {
            variant = first ^ position;
            samples[variant][round].elapsed_ns =
                measure(converters[variant], scenario, buffers, frames);
            samples[variant][round].position = position + 1;
        }
    }
    /* Emit samples after measurement so output I/O cannot perturb a pair. */
    for (round = 0; round < rounds; ++round)
    {
        for (variant = 0; variant < 2; ++variant)
        {
            const struct sample *sample = &samples[variant][round];
            printf("sample,%s,%s,%d,%d,%d,%" PRIu64 ",%.3f,\n",
                   scenario->name, variant_names[variant], round + 1,
                   sample->position, frames, sample->elapsed_ns,
                   (double) sample->elapsed_ns / frames);
        }
    }
    medians[0] = median(samples[0], rounds, frames);
    medians[1] = median(samples[1], rounds, frames);
    for (variant = 0; variant < 2; ++variant)
    {
        printf("summary,%s,%s,0,0,%d,0,%.3f,%.6f\n",
               scenario->name, variant_names[variant], frames, medians[variant],
               medians[0] / medians[variant]);
        free(samples[variant]);
    }
}

static int
parse_number(const char *text, int minimum, int maximum)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
            value < minimum || value > maximum)
    {
        fail("invalid numeric argument");
    }
    return (int) value;
}

int
main(int argc, char **argv)
{
    int rounds = 9;
    int target_ms = 50;
    int check_only = 0;
    int argument;
    unsigned int i;

    for (argument = 1; argument < argc; ++argument)
    {
        if (strcmp(argv[argument], "--check-only") == 0)
        {
            check_only = 1;
        }
        else if (strcmp(argv[argument], "--rounds") == 0 && argument + 1 < argc)
        {
            rounds = parse_number(argv[++argument], 9, 999);
        }
        else if (strcmp(argv[argument], "--target-ms") == 0 && argument + 1 < argc)
        {
            target_ms = parse_number(argv[++argument], 1, 1000);
        }
        else
        {
            fprintf(stderr, "usage: %s [--check-only] [--rounds N>=9] "
                    "[--target-ms N]\n", argv[0]);
            return 1;
        }
    }
    puts("record,case,variant,round,position,frames,total_ns,ns_per_frame,speedup");
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        struct buffers buffers;
        initialize(&scenarios[i], &buffers);
        verify(&scenarios[i], &buffers);
        if (!check_only)
        {
            benchmark(&scenarios[i], &buffers, rounds, target_ms, i);
        }
        free(buffers.source_allocation);
        free(buffers.destination_allocation);
    }
    return 0;
}

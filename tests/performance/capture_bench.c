/*
 * Copyright 2026 xorgxrdp contributors
 * SPDX-License-Identifier: MIT
 *
 * Build with capture_extract.py. Production function bodies are generated
 * from the selected Git revision, rather than duplicated in this harness.
 */

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pixman.h>

#include "capture_config.h"
#include "wyhash.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); \
} } while (0)
#define TRUE 1
#define FALSE 0
#define NullBox NULL
#define LOG(...) ((void)0)
#define g_memset memset
#define g_new(type, count) ((type *)malloc(sizeof(type) * (count)))
#define g_new0(type, count) ((type *)calloc((count), sizeof(type)))
#define rgnOUT PIXMAN_REGION_OUT
#define rgnPART PIXMAN_REGION_PART
#define rgnIN PIXMAN_REGION_IN
#define REGION_NUM_RECTS(region) pixman_region_n_rects(region)
#define REGION_RECTS(region) regionRects(region)

typedef int Bool;
typedef pixman_box16_t BoxRec;
typedef BoxRec *BoxPtr;
typedef pixman_region16_t RegionRec;
typedef RegionRec *RegionPtr;
typedef int (*copy_box_proc)(const uint8_t *, int, uint8_t *, int, int, int);
enum xrdp_encoder_flags { KEY_FRAME_REQUESTED = 1 };

struct bench_device
{
    int glamor;
    int nvidia;
    void *screenSwPixmap;
    copy_box_proc a8r8g8b8_to_yuvalp_box;
};
typedef struct bench_device *rdpPtr;

typedef struct
{
    struct bench_device *dev;
    enum shared_memory_status shmemstatus;
    int num_rfx_crcs_alloc[16];
    uint64_t *rfx_crcs[16];
    RegionPtr dirtyRegion;
    int send_key_frame[16];
} rdpClientCon;

struct image_data
{
    const uint8_t *pixels;
    uint8_t *shmem_pixels;
    int lineBytes;
    int left;
    int top;
    int width;
    int height;
    int flags;
};

struct capture_metrics
{
    int input_rectangles;
    uint64_t input_pixels;
    int capture_rectangles;
    uint64_t capture_pixels;
    int emitted_tiles;
    int emitted_dirty_rectangles;
    uint64_t emitted_dirty_pixels;
};

static struct capture_metrics *record_metrics;
static RegionPtr original_damage;

static BoxPtr
regionRects(RegionPtr region)
{
    int count;
    return pixman_region_rectangles(region, &count);
}

static uint64_t
regionArea(RegionPtr region)
{
    BoxPtr rects = REGION_RECTS(region);
    uint64_t area = 0;
    int index;
    for (index = 0; index < REGION_NUM_RECTS(region); ++index)
    {
        area += (uint64_t)(rects[index].x2 - rects[index].x1) *
                (rects[index].y2 - rects[index].y1);
    }
    return area;
}

static void
rdpRegionInit(RegionPtr region, BoxPtr box, int size)
{
    if (box == NULL)
    {
        pixman_region_init(region);
    }
    else
    {
        pixman_region_init_with_extents(region, box);
    }
}

static RegionPtr
rdpRegionCreate(BoxPtr box, int size)
{
    RegionPtr region = malloc(sizeof(*region));
    CHECK(region != NULL);
    rdpRegionInit(region, box, size);
    return region;
}

static void
rdpRegionDestroy(RegionPtr region)
{
    pixman_region_fini(region);
    free(region);
}

#define rdpRegionCopy pixman_region_copy
#define rdpRegionTranslate pixman_region_translate
#define rdpRegionIntersect pixman_region_intersect
#define rdpRegionSubtract pixman_region_subtract
#define rdpRegionUnion pixman_region_union
#define rdpRegionContainsRect pixman_region_contains_rectangle
#define rdpRegionExtents pixman_region_extents
#define rdpRegionReset pixman_region_reset
#define rdpRegionUninit pixman_region_fini

/* These benchmarks deliberately cover CPU capture only. */
static Bool
rdpCopyBoxList(rdpClientCon *clientCon, void *pixmap, BoxPtr boxes,
               int count, int sx, int sy, int dx, int dy)
{
    CHECK(!"GPU readback is outside this benchmark");
    return FALSE;
}

/* rdpCapRect and rdpCapture live in separate production translation units.
 * Preserve that boundary here: otherwise the extraction allows GCC to inline
 * the entire capture/hash loop into rdpCapRect and unrelated region changes
 * can alter its register allocation, producing artificial control regressions.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static Bool
rdpCapture(rdpClientCon *, RegionPtr, BoxPtr *, int *, struct image_data *);
static int
rdpClientConSendPaintRectShmFd(rdpPtr, rdpClientCon *, struct image_data *,
                               RegionPtr, BoxPtr, int);

#if BENCH_SSE2
extern int
a8r8g8b8_to_yuvalp_box_amd64_sse2(const uint8_t *, int, uint8_t *, int, int, int);
#endif

/* Actual rdpCapRect, rdpCaptureGfxPro, converters and coalescer. */
#include "capture_generated.inc"

static Bool
rdpCapture(rdpClientCon *client, RegionPtr dirty, BoxPtr *out_rects,
           int *out_count, struct image_data *id)
{
    Bool result;
    if (record_metrics != NULL)
    {
        RegionRec missing;
        record_metrics->capture_rectangles = REGION_NUM_RECTS(dirty);
        record_metrics->capture_pixels = regionArea(dirty);
        pixman_region_init(&missing);
        CHECK(pixman_region_subtract(&missing, original_damage, dirty));
        CHECK(!pixman_region_not_empty(&missing));
        CHECK(REGION_NUM_RECTS(dirty) <= MAX_CAPTURE_RECTS);
        pixman_region_fini(&missing);
    }
    result = rdpCaptureGfxPro(client, dirty, out_rects, out_count, id);
    CHECK(result);
    return result;
}

static int
rdpClientConSendPaintRectShmFd(rdpPtr dev, rdpClientCon *client,
                               struct image_data *id, RegionPtr dirty,
                               BoxPtr copy_rects, int count)
{
    if (record_metrics != NULL)
    {
        record_metrics->emitted_tiles = count;
        record_metrics->emitted_dirty_rectangles = REGION_NUM_RECTS(dirty);
        record_metrics->emitted_dirty_pixels = regionArea(dirty);
    }
    return 0;
}

static uint64_t
nowNs(void)
{
    struct timespec time;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static void
jsonString(const char *value)
{
    putchar('"');
    for (; *value; ++value)
    {
        if (*value == '"' || *value == '\\')
        {
            putchar('\\');
        }
        if ((unsigned char)*value < 32)
        {
            printf("\\u%04x", (unsigned char)*value);
        }
        else
        {
            putchar(*value);
        }
    }
    putchar('"');
}

/* Verify every originally damaged output pixel against the same production
 * converter, and return a digest comparable between baseline/current runs. */
static uint64_t
verifyPixels(struct bench_device *dev, struct image_data *id, RegionPtr damage)
{
    uint8_t expected[64 * 64 * 4];
    BoxPtr rects = REGION_RECTS(damage);
    uint64_t digest = UINT64_C(14695981039346656037);
    int count = REGION_NUM_RECTS(damage);
    int tile_columns = (id->width + 63) / 64;
    int index;
    int x;
    int y;
    int row;
    int col;
    int plane;

    for (index = 0; index < count; ++index)
    {
        for (y = rects[index].y1; y < rects[index].y2; y += 64)
        {
            for (x = rects[index].x1; x < rects[index].x2; x += 64)
            {
                int width = rects[index].x2 - x;
                int height = rects[index].y2 - y;
                if (width > 64)
                {
                    width = 64;
                }
                if (height > 64)
                {
                    height = 64;
                }
                CHECK(dev->a8r8g8b8_to_yuvalp_box(
                          id->pixels + y * id->lineBytes + x * 4,
                          id->lineBytes, expected, 64, width, height) == 0);
                for (plane = 0; plane < 4; ++plane)
                {
                    for (row = 0; row < height; ++row)
                    {
                        for (col = 0; col < width; ++col)
                        {
                            int px = x + col;
                            int py = y + row;
                            size_t tile = (py / 64) * tile_columns + px / 64;
                            size_t offset = tile * 16384 + plane * 4096 +
                                            (py % 64) * 64 + px % 64;
                            uint8_t actual = id->shmem_pixels[offset];
                            CHECK(actual == expected[plane * 4096 + row * 64 + col]);
                            digest = (digest ^ actual) * UINT64_C(1099511628211);
                        }
                    }
                }
            }
        }
    }
    return digest;
}

static void
initDamage(RegionPtr damage, const char *name, int width, int height)
{
    BoxRec boxes[32];
    int count;
    int half;
    int index;
    if (strcmp(name, "fullframe") == 0)
    {
        pixman_region_init_rect(damage, 0, 0, width, height);
        return;
    }
    count = strcmp(name, "sparse10") == 0 ? 10 : 32;
    half = count / 2;
    for (index = 0; index < count; ++index)
    {
        int x = index < half ? index * 16 :
                width - 8 - (count - index - 1) * 16;
        int y = index < half ? 0 : height - 8;
        boxes[index] = (BoxRec) {x, y, x + 8, y + 8};
    }
    CHECK(pixman_region_init_rects(damage, boxes, count));
    CHECK(REGION_NUM_RECTS(damage) == count);
}

static void
runCase(const char *name, int changed, int iterations, int warmup, int samples)
{
    const int width = 3840;
    const int height = 2160;
    const size_t pixels = (size_t)width * height;
    const size_t destination_bytes = (size_t)((width + 63) & ~63) *
                                     ((height + 63) & ~63) * 4;
    uint32_t *sources[2];
    uint8_t *destination = calloc(1, destination_bytes);
    RegionRec damage;
    RegionRec dirty;
    struct bench_device dev = {0};
    rdpClientCon client = {0};
    struct image_data id = {0};
    struct capture_metrics metrics = {0};
    BoxRec screen = {0, 0, width, height};
    BoxPtr rects;
    uint64_t *times = calloc(samples + 1, sizeof(*times));
    uint64_t digest;
    size_t pixel;
    int index;
    int x;
    int y;
    int sample;
    int iteration;
    unsigned int frame = 0;

    sources[0] = malloc(pixels * 4);
    sources[1] = malloc(pixels * 4);
    CHECK(sources[0] != NULL && sources[1] != NULL && destination != NULL && times != NULL);
    initDamage(&damage, name, width, height);
    pixman_region_init(&dirty);
    for (pixel = 0; pixel < pixels; ++pixel)
    {
        sources[0][pixel] = UINT32_C(0xff000000) |
                            ((uint32_t)pixel * UINT32_C(2654435761) & UINT32_C(0xffffff));
    }
    memcpy(sources[1], sources[0], pixels * 4);
    rects = REGION_RECTS(&damage);
    for (index = 0; index < REGION_NUM_RECTS(&damage); ++index)
    {
        for (y = rects[index].y1; y < rects[index].y2; ++y)
        {
            for (x = rects[index].x1; x < rects[index].x2; ++x)
            {
                sources[1][y * width + x] ^= UINT32_C(0xffffff);
            }
        }
    }
#if BENCH_SSE2
    dev.a8r8g8b8_to_yuvalp_box = a8r8g8b8_to_yuvalp_box_amd64_sse2_wrap;
#else
    dev.a8r8g8b8_to_yuvalp_box = a8r8g8b8_to_yuvalp_box;
#endif
    client.dev = &dev;
    client.shmemstatus = SHM_RFX_ACTIVE;
    client.dirtyRegion = &dirty;
    id.shmem_pixels = destination;
    id.lineBytes = width * 4;
    id.width = width;
    id.height = height;
    original_damage = &damage;

    /* Prime caches with frame A, then validate A (unchanged) or B (changed). */
    for (iteration = 0; iteration < 2; ++iteration)
    {
        CHECK(pixman_region_copy(&dirty, &damage));
        id.pixels = (const uint8_t *)sources[changed ? frame++ & 1 : 0];
        record_metrics = &metrics;
        CHECK(rdpCapRect(&client, &screen, 0, &id) == 0);
        CHECK(!pixman_region_not_empty(&dirty));
        digest = verifyPixels(&dev, &id, &damage);
    }
    record_metrics = NULL;
    metrics.input_rectangles = REGION_NUM_RECTS(&damage);
    metrics.input_pixels = regionArea(&damage);
    CHECK(changed ? metrics.emitted_tiles > 0 : metrics.emitted_tiles == 0);

    for (iteration = 0; iteration < warmup; ++iteration)
    {
        CHECK(pixman_region_copy(&dirty, &damage));
        id.pixels = (const uint8_t *)sources[changed ? frame++ & 1 : 0];
        CHECK(rdpCapRect(&client, &screen, 0, &id) == 0);
    }
    for (sample = 0; sample < samples; ++sample)
    {
        for (iteration = 0; iteration < iterations; ++iteration)
        {
            uint64_t start;
            CHECK(pixman_region_copy(&dirty, &damage));
            id.pixels = (const uint8_t *)sources[changed ? frame++ & 1 : 0];
            start = nowNs();
            CHECK(rdpCapRect(&client, &screen, 0, &id) == 0);
            times[sample] += nowNs() - start;
        }
    }

    printf("{\"case\":\"%s_%s\",\"width\":%d,\"height\":%d,", name,
           changed ? "changed" : "unchanged", width, height);
    printf("\"input_rectangles\":%d,\"input_pixels\":%" PRIu64 ",",
           metrics.input_rectangles, metrics.input_pixels);
    printf("\"capture_rectangles\":%d,\"capture_pixels\":%" PRIu64 ",",
           metrics.capture_rectangles, metrics.capture_pixels);
    printf("\"emitted_tiles\":%d,\"emitted_dirty_rectangles\":%d,",
           metrics.emitted_tiles, metrics.emitted_dirty_rectangles);
    printf("\"emitted_dirty_pixels\":%" PRIu64 ",\"validation_digest\":\"%016" PRIx64 "\",",
           metrics.emitted_dirty_pixels, digest);
    printf("\"iterations_per_sample\":%d,\"warmup_iterations\":%d,\"samples_ns_total\":[",
           iterations, warmup);
    for (sample = 0; sample < samples; ++sample)
    {
        printf("%s%" PRIu64, sample ? "," : "", times[sample]);
    }
    printf("],\"samples_ns_per_iteration\":[");
    for (sample = 0; sample < samples; ++sample)
    {
        printf("%s%.3f", sample ? "," : "", (double)times[sample] / iterations);
    }
    printf("]}");

    for (index = 0; index < 16; ++index)
    {
        free(client.rfx_crcs[index]);
    }
    pixman_region_fini(&dirty);
    pixman_region_fini(&damage);
    free(destination);
    free(sources[0]);
    free(sources[1]);
    free(times);
}

static int
positiveNumber(const char *value, int allow_zero)
{
    char *end;
    long number = strtol(value, &end, 10);
    CHECK(*value != '\0' && *end == '\0' && number >= (allow_zero ? 0 : 1) && number <= 1000000);
    return (int)number;
}

int
main(int argc, char **argv)
{
    const char *names[] = {"sparse10", "sparse32", "fullframe"};
    const char *selected_case = "all";
    const char *selected_mode = "all";
    int iterations = 32;
    int warmup = 3;
    int samples = 1;
    int verify_only = 0;
    int index;
    int changed;
    int first = 1;

    for (index = 1; index < argc; ++index)
    {
        if (strcmp(argv[index], "--verify-only") == 0)
        {
            verify_only = 1;
        }
        else if (index + 1 < argc && strcmp(argv[index], "--iterations") == 0)
        {
            iterations = positiveNumber(argv[++index], 0);
        }
        else if (index + 1 < argc && strcmp(argv[index], "--warmup") == 0)
        {
            warmup = positiveNumber(argv[++index], 1);
        }
        else if (index + 1 < argc && strcmp(argv[index], "--samples") == 0)
        {
            samples = positiveNumber(argv[++index], 0);
        }
        else if (index + 1 < argc && strcmp(argv[index], "--case") == 0)
        {
            selected_case = argv[++index];
        }
        else if (index + 1 < argc && strcmp(argv[index], "--mode") == 0)
        {
            selected_mode = argv[++index];
        }
        else
        {
            fprintf(stderr, "Usage: %s [--case all|sparse10|sparse32|fullframe] "
                    "[--mode all|changed|unchanged] [--iterations N] [--warmup N] "
                    "[--samples N] [--verify-only]\n", argv[0]);
            return 1;
        }
    }
    CHECK(strcmp(selected_case, "all") == 0 || strcmp(selected_case, "sparse10") == 0 ||
          strcmp(selected_case, "sparse32") == 0 || strcmp(selected_case, "fullframe") == 0);
    CHECK(strcmp(selected_mode, "all") == 0 || strcmp(selected_mode, "changed") == 0 ||
          strcmp(selected_mode, "unchanged") == 0);
    if (verify_only)
    {
        samples = 0;
        warmup = 0;
    }
    printf("{\"benchmark\":\"production_region_and_rfx_capture\",\"revision\":");
    jsonString(BENCH_REVISION);
    printf(",\"backend\":");
    jsonString(BENCH_BACKEND);
    printf(",\"cflags\":");
    jsonString(BENCH_CFLAGS);
    printf(",\"coalescing\":%s,\"pixman_version\":", BENCH_COALESCING ? "true" : "false");
    jsonString(pixman_version_string());
    printf(",\"scope\":\"actual rdpCapRect region preparation and rdpCaptureGfxPro CPU "
           "capture; noinline capture shim preserves production translation-unit boundary; "
           "network send stubbed; damage reset, source selection and validation "
           "outside timing\",\"cases\":[");
    for (index = 0; index < 3; ++index)
    {
        if (strcmp(selected_case, "all") != 0 && strcmp(selected_case, names[index]) != 0)
        {
            continue;
        }
        for (changed = 0; changed < 2; ++changed)
        {
            if ((strcmp(selected_mode, "changed") == 0 && !changed) ||
                    (strcmp(selected_mode, "unchanged") == 0 && changed))
            {
                continue;
            }
            if (!first)
            {
                putchar(',');
            }
            first = 0;
            runCase(names[index], changed, iterations, warmup, samples);
        }
    }
    puts("]}");
    return 0;
}

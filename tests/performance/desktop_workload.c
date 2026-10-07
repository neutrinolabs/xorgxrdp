/* Deterministic X11 source for an actual RDP client frame-rate measurement.
 * Build: cc -O2 -std=c11 -Wall -Wextra desktop_workload.c -o desktop_workload \
 *           $(pkg-config --cflags --libs x11)
 * See desktop_workload.md for the marker and timestamp contract.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MARKER_X 32
#define MARKER_MARGIN_Y 32
#define MARKER_CELL 8
#define MARKER_BITS 64
#define MARKER_WIDTH (MARKER_BITS * MARKER_CELL)
#define MARKER_HEIGHT (2 * MARKER_CELL)
#define SPARSE_COUNT 32
#define SPARSE_SIZE 16
#define NS_SECOND UINT64_C(1000000000)

struct record
{
    uint32_t frame_id;
    int measured;
    uint64_t target_ns;
    uint64_t generation_begin_ns;
    uint64_t ready_ns;
    uint64_t submit_ns;
    uint64_t flushed_ns;
    uint64_t server_done_ns;
};

struct options
{
    const char *display;
    const char *prefix;
    const char *mode;
    int width;
    int height;
    int fps;
    int seconds;
    int warmup;
    int hold;
    uint64_t start_ns;
};

static void
fail(const char *message)
{
    fprintf(stderr, "desktop_workload: %s\n", message);
    exit(1);
}

static uint64_t
now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    {
        fail("clock_gettime failed");
    }
    return (uint64_t)value.tv_sec * NS_SECOND + (uint64_t)value.tv_nsec;
}

static void
sleep_until(uint64_t ns)
{
    struct timespec value = {(time_t)(ns / NS_SECOND), (long)(ns % NS_SECOND)};
    int result;
    do
    {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &value, NULL);
    }
    while (result == EINTR);
    if (result != 0)
    {
        fail("clock_nanosleep failed");
    }
}

static uint8_t
marker_crc(uint64_t value)
{
    unsigned int crc = 0x5a;
    int byte;
    int bit;
    for (byte = 0; byte < 5; ++byte)
    {
        crc ^= (unsigned int)(value >> (byte * 8)) & 255;
        for (bit = 0; bit < 8; ++bit)
        {
            crc = ((crc << 1) ^ ((crc & 128) ? 7 : 0)) & 255;
        }
    }
    return (uint8_t)crc;
}

static uint64_t
marker_packet(uint32_t frame_id)
{
    uint64_t value = frame_id;
    return value | ((uint64_t)marker_crc(value) << 40) |
           (UINT64_C(0xb67a) << 48);
}

static void
paint_marker(uint32_t *pixels, int stride, int x0, int y0, uint64_t packet)
{
    int row;
    int bit;
    int x;
    int y;
    for (row = 0; row < 2; ++row)
    {
        for (bit = 0; bit < MARKER_BITS; ++bit)
        {
            uint32_t color = (((packet >> bit) & 1) ^ row) ? 0xffffff : 0;
            for (y = 0; y < MARKER_CELL; ++y)
            {
                uint32_t *line = pixels + (y0 + row * MARKER_CELL + y) * stride;
                for (x = 0; x < MARKER_CELL; ++x)
                {
                    line[x0 + bit * MARKER_CELL + x] = color;
                }
            }
        }
    }
}

static uint32_t
pattern_pixel(int x, int y)
{
    /* Smooth color contours, fine texture and document-like panels provide
     * both spatial detail and compression-friendly structure. This is a
     * synthetic moving desktop/video texture, not a video-decoder benchmark. */
    unsigned int grain = ((unsigned int)x * 1103515245u ^
                          (unsigned int)y * 12345u) >> 27;
    unsigned int r = 38 + ((x / 7 + y / 13 + grain) % 172);
    unsigned int g = 42 + ((x / 19 + y / 5 + grain) % 174);
    unsigned int b = 52 + ((x / 11 + y / 17 + grain) % 164);
    int px = x % 320;
    int py = y % 240;
    if (px < 5 || py < 5)
    {
        r /= 2;
        g /= 2;
        b /= 2;
    }
    else if (px > 28 && px < 248 && py > 32 && py < 166)
    {
        r = 224;
        g = 229;
        b = 236;
        if (py % 19 < 3 && px < 210 - (py / 19 % 4) * 12)
        {
            r = 48;
            g = 62;
            b = 79;
        }
    }
    return (r << 16) | (g << 8) | b;
}

static uint32_t *
make_texture(int width, int height)
{
    uint32_t *texture = malloc((size_t)width * height * sizeof(*texture));
    int x;
    int y;
    if (texture == NULL)
    {
        fail("texture allocation failed");
    }
    for (y = 0; y < height; ++y)
    {
        for (x = 0; x < width; ++x)
        {
            texture[(size_t)y * width + x] = pattern_pixel(x, y);
        }
    }
    return texture;
}

static void
render_motion(XImage *image, const uint32_t *texture, uint32_t frame_id)
{
    int width = image->width;
    int height = image->height;
    int shift_x = (int)(((uint64_t)frame_id * 5) % (unsigned int)width);
    int shift_y = (int)(((uint64_t)frame_id * 3) % (unsigned int)height);
    int y;
    for (y = 0; y < height; ++y)
    {
        const uint32_t *source = texture + (size_t)((y + shift_y) % height) * width;
        uint32_t *target = (uint32_t *)(image->data + (size_t)y * image->bytes_per_line);
        memcpy(target, source + shift_x, (size_t)(width - shift_x) * sizeof(*target));
        memcpy(target + width - shift_x, source, (size_t)shift_x * sizeof(*target));
    }
    paint_marker((uint32_t *)image->data, image->bytes_per_line / 4,
                 MARKER_X, MARKER_MARGIN_Y, marker_packet(frame_id));
    paint_marker((uint32_t *)image->data, image->bytes_per_line / 4,
                 MARKER_X, height - MARKER_MARGIN_Y - MARKER_HEIGHT,
                 marker_packet(frame_id));
}

static XImage *
make_image(Display *display, int width, int height)
{
    int screen = DefaultScreen(display);
    XImage *image = XCreateImage(display, DefaultVisual(display, screen),
                                DefaultDepth(display, screen), ZPixmap, 0,
                                NULL, width, height, 32, 0);
    uint32_t order = 1;
    if (image == NULL || image->bits_per_pixel != 32 ||
            image->red_mask != 0xff0000 || image->green_mask != 0xff00 ||
            image->blue_mask != 0xff ||
            image->byte_order != (*(uint8_t *)&order ? LSBFirst : MSBFirst))
    {
        fail("requires native-byte-order 32-bit RGB888 XImage");
    }
    image->data = calloc((size_t)image->bytes_per_line, height);
    if (image->data == NULL)
    {
        fail("image allocation failed");
    }
    return image;
}

static unsigned int
number(const char *text, unsigned int maximum, int zero_allowed)
{
    char *end;
    unsigned long result;
    errno = 0;
    result = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || result > maximum ||
            (!zero_allowed && result == 0))
    {
        fail("invalid numeric argument");
    }
    return (unsigned int)result;
}

static int
self_test(void)
{
    uint32_t pixels[MARKER_WIDTH * MARKER_HEIGHT];
    static const uint32_t ids[] = {0, 1, 255, 256, 65535, 0xffffff};
    size_t index;
    for (index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index)
    {
        uint64_t expected = marker_packet(ids[index]);
        uint64_t row[2] = {0, 0};
        int y;
        int bit;
        paint_marker(pixels, MARKER_WIDTH, 0, 0, expected);
        for (y = 0; y < 2; ++y)
        {
            for (bit = 0; bit < MARKER_BITS; ++bit)
            {
                uint32_t value = pixels[(y * MARKER_CELL + MARKER_CELL / 2) *
                                        MARKER_WIDTH + bit * MARKER_CELL + MARKER_CELL / 2];
                row[y] |= (uint64_t)(value != 0) << bit;
            }
        }
        if (row[0] != expected || row[1] != ~expected ||
                expected >> 48 != 0xb67a ||
                ((expected >> 40) & 255) != marker_crc(expected) ||
                (expected & 0xffffff) != ids[index])
        {
            fail("marker self-test failed");
        }
    }
    puts("desktop_workload marker self-test passed");
    return 0;
}

static FILE *
output_file(const char *prefix, const char *suffix)
{
    size_t length = strlen(prefix) + strlen(suffix) + 1;
    char *path = malloc(length);
    FILE *file;
    if (path == NULL)
    {
        fail("filename allocation failed");
    }
    snprintf(path, length, "%s%s", prefix, suffix);
    file = fopen(path, "w");
    if (file == NULL)
    {
        perror(path);
        exit(1);
    }
    free(path);
    return file;
}

int
main(int argc, char **argv)
{
    struct options options = {NULL, NULL, "fullframe", 1920, 1080, 60, 20, 3, 2, 0};
    Display *display;
    Window window;
    Pixmap back = None;
    XSetWindowAttributes attributes = {0};
    GC gc;
    XImage *image;
    XImage *marker;
    uint32_t *texture;
    struct record *records;
    size_t record_capacity;
    size_t count = 0;
    size_t measured_count = 0;
    uint64_t start;
    uint64_t end;
    uint64_t measurement_start;
    uint64_t slot = 0;
    uint64_t skipped_slots = 0;
    uint64_t finished;
    int fullframe;
    int index;
    FILE *csv;
    FILE *summary;
    for (index = 1; index < argc; ++index)
    {
        if (strcmp(argv[index], "--self-test") == 0)
        {
            return self_test();
        }
        if (index + 1 >= argc)
        {
            fail("every option needs a value; see desktop_workload.md");
        }
        if (strcmp(argv[index], "--display") == 0)
            options.display = argv[++index];
        else if (strcmp(argv[index], "--output-prefix") == 0)
            options.prefix = argv[++index];
        else if (strcmp(argv[index], "--mode") == 0)
            options.mode = argv[++index];
        else if (strcmp(argv[index], "--width") == 0)
            options.width = number(argv[++index], 8192, 0);
        else if (strcmp(argv[index], "--height") == 0)
            options.height = number(argv[++index], 8192, 0);
        else if (strcmp(argv[index], "--fps") == 0)
            options.fps = number(argv[++index], 240, 0);
        else if (strcmp(argv[index], "--seconds") == 0)
            options.seconds = number(argv[++index], 300, 0);
        else if (strcmp(argv[index], "--warmup") == 0)
            options.warmup = number(argv[++index], 60, 1);
        else if (strcmp(argv[index], "--hold-seconds") == 0)
            options.hold = number(argv[++index], 30, 1);
        else if (strcmp(argv[index], "--start-ns") == 0)
        {
            char *tail;
            errno = 0;
            options.start_ns = strtoull(argv[++index], &tail, 10);
            if (errno != 0 || tail == argv[index] || *tail != '\0' || options.start_ns == 0)
                fail("invalid --start-ns");
        }
        else
            fail("unknown option; see desktop_workload.md");
    }
    if (options.display == NULL || options.prefix == NULL)
        fail("--display and --output-prefix are required");
    if (options.width < MARKER_X + MARKER_WIDTH || options.height < 240)
        fail("dimensions must be at least 544x240 for the markers and sparse cells");
    if (strcmp(options.mode, "fullframe") != 0 && strcmp(options.mode, "sparse32") != 0)
        fail("--mode must be fullframe or sparse32");
    {
        const char *colon = strrchr(options.display, ':');
        char *tail;
        long display_number;
        if (colon == NULL || colon[1] < '0' || colon[1] > '9')
            fail("use an explicit private X11 display");
        display_number = strtol(colon + 1, &tail, 10);
        if ((*tail != '\0' && *tail != '.') || display_number == 0 || display_number == 10)
            fail("refusing the user's display :0 or :10; use a private test display");
    }
    fullframe = strcmp(options.mode, "fullframe") == 0;
    display = XOpenDisplay(options.display);
    if (display == NULL)
        fail("cannot open the requested display");
    if (DisplayWidth(display, DefaultScreen(display)) != options.width ||
            DisplayHeight(display, DefaultScreen(display)) != options.height)
        fail("display dimensions must exactly match the requested workload");
    attributes.override_redirect = True;
    window = XCreateWindow(display, DefaultRootWindow(display), 0, 0,
                            options.width, options.height, 0, CopyFromParent,
                            InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
    XStoreName(display, window, "xorgxrdp isolated performance workload");
    XMapRaised(display, window);
    gc = XCreateGC(display, window, 0, NULL);
    if (fullframe)
        back = XCreatePixmap(display, window, options.width, options.height,
                             DefaultDepth(display, DefaultScreen(display)));
    image = make_image(display, options.width, options.height);
    marker = make_image(display, MARKER_WIDTH, MARKER_HEIGHT);
    texture = make_texture(options.width, options.height);
    render_motion(image, texture, 0);
    XPutImage(display, fullframe ? back : window, gc, image, 0, 0, 0, 0,
              options.width, options.height);
    if (fullframe)
        XCopyArea(display, back, window, gc, 0, 0, options.width, options.height, 0, 0);
    XSync(display, False);
    record_capacity = (size_t)(options.seconds + options.warmup) * options.fps + 1;
    records = calloc(record_capacity, sizeof(*records));
    if (records == NULL)
        fail("frame record allocation failed");
    start = options.start_ns != 0 ? options.start_ns : now_ns() + NS_SECOND;
    if (start <= now_ns())
        fail("--start-ns must be in the future after display setup");
    measurement_start = start + (uint64_t)options.warmup * NS_SECOND;
    end = measurement_start + (uint64_t)options.seconds * NS_SECOND;
    fprintf(stderr, "WORKLOAD_READY display=%s mode=%s window=0x%lx start_ns=%" PRIu64
            " measurement_start_ns=%" PRIu64 " end_ns=%" PRIu64 "\n",
            options.display, options.mode, window, start, measurement_start, end);
    fflush(stderr);
    while (1)
    {
        uint64_t target = start + slot * NS_SECOND / (unsigned int)options.fps;
        uint64_t current;
        struct record *record;
        uint32_t colors[SPARSE_COUNT];
        if (target >= end)
            break;
        sleep_until(target);
        current = now_ns();
        if (current >= end)
            break;
        /* Drop missed source slots rather than catch up with bursts. */
        if (current > target + NS_SECOND / (unsigned int)options.fps)
        {
            uint64_t current_slot = (current - start) * (unsigned int)options.fps / NS_SECOND;
            skipped_slots += current_slot - slot;
            slot = current_slot;
            target = start + slot * NS_SECOND / (unsigned int)options.fps;
        }
        if (count >= record_capacity || count + 1 > 0xffffff)
            fail("frame record capacity exceeded");
        record = &records[count];
        record->frame_id = (uint32_t)count + 1;
        record->target_ns = target;
        record->generation_begin_ns = now_ns();
        if (fullframe)
        {
            render_motion(image, texture, record->frame_id);
        }
        else
        {
            for (index = 0; index < SPARSE_COUNT; ++index)
            {
                uint32_t phase = record->frame_id + (uint32_t)index * 19;
                colors[index] = (((phase * 17) & 255) << 16) |
                                (((phase * 29) & 255) << 8) | ((phase * 43) & 255);
            }
            paint_marker((uint32_t *)marker->data, marker->bytes_per_line / 4,
                         0, 0, marker_packet(record->frame_id));
        }
        record->ready_ns = now_ns();
        record->submit_ns = now_ns();
        record->measured = record->submit_ns >= measurement_start && record->submit_ns < end;
        if (fullframe)
        {
            /* XPutImage may span many protocol requests. Stage the complete
             * frame offscreen, then present it with one server-side copy so
             * capture cannot see an image halfway through the upload. */
            XPutImage(display, back, gc, image, 0, 0, 0, 0, options.width, options.height);
            XCopyArea(display, back, window, gc, 0, 0, options.width, options.height, 0, 0);
        }
        else
        {
            for (index = 0; index < SPARSE_COUNT; ++index)
            {
                int x = 24 + (index % 8) * (options.width - 48 - SPARSE_SIZE) / 7;
                int y = 96 + (index / 8) * (options.height - 192 - SPARSE_SIZE) / 3;
                /* Distinct GC colors prevent Xlib combining the rectangle
                 * requests; these are 32 separate drawing operations. */
                XSetForeground(display, gc, colors[index]);
                XFillRectangle(display, window, gc, x, y, SPARSE_SIZE, SPARSE_SIZE);
            }
            XPutImage(display, window, gc, marker, 0, 0, MARKER_X, MARKER_MARGIN_Y,
                      MARKER_WIDTH, MARKER_HEIGHT);
            XPutImage(display, window, gc, marker, 0, 0, MARKER_X,
                      options.height - MARKER_MARGIN_Y - MARKER_HEIGHT,
                      MARKER_WIDTH, MARKER_HEIGHT);
        }
        XFlush(display);
        record->flushed_ns = now_ns();
        XSync(display, False);
        record->server_done_ns = now_ns();
        measured_count += record->measured;
        ++count;
        ++slot;
    }
    finished = now_ns();
    csv = output_file(options.prefix, ".frames.csv");
    fputs("frame_id,phase,target_ns,generation_begin_ns,ready_ns,submit_ns,flushed_ns,server_done_ns\n", csv);
    for (size_t row = 0; row < count; ++row)
    {
        const struct record *record = &records[row];
        fprintf(csv, "%u,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 "\n", record->frame_id,
                record->measured ? "measured" : "warmup", record->target_ns,
                record->generation_begin_ns, record->ready_ns, record->submit_ns,
                record->flushed_ns, record->server_done_ns);
    }
    if (fclose(csv) != 0)
        fail("frame CSV write failed");
    summary = output_file(options.prefix, ".summary.json");
    fprintf(summary, "{\"workload\":\"%s\",\"width\":%d,\"height\":%d,"
            "\"target_fps\":%d,\"warmup_seconds\":%d,\"measured_seconds\":%d,"
            "\"clock\":\"CLOCK_MONOTONIC\",\"start_ns\":%" PRIu64 ","
            "\"measurement_start_ns\":%" PRIu64 ",\"measurement_end_ns\":%" PRIu64 ","
            "\"finished_ns\":%" PRIu64 ",\"frames_generated\":%zu,"
            "\"measured_frames_generated\":%zu,\"source_fps\":%.6f,"
            "\"skipped_source_slots\":%" PRIu64 ",\"marker_pixels_per_frame\":%d,"
            "\"sparse_rectangles\":%d,\"sparse_pixels_per_frame\":%d,"
            "\"presentation\":\"%s\",\"sync_each_frame\":true}\n",
            options.mode, options.width, options.height,
            options.fps, options.warmup, options.seconds, start, measurement_start, end,
            finished, count, measured_count, (double)measured_count / options.seconds,
            skipped_slots, 2 * MARKER_WIDTH * MARKER_HEIGHT,
            fullframe ? 0 : SPARSE_COUNT,
            fullframe ? 0 : SPARSE_COUNT * SPARSE_SIZE * SPARSE_SIZE,
            fullframe ? "offscreen_upload_then_copy" : "separate_rectangles");
    if (fclose(summary) != 0)
        fail("summary JSON write failed");
    fprintf(stderr, "WORKLOAD_DONE frames=%zu measured_frames=%zu skipped_slots=%" PRIu64 "\n",
            count, measured_count, skipped_slots);
    fflush(stderr);
    /* Preserve the final markers while the real RDP decoder drains. */
    sleep_until(now_ns() + (uint64_t)options.hold * NS_SECOND);
    free(records);
    free(texture);
    XDestroyImage(marker);
    XDestroyImage(image);
    if (back != None)
        XFreePixmap(display, back);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

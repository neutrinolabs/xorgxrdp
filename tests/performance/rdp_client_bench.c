/*
 * Headless FreeRDP 3 client for end-to-end desktop benchmarks.
 *
 * This uses FreeRDP's software GDI and real GFX decoders. Timestamps describe
 * the decoded/composed framebuffer, not presentation on a physical display.
 * The marker matches desktop_workload.c (two CRC-protected 64-bit patches).
 *
 * Build with pkg-config --cflags --libs freerdp-client3. Run --help for options.
 * SIGINT/SIGTERM stop the event loop and write the summary.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <freerdp/client.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/constants.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <winpr/synch.h>

typedef struct
{
    rdpClientContext common;
    FILE *frames;
    const char *ready_path;
    const char *codec;
    pcRdpgfxEndFrame decode_end_frame;
    pcRdpgfxSurfaceCommand decode_surface;
    uint64_t end_frames;
    uint64_t unique_frames;
    uint64_t duplicate_frames;
    uint64_t invalid_frames;
    uint64_t torn_frames;
    uint64_t regression_frames;
    uint64_t surface_commands;
    uint64_t encoded_surface_bytes;
    uint64_t frame_bytes;
    uint64_t codec_commands[16];
    uint64_t first_unique_ns;
    uint64_t last_unique_ns;
    uint32_t last_marker;
    uint32_t frame_codec;
    int ready;
    _Atomic int io_error;
} BenchContext;

static volatile sig_atomic_t stopped;

static uint64_t
now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void
stop_handler(int signo)
{
    (void)signo;
    stopped = 1;
}

static uint8_t
crc40(uint64_t value)
{
    unsigned int crc = 0x5a;
    unsigned int i;
    unsigned int j;
    for (i = 0; i < 5; ++i)
    {
        crc ^= (unsigned int)((value >> (i * 8)) & 255);
        for (j = 0; j < 8; ++j)
        {
            crc = ((crc << 1) ^ ((crc & 128) ? 7 : 0)) & 255;
        }
    }
    return (uint8_t)crc;
}

/* XRGB32 is read through FreeRDP's format helper to avoid byte-order assumptions. */
static int
read_marker(const rdpGdi *gdi, unsigned int y, uint64_t *packet)
{
    uint64_t value = 0;
    unsigned int bit;
    for (bit = 0; bit < 64; ++bit)
    {
        unsigned int x = 36 + 8 * bit;
        uint32_t a = FreeRDPReadColor(gdi->primary_buffer +
                                    (size_t)(y + 4) * gdi->stride + x * 4,
                                    PIXEL_FORMAT_XRGB32);
        uint32_t b = FreeRDPReadColor(gdi->primary_buffer +
                                    (size_t)(y + 12) * gdi->stride + x * 4,
                                    PIXEL_FORMAT_XRGB32);
        int av = ((a & 255) + ((a >> 8) & 255) + ((a >> 16) & 255)) > 384;
        int bv = ((b & 255) + ((b >> 8) & 255) + ((b >> 16) & 255)) > 384;
        if (av == bv)
        {
            return 0;
        }
        value |= (uint64_t)av << bit;
    }
    if ((value >> 48) != UINT64_C(0xb67a) ||
        ((value >> 40) & 255) != crc40(value) ||
        ((value >> 24) & 65535) != 0)
    {
        return 0;
    }
    *packet = value;
    return 1;
}

static void
write_ready(BenchContext *bench, uint64_t timestamp)
{
    FILE *fp;
    char *temporary;
    rdpContext *context = &bench->common.context;
    if (bench->ready || bench->io_error)
    {
        return;
    }
    temporary = malloc(strlen(bench->ready_path) + 5);
    if (temporary == NULL)
    {
        bench->io_error = 1;
        return;
    }
    sprintf(temporary, "%s.tmp", bench->ready_path);
    fp = fopen(temporary, "w");
    if (fp == NULL)
    {
        perror(bench->ready_path);
        bench->io_error = 1;
        free(temporary);
        return;
    }
    fprintf(fp, "{\"stage\":\"first_decoded_gfx_frame\",\"monotonic_ns\":%" PRIu64
            ",\"width\":%d,\"height\":%d,\"requested_codec\":\"%s\"}\n",
            timestamp, context->gdi->width, context->gdi->height, bench->codec);
    if (fclose(fp) != 0)
    {
        bench->io_error = 1;
    }
    if (!bench->io_error && rename(temporary, bench->ready_path) != 0)
    {
        perror(bench->ready_path);
        bench->io_error = 1;
    }
    if (bench->io_error) remove(temporary);
    free(temporary);
    bench->ready = !bench->io_error;
}

static UINT
bench_end_frame(RdpgfxClientContext *gfx, const RDPGFX_END_FRAME_PDU *frame)
{
    rdpGdi *gdi = (rdpGdi *)gfx->custom;
    BenchContext *bench = (BenchContext *)gdi->context;
    UINT rc = bench->decode_end_frame(gfx, frame);
    uint64_t timestamp;
    uint64_t top = 0;
    uint64_t bottom = 0;
    int64_t marker = -1;
    const char *status;
    if (rc != CHANNEL_RC_OK)
    {
        return rc;
    }
    /* gdi_EndFrame has decoded and composed every preceding surface command. */
    timestamp = now_ns();
    ++bench->end_frames;
    if (gdi->width < 544 || gdi->height < 96 ||
        !read_marker(gdi, 32, &top) ||
        !read_marker(gdi, (unsigned int)gdi->height - 48, &bottom))
    {
        status = "invalid";
        ++bench->invalid_frames;
    }
    else if (top != bottom)
    {
        status = "torn";
        ++bench->torn_frames;
    }
    else
    {
        marker = (int64_t)(top & UINT64_C(0xffffff));
        if (marker == 0)
        {
            status = "setup";
        }
        else if ((uint32_t)marker > bench->last_marker)
        {
            status = "unique";
            if (bench->unique_frames == 0)
            {
                bench->first_unique_ns = timestamp;
            }
            ++bench->unique_frames;
            bench->last_unique_ns = timestamp;
            bench->last_marker = (uint32_t)marker;
        }
        else if ((uint32_t)marker == bench->last_marker)
        {
            status = "duplicate";
            ++bench->duplicate_frames;
        }
        else
        {
            status = "regression";
            ++bench->regression_frames;
        }
    }
    if (fprintf(bench->frames, "%" PRIu64 ",%" PRIu32 ",%" PRId64 ",%s,%" PRIu32
                ",%" PRIu64 "\n", timestamp, frame->frameId, marker, status,
                bench->frame_codec, bench->frame_bytes) < 0)
    {
        bench->io_error = 1;
    }
    bench->frame_bytes = 0;
    bench->frame_codec = UINT32_MAX;
    write_ready(bench, timestamp);
    return rc;
}

static UINT
bench_surface(RdpgfxClientContext *gfx, const RDPGFX_SURFACE_COMMAND *command)
{
    rdpGdi *gdi = (rdpGdi *)gfx->custom;
    BenchContext *bench = (BenchContext *)gdi->context;
    ++bench->surface_commands;
    bench->encoded_surface_bytes += command->length;
    bench->frame_bytes += command->length;
    bench->frame_codec = command->codecId;
    if (command->codecId < 16)
    {
        ++bench->codec_commands[command->codecId];
    }
    return bench->decode_surface(gfx, command);
}

static void
channel_connected(void *opaque, const ChannelConnectedEventArgs *event)
{
    BenchContext *bench = (BenchContext *)opaque;
    freerdp_client_OnChannelConnectedEventHandler(&bench->common, event);
    if (strcmp(event->name, RDPGFX_DVC_CHANNEL_NAME) == 0)
    {
        RdpgfxClientContext *gfx = (RdpgfxClientContext *)event->pInterface;
        bench->decode_end_frame = gfx->EndFrame;
        bench->decode_surface = gfx->SurfaceCommand;
        gfx->EndFrame = bench_end_frame;
        gfx->SurfaceCommand = bench_surface;
    }
}

static void
channel_disconnected(void *opaque, const ChannelDisconnectedEventArgs *event)
{
    BenchContext *bench = (BenchContext *)opaque;
    freerdp_client_OnChannelDisconnectedEventHandler(&bench->common, event);
}

static BOOL
begin_paint(rdpContext *context)
{
    context->gdi->primary->hdc->hwnd->invalid->null = TRUE;
    return TRUE;
}

static BOOL
end_paint(rdpContext *context)
{
    (void)context;
    return TRUE;
}

static BOOL
desktop_resize(rdpContext *context)
{
    return gdi_resize(context->gdi,
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
}

static BOOL
pre_connect(freerdp *instance)
{
    rdpContext *context = instance->context;
    return PubSub_SubscribeChannelConnected(context->pubSub, channel_connected) >= 0 &&
           PubSub_SubscribeChannelDisconnected(context->pubSub, channel_disconnected) >= 0;
}

static BOOL
post_connect(freerdp *instance)
{
    if (!gdi_init(instance, PIXEL_FORMAT_XRGB32))
    {
        return FALSE;
    }
    instance->context->update->BeginPaint = begin_paint;
    instance->context->update->EndPaint = end_paint;
    instance->context->update->DesktopResize = desktop_resize;
    return TRUE;
}

static void
post_disconnect(freerdp *instance)
{
    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, channel_connected);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, channel_disconnected);
    gdi_free(instance);
}

static BOOL
client_new(freerdp *instance, rdpContext *context)
{
    (void)context;
    instance->PreConnect = pre_connect;
    instance->PostConnect = post_connect;
    instance->PostDisconnect = post_disconnect;
    return TRUE;
}

static void
usage(const char *program)
{
    fprintf(stderr, "Usage: %s --output PREFIX --ready-file PATH [options]\n"
            "  --host 127.0.0.1 --port 3397 --codec rfx|avc420\n"
            "  --width 1920 --height 1080 --duration 60 --username benchmark\n"
            "  --password-env RDP_BENCH_PASSWORD (default password: benchmark)\n"
            "  --selftest (marker decoder only, no connection)\n"
            "Loopback TLS only. Output: PREFIX.frames.csv, PREFIX.summary.json.\n",
            program);
}

static unsigned int
positive_number(const char *value, unsigned int maximum)
{
    char *end;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno || value[0] == '\0' || *end || parsed == 0 || parsed > maximum)
    {
        fprintf(stderr, "Invalid positive integer: %s\n", value);
        exit(2);
    }
    return (unsigned int)parsed;
}

static int
marker_selftest(void)
{
    rdpGdi gdi = {0};
    uint64_t decoded;
    uint64_t packet = UINT64_C(0x123456);
    unsigned int bit;
    unsigned int row;
    gdi.width = 640;
    gdi.height = 128;
    gdi.stride = 640 * 4;
    gdi.primary_buffer = calloc(gdi.height, gdi.stride);
    if (gdi.primary_buffer == NULL)
    {
        return 1;
    }
    packet |= (uint64_t)crc40(packet) << 40 | UINT64_C(0xb67a) << 48;
    for (bit = 0; bit < 64; ++bit)
    {
        for (row = 0; row < 2; ++row)
        {
            uint32_t color = (((packet >> bit) & 1) ^ row) ? 0xf0f0f0 : 0x101010;
            FreeRDPWriteColor(gdi.primary_buffer + (size_t)(36 + 8 * row) * gdi.stride +
                             (36 + 8 * bit) * 4, PIXEL_FORMAT_XRGB32, color);
        }
    }
    if (!read_marker(&gdi, 32, &decoded) || decoded != packet)
    {
        free(gdi.primary_buffer);
        return 1;
    }
    /* Complement damage must not produce a frame ID. */
    FreeRDPWriteColor(gdi.primary_buffer + (size_t)36 * gdi.stride + 36 * 4,
                     PIXEL_FORMAT_XRGB32, 0xf0f0f0);
    if (read_marker(&gdi, 32, &decoded))
    {
        free(gdi.primary_buffer);
        return 1;
    }
    free(gdi.primary_buffer);
    puts("marker decoder selftest passed");
    return 0;
}

int
main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    const char *codec = "rfx";
    const char *username = "benchmark";
    const char *password_env = "RDP_BENCH_PASSWORD";
    const char *output = NULL;
    const char *ready_path = NULL;
    const char *password;
    unsigned int port = 3397;
    unsigned int width = 1920;
    unsigned int height = 1080;
    unsigned int duration = 60;
    char frames_path[4096];
    char summary_path[4096];
    RDP_CLIENT_ENTRY_POINTS entry = {0};
    rdpContext *context;
    BenchContext *bench;
    rdpSettings *settings;
    FILE *summary;
    uint64_t started;
    uint64_t finished;
    uint32_t error;
    int result = 0;
    int connected = 0;
    int i;
    for (i = 1; i < argc; ++i)
    {
        const char *option = argv[i];
        const char *value;
        if (strcmp(option, "--selftest") == 0)
        {
            return marker_selftest();
        }
        if (strcmp(option, "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (++i == argc)
        {
            usage(argv[0]);
            return 2;
        }
        value = argv[i];
        if (strcmp(option, "--host") == 0) host = value;
        else if (strcmp(option, "--port") == 0) port = positive_number(value, 65535);
        else if (strcmp(option, "--codec") == 0) codec = value;
        else if (strcmp(option, "--width") == 0) width = positive_number(value, 16384);
        else if (strcmp(option, "--height") == 0) height = positive_number(value, 16384);
        else if (strcmp(option, "--duration") == 0) duration = positive_number(value, 3600);
        else if (strcmp(option, "--username") == 0) username = value;
        else if (strcmp(option, "--password-env") == 0) password_env = value;
        else if (strcmp(option, "--output") == 0) output = value;
        else if (strcmp(option, "--ready-file") == 0) ready_path = value;
        else
        {
            fprintf(stderr, "Unknown option: %s\n", option);
            return 2;
        }
    }
    if (!output || !ready_path ||
        (strcmp(host, "127.0.0.1") != 0 && strcmp(host, "::1") != 0) ||
        (strcmp(codec, "rfx") != 0 && strcmp(codec, "avc420") != 0) ||
        width < 544 || height < 96 ||
        snprintf(frames_path, sizeof(frames_path), "%s.frames.csv", output) >= (int)sizeof(frames_path) ||
        snprintf(summary_path, sizeof(summary_path), "%s.summary.json", output) >= (int)sizeof(summary_path))
    {
        usage(argv[0]);
        return 2;
    }
    password = getenv(password_env);
    if (!password) password = "benchmark";
    entry.Size = sizeof(entry);
    entry.Version = RDP_CLIENT_INTERFACE_VERSION;
    entry.ContextSize = sizeof(BenchContext);
    entry.ClientNew = client_new;
    context = freerdp_client_context_new(&entry);
    if (!context) return 1;
    bench = (BenchContext *)context;
    bench->codec = codec;
    bench->ready_path = ready_path;
    bench->frame_codec = UINT32_MAX;
    bench->frames = fopen(frames_path, "w");
    if (!bench->frames)
    {
        perror(frames_path);
        freerdp_client_context_free(context);
        return 1;
    }
    setvbuf(bench->frames, NULL, _IOFBF, 65536);
    fprintf(bench->frames, "decoded_ns,rdp_frame_id,marker_frame_id,status,codec_id,encoded_surface_bytes\n");
    settings = context->settings;
#define SET_BOOL(key, value) do { if (!freerdp_settings_set_bool(settings, FreeRDP_ ## key, (value))) goto settings_error; } while (0)
#define SET_UINT(key, value) do { if (!freerdp_settings_set_uint32(settings, FreeRDP_ ## key, (value))) goto settings_error; } while (0)
#define SET_STR(key, value) do { if (!freerdp_settings_set_string(settings, FreeRDP_ ## key, (value))) goto settings_error; } while (0)
    SET_STR(ServerHostname, host);
    SET_UINT(ServerPort, port);
    SET_STR(Username, username);
    SET_STR(Password, password);
    SET_UINT(DesktopWidth, width);
    SET_UINT(DesktopHeight, height);
    SET_UINT(ColorDepth, 32);
    SET_BOOL(SoftwareGdi, TRUE);
    SET_BOOL(DeactivateClientDecoding, FALSE);
    SET_BOOL(TlsSecurity, TRUE);
    SET_BOOL(NlaSecurity, FALSE);
    SET_BOOL(RdpSecurity, FALSE);
    SET_BOOL(IgnoreCertificate, TRUE);
    SET_BOOL(AutoLogonEnabled, TRUE);
    SET_BOOL(SupportGraphicsPipeline, TRUE);
    SET_BOOL(SupportDynamicChannels, TRUE);
    SET_BOOL(GfxH264, strcmp(codec, "avc420") == 0);
    SET_BOOL(GfxAVC444, FALSE);
    SET_BOOL(GfxAVC444v2, FALSE);
    SET_BOOL(RemoteFxCodec, strcmp(codec, "rfx") == 0);
    SET_BOOL(GfxProgressive, FALSE);
    SET_BOOL(GfxProgressiveV2, FALSE);
    SET_BOOL(GfxSuspendFrameAck, FALSE);
    SET_BOOL(NetworkAutoDetect, FALSE);
    SET_BOOL(AudioPlayback, FALSE);
    SET_BOOL(RedirectClipboard, FALSE);
    SET_BOOL(SupportDisplayControl, FALSE);
    SET_BOOL(AsyncUpdate, FALSE);
    SET_BOOL(AsyncChannels, FALSE);
    SET_UINT(TcpConnectTimeout, 10000);
    SET_UINT(ConnectionType, CONNECTION_TYPE_LAN);
#undef SET_BOOL
#undef SET_UINT
#undef SET_STR
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    started = now_ns();
    connected = freerdp_connect(context->instance);
    if (!connected)
    {
        result = 1;
    }
    while (connected && !stopped && !bench->io_error &&
           now_ns() - started < (uint64_t)duration * UINT64_C(1000000000) &&
           !freerdp_shall_disconnect_context(context))
    {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        DWORD count = freerdp_get_event_handles(context, handles, MAXIMUM_WAIT_OBJECTS);
        DWORD wait_status;
        if (!count)
        {
            result = 1;
            break;
        }
        wait_status = WaitForMultipleObjects(count, handles, FALSE, 100);
        if (wait_status == WAIT_FAILED || !freerdp_check_event_handles(context))
        {
            result = 1;
            break;
        }
    }
    error = freerdp_get_last_error(context);
    freerdp_disconnect(context->instance);
    finished = now_ns();
    if (fclose(bench->frames) != 0) bench->io_error = 1;
    if (bench->io_error || !bench->ready) result = 1;
    summary = fopen(summary_path, "w");
    if (!summary)
    {
        perror(summary_path);
        result = 1;
    }
    else
    {
        fprintf(summary, "{\n  \"freerdp_version\": \"%s\",\n"
                "  \"requested_codec\": \"%s\",\n"
                "  \"endpoint\": \"decoded software GDI framebuffer\",\n"
                "  \"clock\": \"CLOCK_MONOTONIC\",\n"
                "  \"started_ns\": %" PRIu64 ",\n  \"finished_ns\": %" PRIu64 ",\n"
                "  \"connected\": %s,\n  \"decoder_ready\": %s,\n"
                "  \"freerdp_error\": %" PRIu32 ",\n  \"exit_status\": %d,\n"
                "  \"gfx_end_frames\": %" PRIu64 ",\n  \"unique_marker_frames\": %" PRIu64 ",\n"
                "  \"duplicate_marker_frames\": %" PRIu64 ",\n  \"invalid_marker_frames\": %" PRIu64 ",\n"
                "  \"torn_marker_frames\": %" PRIu64 ",\n  \"regression_marker_frames\": %" PRIu64 ",\n"
                "  \"first_unique_ns\": %" PRIu64 ",\n  \"last_unique_ns\": %" PRIu64 ",\n"
                "  \"surface_commands\": %" PRIu64 ",\n  \"encoded_surface_bytes\": %" PRIu64 ",\n"
                "  \"codec_command_counts\": {",
                freerdp_get_version_string(), codec, started, finished,
                connected ? "true" : "false", bench->ready ? "true" : "false", error, result,
                bench->end_frames, bench->unique_frames, bench->duplicate_frames,
                bench->invalid_frames, bench->torn_frames, bench->regression_frames,
                bench->first_unique_ns, bench->last_unique_ns, bench->surface_commands,
                bench->encoded_surface_bytes);
        for (i = 0; i < 16; ++i)
        {
            fprintf(summary, "%s\"%d\":%" PRIu64, i ? "," : "", i, bench->codec_commands[i]);
        }
        fputs("}\n}\n", summary);
        if (fclose(summary) != 0) result = 1;
    }
    freerdp_client_context_free(context);
    return result;

settings_error:
    fprintf(stderr, "FreeRDP setting could not be configured\n");
    fclose(bench->frames);
    freerdp_client_context_free(context);
    return 1;
}

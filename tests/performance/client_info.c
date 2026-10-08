/* Emit the native xup_client_info used by the live Xorg performance probe. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xup_client_info.h"

static int
positive_int(const char *value, int limit)
{
    char *end;
    long result;

    errno = 0;
    result = strtol(value, &end, 10);
    if (errno != 0 || *value == '\0' || *end != '\0' ||
            result < 1 || result > limit)
    {
        fprintf(stderr, "Invalid integer: %s\n", value);
        exit(1);
    }
    return (int) result;
}

int
main(int argc, char **argv)
{
    struct xup_client_info info = {0};
    int interval;

    if (argc < 3 || argc > 4)
    {
        fprintf(stderr, "Usage: %s WIDTH HEIGHT [FRAME_INTERVAL_MS]\n", argv[0]);
        return 1;
    }
    info.size = sizeof(info);
    info.version = XUP_CLIENT_INFO_CURRENT_VERSION;
    info.bpp = 32;
    info.display_sizes.session_width = positive_int(argv[1], 16384);
    info.display_sizes.session_height = positive_int(argv[2], 16384);
    info.capture_code = CC_SIMPLE;
    info.capture_format = XRDP_a8r8g8b8;
    strcpy(info.model, "pc105");
    strcpy(info.layout, "us");
    strcpy(info.xkb_rules, "evdev");
    interval = argc == 4 ? positive_int(argv[3], 1000) : 40;
    info.normal_frame_interval = interval;
    info.rfx_frame_interval = interval;
    info.h264_frame_interval = interval;
    if (fwrite(&info, sizeof(info), 1, stdout) != 1 || fflush(stdout) != 0)
    {
        perror("Writing client_info");
        return 1;
    }
    return 0;
}

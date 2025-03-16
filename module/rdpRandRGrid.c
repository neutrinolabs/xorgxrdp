/*
Copyright 2024 Jay Sorg

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

NVidia Grid RandR

*/

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include "rdp.h"
#include "rdpMisc.h"

#define NV_GRID_MAX_OUTPUTS 4
#define NV_GRID_MAX_CMD 256
#define NV_GRID_OP_MAX 16
#define NV_GRID_END_CMD_MAX 2048
#define NV_GRID_CMD_FILE ".xrdp_grid_xrandr.sh"

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

struct monitor_t
{
    int flags;
    int pad0;
    int x;
    int y;
    int w;
    int h;
};

struct monitors_t
{
    int monitor_count;
    int pad0;
    struct monitor_t monitors[NV_GRID_MAX_OUTPUTS];
    char newmodes[NV_GRID_MAX_OUTPUTS][NV_GRID_MAX_CMD];
    char addmodes[NV_GRID_MAX_OUTPUTS][NV_GRID_MAX_CMD];
    char setmodes[NV_GRID_MAX_OUTPUTS][NV_GRID_MAX_CMD];
};

struct cmd_file_t
{
    char filename[NV_GRID_MAX_CMD];
    char cmd[NV_GRID_END_CMD_MAX];
};

/******************************************************************************/
static int
rdpRandRGridUpdateCmds(struct monitors_t *monitors)
{
    int index;
    int x;
    int y;
    int w;
    int h;
    int clk;
    char output_name[NV_GRID_OP_MAX];

    for (index = 0; index < NV_GRID_MAX_OUTPUTS; index++)
    {
        snprintf(output_name, sizeof(output_name), "DVI-D-%d", index);
        if (index < monitors->monitor_count)
        {
            x = monitors->monitors[index].x;
            y = monitors->monitors[index].y;
            w = monitors->monitors[index].w;
            h = monitors->monitors[index].h;
            clk = w * h * 60 / 1000000;
            snprintf(monitors->newmodes[index],
                     sizeof(monitors->newmodes[index]),
                     "xrandr --newmode %dx%d_%s "
                     "%d %d %d %d %d %d %d %d %d\n",
                     w, h, output_name, clk,
                     w, w, w, w, h, h, h, h);
            snprintf(monitors->addmodes[index],
                     sizeof(monitors->addmodes[index]),
                     "xrandr --addmode %s %dx%d_%s\n",
                     output_name, w, h, output_name);
            snprintf(monitors->setmodes[index],
                     sizeof(monitors->setmodes[index]),
                     "--output %s --mode %dx%d_%s --pos %dx%d",
                     output_name, w, h, output_name, x, y);
        }
        else
        {
            snprintf(monitors->setmodes[index], 256, "--output %s --off",
                     output_name);
        }
    }
    return 0;
}

/******************************************************************************/
static int
rdpRandRGridWriteString(int fd, const char *str)
{
    int to_write;
    int written;

    to_write = strlen(str);
    written = write(fd, str, to_write);
    if (written != to_write)
    {
        LLOGLN(0, ("rdpRandRGridWriteString: write failed fd %d written %d "
               "to_write %d", fd, written, to_write));
        return 1;
    }
    return 0;
}

/******************************************************************************/
static int
rdpRandRGridUpdateRunCmds(struct monitors_t *monitors)
{
    int index;
    struct cmd_file_t *cmd_file;
    char *env;
    int fd;
    int system_rv;

    cmd_file = g_new0(struct cmd_file_t, 1);
    if (cmd_file == NULL)
    {
        LLOGLN(0, ("rdpRandRGridUpdateRunCmds: alloc failed"));
        return 1;
    }
    env = getenv("HOME");
    if (env == NULL)
    {
        LLOGLN(0, ("rdpRandRGridUpdateRunCmds: getenv HOME failed"));
        free(cmd_file);
        return 1;
    }
    snprintf(cmd_file->filename, sizeof(cmd_file->filename),
             "%s/%s", env, NV_GRID_CMD_FILE);
    fd = open(cmd_file->filename, O_WRONLY | O_CREAT | O_TRUNC,
              S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        LLOGLN(0, ("rdpRandRGridUpdateRunCmds: open %s failed",
               cmd_file->filename));
        free(cmd_file);
        return 1;
    }
    rdpRandRGridWriteString(fd, "#!/bin/sh\n");
    rdpRandRGridWriteString(fd, "sleep 2\n");
    for (index = 0; index < NV_GRID_MAX_OUTPUTS; index++)
    {
        rdpRandRGridWriteString(fd, monitors->newmodes[index]);
    }
    for (index = 0; index < NV_GRID_MAX_OUTPUTS; index++)
    {
        rdpRandRGridWriteString(fd, monitors->addmodes[index]);
    }
    strcpy(cmd_file->cmd, "xrandr");
    for (index = 0; index < NV_GRID_MAX_OUTPUTS; index++)
    {
        strcat(cmd_file->cmd, " ");
        strcat(cmd_file->cmd, monitors->setmodes[index]);
    }
    strcat(cmd_file->cmd, "\n");
    rdpRandRGridWriteString(fd, cmd_file->cmd);
    close(fd);
    snprintf(cmd_file->cmd, sizeof(cmd_file->cmd),
             "sh %s&", cmd_file->filename);
    LLOGLN(0, ("rdpRandRGridUpdateRunCmds: running command %s",
           cmd_file->cmd));
    system_rv = system(cmd_file->cmd);
    if (system_rv != 0)
    {
        LLOGLN(0, ("rdpRandRGridUpdateRunCmds: command %s returned %d",
               cmd_file->cmd, system_rv));
        free(cmd_file);
        return 1;
    }
    free(cmd_file);
    return 0;
}

/******************************************************************************/
int
rdpRandRGridSetRdpOutputs(rdpPtr dev)
{
    int index;
    int x;
    int y;
    int w;
    int h;
    int rv;
    struct monitors_t *monitors;

    monitors = g_new0(struct monitors_t, 1);
    if (monitors == NULL)
    {
        return 1;
    }
    if (dev->monitorCount > 0)
    {
        monitors->monitor_count = min(dev->monitorCount, NV_GRID_MAX_OUTPUTS);
        for (index = 0; index < monitors->monitor_count; index++)
        {
            x = dev->minfo[index].left;
            y = dev->minfo[index].top;
            w = dev->minfo[index].right - dev->minfo[index].left + 1;
            h = dev->minfo[index].bottom - dev->minfo[index].top + 1;
            monitors->monitors[index].x = x;
            monitors->monitors[index].y = y;
            monitors->monitors[index].w = w;
            monitors->monitors[index].h = h;
        }
    }
    else
    {
        monitors->monitor_count = 1;
        monitors->monitors[0].w = dev->width;
        monitors->monitors[0].h = dev->height;
    }
    rv = rdpRandRGridUpdateCmds(monitors);
    if (rv == 0)
    {
        rv = rdpRandRGridUpdateRunCmds(monitors);
    }
    free(monitors);
    return rv;
}

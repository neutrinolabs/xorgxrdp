/*
Copyright 2018 Jay Sorg

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

dri3

*/

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>

#include <mipointer.h>
#include <fb.h>
#include <micmap.h>
#include <mi.h>
#include <randrstr.h>

#include <xf86Modes.h>

#include "rdp.h"
#include "rdpPri.h"

#include <glamor.h>
#include <dri3.h>

extern char g_drm_device[]; /* in xrdpdev.c */

#define LLOG_LEVEL 1
#define LLOGLN(_level, _args) \
  do \
  { \
    if (_level < LLOG_LEVEL) \
    { \
      ErrorF _args ; \
      ErrorF("\n"); \
    } \
  } \
  while (0)

/*****************************************************************************/
static PixmapPtr
rdpDri3PixmapFromFd(ScreenPtr screen, int fd,
                    CARD16 width, CARD16 height, CARD16 stride,
                    CARD8 depth, CARD8 bpp)
{
    PixmapPtr rv;

    LLOGLN(10, ("rdpDri3PixmapFromFd:"));
    rv = glamor_pixmap_from_fd(screen, fd, width, height, stride, depth, bpp);
    LLOGLN(10, ("rdpDri3PixmapFromFd: fd %d pixmap %p", fd, rv));
    return rv;
}

/*****************************************************************************/
static int
rdpDri3FdFromPixmap(ScreenPtr screen, PixmapPtr pixmap,
                    CARD16 *stride, CARD32 *size)
{
    int rv;

    LLOGLN(10, ("rdpDri3FdFromPixmap:"));
    rv = glamor_fd_from_pixmap(screen, pixmap, stride, size);
    LLOGLN(10, ("rdpDri3FdFromPixmap: fd %d pixmap %p", rv, pixmap));
    return rv;
}

/*****************************************************************************/
static int
rdpDri3OpenClient(ClientPtr client, ScreenPtr screen,
                  RRProviderPtr provider, int *pfd)
{
    int fd;

    LLOGLN(10, ("rdpDri3OpenClient:"));
    fd = open(g_drm_device, O_RDWR | O_CLOEXEC);
    LLOGLN(10, ("rdpDri3OpenClient: fd %d", fd));
    if (fd < 0)
    {
        return BadAlloc;
    }
    *pfd = fd;
    return Success;
}

/*****************************************************************************/
int
rdpDri3Init(ScreenPtr pScreen)
{
    static dri3_screen_info_rec rdp_dri3_info;

    memset(&rdp_dri3_info, 0, sizeof(rdp_dri3_info));
    rdp_dri3_info.version = 1;
    rdp_dri3_info.pixmap_from_fd = rdpDri3PixmapFromFd;
    rdp_dri3_info.fd_from_pixmap = rdpDri3FdFromPixmap;
    rdp_dri3_info.open_client = rdpDri3OpenClient;
    if (!dri3_screen_init(pScreen, &rdp_dri3_info))
    {
        LLOGLN(0, ("rdpScreenInit: dri3_screen_init failed"));
        return 1;
    }
    return 0;
}

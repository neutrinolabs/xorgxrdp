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
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* this should be before all X11 .h files */
#include <xorg-server.h>
#include <xorgVersion.h>

/* all driver need this */
#include <xf86.h>
#include <xf86_OSproc.h>
#include <xf86drm.h>

#include <mipointer.h>
#include <fb.h>
#include <micmap.h>
#include <mi.h>
#include <randrstr.h>

#include <xf86Modes.h>

#include "rdp.h"
#include "rdpMisc.h"
#include "rdpPri.h"

#include <glamor.h>
#include <dri3.h>

extern char g_drm_device[]; /* in xrdpdev.c */

/*****************************************************************************/
static PixmapPtr
rdpDri3PixmapFromFd(ScreenPtr screen, int fd,
                    CARD16 width, CARD16 height, CARD16 stride,
                    CARD8 depth, CARD8 bpp)
{
    PixmapPtr rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3PixmapFromFd:");
    rv = glamor_pixmap_from_fd(screen, fd, width, height, stride, depth, bpp);
    LOG(LOG_LEVEL_TRACE, "rdpDri3PixmapFromFd: fd %d pixmap %p", fd, rv);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3FdFromPixmap(ScreenPtr screen, PixmapPtr pixmap,
                    CARD16 *stride, CARD32 *size)
{
    int rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3FdFromPixmap:");
    rv = glamor_fd_from_pixmap(screen, pixmap, stride, size);
    LOG(LOG_LEVEL_TRACE, "rdpDri3FdFromPixmap: fd %d pixmap %p", rv, pixmap);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3OpenClient(ClientPtr client, ScreenPtr screen,
                  RRProviderPtr provider, int *pfd)
{
    ScrnInfoPtr pScrn;
    rdpPtr dev;
    int fd;
    drm_magic_t magic;

    LOG(LOG_LEVEL_TRACE, "rdpDri3OpenClient:");
    pScrn = xf86ScreenToScrn(screen);
    dev = XRDPPTR(pScrn);
    fd = open(g_drm_device, O_RDWR | O_CLOEXEC);
    LOG(LOG_LEVEL_TRACE, "rdpDri3OpenClient: fd %d", fd);
    if (fd < 0)
    {
        return BadAlloc;
    }

    if (drmGetMagic(fd, &magic) < 0)
    {
        if (errno == EACCES)
        {
            /* Render nodes are already as authenticated as they should be. */
            *pfd = fd;
            return Success;
        }
        close(fd);
        return BadMatch;
    }

    if (drmAuthMagic(dev->fd, magic) < 0)
    {
        close(fd);
        return BadMatch;
    }

    *pfd = fd;
    return Success;
}

/*****************************************************************************/
#if DRI3_SCREEN_INFO_VERSION >= 2

static PixmapPtr
rdpDri3PixmapFromFds(ScreenPtr screen, CARD8 num_fds, const int *fds,
                     CARD16 width, CARD16 height, const CARD32 *strides,
                     const CARD32 *offsets, CARD8 depth, CARD8 bpp,
                     CARD64 modifier)
{
    PixmapPtr rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3PixmapFromFds:");
    rv = glamor_pixmap_from_fds(screen, num_fds, fds, width, height, strides,
                                  offsets, depth, bpp, modifier);
    LOG(LOG_LEVEL_TRACE, "rdpDri3PixmapFromFds: pixmap %p", rv);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3FdsFromPixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                     uint32_t *strides, uint32_t *offsets,
                     uint64_t *modifier)
{
    int rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3FdsFromPixmap:");
    rv = glamor_fds_from_pixmap(screen, pixmap, fds, strides, offsets, modifier);
    LOG(LOG_LEVEL_TRACE, "rdpDri3FdsFromPixmap: pixmap %p", pixmap);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3GetFormats(ScreenPtr screen, CARD32 *num_formats, CARD32 **formats)
{
    int rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3GetFormats:");
    rv = glamor_get_formats(screen, num_formats, formats);
    LOG(LOG_LEVEL_TRACE, "rdpDri3GetFormats: rv %d", rv);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3GetModifiers(ScreenPtr screen, uint32_t format,
                    uint32_t *num_modifiers, uint64_t **modifiers)
{
    int rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3GetModifiers:");
    rv = glamor_get_modifiers(screen, format, num_modifiers, modifiers);
    LOG(LOG_LEVEL_TRACE, "rdpDri3GetModifiers: rv %d", rv);
    return rv;
}

/*****************************************************************************/
static int
rdpDri3GetDrawableModifiers(DrawablePtr draw, uint32_t format,
                            uint32_t *num_modifiers, uint64_t **modifiers)
{
    int rv;

    LOG(LOG_LEVEL_TRACE, "rdpDri3GetDrawableModifiers:");
    rv = glamor_get_drawable_modifiers(draw, format, num_modifiers, modifiers);
    LOG(LOG_LEVEL_TRACE, "rdpDri3GetDrawableModifiers: draw %p rv %d", draw, rv);
    return rv;
}

#endif

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
#if DRI3_SCREEN_INFO_VERSION >= 2
    rdp_dri3_info.version = 2;
    rdp_dri3_info.pixmap_from_fds = rdpDri3PixmapFromFds;
    rdp_dri3_info.fds_from_pixmap = rdpDri3FdsFromPixmap;
    rdp_dri3_info.get_formats = rdpDri3GetFormats;
    rdp_dri3_info.get_modifiers = rdpDri3GetModifiers;
    rdp_dri3_info.get_drawable_modifiers = rdpDri3GetDrawableModifiers;
#endif
    LOG(LOG_LEVEL_INFO, "rdpDri3Init: rdp_dri3_info.version = %lu", (unsigned long)rdp_dri3_info.version);
    if (!dri3_screen_init(pScreen, &rdp_dri3_info))
    {
        LOG(LOG_LEVEL_INFO, "rdpDri3Init: dri3_screen_init failed");
        return 1;
    }
    return 0;
}

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

dri2

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
#include <xf86drm.h>

#include <mipointer.h>
#include <fb.h>
#include <micmap.h>
#include <mi.h>
#include <randrstr.h>
#include <dri2.h>

#include <xf86Modes.h>

#include "rdp.h"
#include "rdpPri.h"
#include "rdpDraw.h"

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

static DevPrivateKeyRec g_rdpDri2ClientKey;

/*****************************************************************************/
static DRI2Buffer2Ptr
rdpDri2CreateBuffer(DrawablePtr drawable, unsigned int attachment,
                    unsigned int format)
{
    LLOGLN(0, ("rdpDri2CreateBuffer:"));
    return 0;
}

/*****************************************************************************/
static void rdpDri2DestroyBuffer(DrawablePtr drawable, DRI2Buffer2Ptr buffer)
{
    LLOGLN(0, ("rdpDri2DestroyBuffer:"));
}

/*****************************************************************************/
static void
rdpDri2CopyRegion(DrawablePtr drawable, RegionPtr pRegion,
                  DRI2BufferPtr destBuffer, DRI2BufferPtr sourceBuffer)
{
    LLOGLN(0, ("rdpDri2CopyRegion:"));
}

/*****************************************************************************/
static int
rdpDri2ScheduleSwap(ClientPtr client, DrawablePtr draw,
                    DRI2BufferPtr front, DRI2BufferPtr back,
                    CARD64 *target_msc, CARD64 divisor,
                    CARD64 remainder, DRI2SwapEventPtr func, void *data)
{
    LLOGLN(0, ("rdpDri2ScheduleSwap:"));
    return 0;
}

/*****************************************************************************/
static int
rdpDri2GetMSC(DrawablePtr draw, CARD64 *ust, CARD64 *msc)
{
    LLOGLN(0, ("rdpDri2GetMSC:"));
    return 0;
}

/*****************************************************************************/
static int
rdpDri2ScheduleWaitMSC(ClientPtr client, DrawablePtr draw, CARD64 target_msc,
                       CARD64 divisor, CARD64 remainder)
{
    LLOGLN(0, ("rdpDri2ScheduleWaitMSC:"));
    return 0;
}

/*****************************************************************************/
static DRI2Buffer2Ptr
rdpDri2CreateBuffer2(ScreenPtr screen, DrawablePtr drawable,
                     unsigned int attachment, unsigned int format)
{
    LLOGLN(0, ("rdpDri2CreateBuffer2:"));
    return 0;
}

/*****************************************************************************/
static void
rdpDri2DestroyBuffer2(ScreenPtr unused, DrawablePtr unused2,
                      DRI2Buffer2Ptr buffer)
{
    LLOGLN(0, ("rdpDri2DestroyBuffer2:"));
}

/*****************************************************************************/
static void
rdpDri2CopyRegion2(ScreenPtr screen, DrawablePtr drawable, RegionPtr pRegion,
                   DRI2BufferPtr destBuffer, DRI2BufferPtr sourceBuffer)
{
    LLOGLN(0, ("rdpDri2CopyRegion2:"));
}

/*****************************************************************************/
int
rdpDri2Init(ScreenPtr pScreen)
{
    rdpPtr dev;
    DRI2InfoRec info;

    LLOGLN(0, ("rdpDri2Init:"));
    dev = rdpGetDevFromScreen(pScreen);
    if (!dixRegisterPrivateKey(&g_rdpDri2ClientKey,
                               PRIVATE_CLIENT, sizeof(XID)))
    {
        return FALSE;
    }
    memset(&info, 0, sizeof(info));
    info.fd = dev->fd;
    info.deviceName = drmGetDeviceNameFromFd2(dev->fd);
    info.version = 9;
    info.CreateBuffer = rdpDri2CreateBuffer;
    info.DestroyBuffer = rdpDri2DestroyBuffer;
    info.CopyRegion = rdpDri2CopyRegion;
    info.ScheduleSwap = rdpDri2ScheduleSwap;
    info.GetMSC = rdpDri2GetMSC;
    info.ScheduleWaitMSC = rdpDri2ScheduleWaitMSC;
    info.CreateBuffer2 = rdpDri2CreateBuffer2;
    info.DestroyBuffer2 = rdpDri2DestroyBuffer2;
    info.CopyRegion2 = rdpDri2CopyRegion2;
    if (!DRI2ScreenInit(pScreen, &info))
    {
        return 1;
    }
    return 0;
}

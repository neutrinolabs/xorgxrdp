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

#if defined(XORGXRDP_GLAMOR)
#include <glamor.h>
#endif

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
#if defined(XORGXRDP_GLAMOR)
    const char *driver_names[2] = { NULL, NULL };
#endif

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
#if defined(XORGXRDP_GLAMOR)
    /* This is from xorg's hw/xfree86/drivers/modesetting/dri2.c. */
    /* This ensures that dri/va (=driver[0]) and vdpau (=driver[1])   */
    /* get the correct values. Currently only needed for intel drivers.    */
    /* Ask Glamor to obtain the DRI driver name via EGL_MESA_query_driver. */
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1, 20, 7, 0, 0)
    driver_names[0] = glamor_egl_get_driver_name(pScreen);
#endif
    if (driver_names[0])
    {
        /* There is no VDPAU driver for Intel, fallback to the generic
         * OpenGL/VAAPI va_gl backend to emulate VDPAU.  Otherwise,
         * guess that the DRI and VDPAU drivers have the same name.
         */
        if (strcmp(driver_names[0], "i965") == 0 ||
            strcmp(driver_names[0], "iris") == 0 ||
            strcmp(driver_names[0], "crocus") == 0)
        {
            driver_names[1] = "va_gl";
        }
        else
        {
            driver_names[1] = driver_names[0];
        }

        info.numDrivers = 2;
        info.driverNames = driver_names;
    }
    else
    {
        /* EGL_MESA_query_driver was unavailable; let dri2.c select the
         * driver and fill in these fields for us.
         */
        info.numDrivers = 0;
        info.driverNames = NULL;
    }
#endif
    if (!DRI2ScreenInit(pScreen, &info))
    {
        return 1;
    }
    return 0;
}

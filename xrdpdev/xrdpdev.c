/*
Copyright 2013-2017 Jay Sorg

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

This is the main driver file

*/

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "rdpDraw.h"
#include "rdpGC.h"
#include "rdpCursor.h"
#include "rdpRandR.h"
#include "rdpMisc.h"
#include "rdpComposite.h"
#include "rdpTrapezoids.h"
#include "rdpTriangles.h"
#include "rdpCompositeRects.h"
#include "rdpGlyphs.h"
#include "rdpPixmap.h"
#include "rdpClientCon.h"
#include "rdpXv.h"
#include "rdpSimd.h"

#if defined(XORGXRDP_GLAMOR)
#include "xrdpdri2.h"
#include "xrdpdri3.h"
#include "rdpEgl.h"
#include <xf86drm.h>
#include <glamor.h>
/* use environment variable XORGXRDP_DRM_DEVICE to override
 * also read from xorg.conf file */
char g_drm_device[128] = "/dev/dri/renderD128";
Bool g_use_dri2 = TRUE;
Bool g_use_dri3 = TRUE;
char g_drm_allow_list[128] = "";
#endif

static int g_setup_done = 0;
static OsTimerPtr g_randr_timer = 0;
static OsTimerPtr g_damage_timer = 0;

static char g_xrdp_driver_name[] = XRDP_DRIVER_NAME;

/* Supported "chipsets" */
static SymTabRec g_Chipsets[] =
{
    { 0, XRDP_DRIVER_NAME },
    { -1, 0 }
};

static XF86ModuleVersionInfo g_VersRec =
{
    XRDP_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR,
    PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    0,
    { 0, 0, 0, 0 }
};

/*****************************************************************************/
static Bool
rdpAllocRec(ScrnInfoPtr pScrn)
{
    LOG(LOG_LEVEL_TRACE, "rdpAllocRec:");
    if (pScrn->reservedPtr[0] != NULL)
    {
        return TRUE;
    }
    /* XNFcallocarray exits if alloc failed */
    pScrn->reservedPtr[0] = XNFcallocarray(sizeof(rdpRec), 1);
    return TRUE;
}

/*****************************************************************************/
static void
rdpFreeRec(ScrnInfoPtr pScrn)
{
    LOG(LOG_LEVEL_TRACE, "rdpFreeRec:");
    if (pScrn->reservedPtr[0] == NULL)
    {
        return;
    }
    free(pScrn->reservedPtr[0]);
    pScrn->reservedPtr[0] = NULL;
}

/*****************************************************************************/
static Bool
rdpPreInit(ScrnInfoPtr pScrn, int flags)
{
    rgb zeros1 = {0};
    Gamma zeros2 = {0};
    int got_res_match;
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 16, 0, 0, 0)
    char **modename;
#else
    const char **modename;
#endif
    DisplayModePtr mode;
    rdpPtr dev;

    LOG(LOG_LEVEL_TRACE, "rdpPreInit:");
    if (flags & PROBE_DETECT)
    {
        return FALSE;
    }
    if (pScrn->numEntities == 0)
    {
        return FALSE;
    }

    rdpAllocRec(pScrn);
    dev = XRDPPTR(pScrn);

    dev->glamor = FALSE;

#if defined(XORGXRDP_GLAMOR)
    if (getenv("XORGXRDP_DRM_DEVICE") != NULL)
    {
        strncpy(g_drm_device, getenv("XORGXRDP_DRM_DEVICE"), 127);
        g_drm_device[127] = 0;
    }
    dev->fd = open(g_drm_device, O_RDWR | O_CLOEXEC, 0);
    if (dev->fd == -1)
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: %s open failed", g_drm_device);
    }
    else
    {
        drmVersionPtr dver;
        char delim[] = " ";
        char *token;
        const char *drm_name;
        const char *drm_date;
        const char *drm_desc;
        LOG(LOG_LEVEL_INFO, "rdpPreInit: %s open ok, fd %d", g_drm_device, dev->fd);
        dver = drmGetVersion(dev->fd);
        if (dver != NULL)
        {
            drm_name = (dver->name != NULL) ? dver->name : "";
            drm_date = (dver->date != NULL) ? dver->date : "";
            drm_desc = (dver->desc != NULL) ? dver->desc : "";
            LOG(LOG_LEVEL_INFO, "rdpPreInit: name [%s]", drm_name);
            LOG(LOG_LEVEL_INFO, "rdpPreInit: date [%s]", drm_date);
            LOG(LOG_LEVEL_INFO, "rdpPreInit: desc [%s]", drm_desc);
            token = strtok(g_drm_allow_list, delim);
            while (token != NULL)
            {
                LOG(LOG_LEVEL_TRACE, "rdpPreInit: token [%s]", token);
                if (strstr(drm_name, token) != NULL)
                {
                    dev->glamor = TRUE;
                    LOG(LOG_LEVEL_INFO, "rdpPreInit: drm device looks ok, "
                        "use glamor set");
                    break;
                }
                token = strtok(NULL, delim);
            }
            if (dev->glamor == FALSE)
            {
                LOG(LOG_LEVEL_INFO, "rdpPreInit: unsupported render node");
            }
        }
        else
        {
            LOG(LOG_LEVEL_INFO, "rdpPreInit: drmGetVersion failed");
        }
        if (dver != NULL)
        {
            drmFreeVersion(dver);
        }
    }
#endif

    dev->width = 800;
    dev->height = 600;

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->bitsPerPixel = 32;
    pScrn->virtualX = dev->width;
    pScrn->displayWidth = dev->width;
    pScrn->virtualY = dev->height;
    pScrn->progClock = 1;
    pScrn->rgbBits = 8;
    pScrn->depth = 24;
    pScrn->chipset = g_xrdp_driver_name;
    pScrn->currentMode = pScrn->modes;

    pScrn->offset.blue = 0;
    pScrn->offset.green = 8;
    pScrn->offset.red = 16;
    pScrn->mask.blue = ((1 << 8) - 1) << pScrn->offset.blue;
    pScrn->mask.green = ((1 << 8) - 1) << pScrn->offset.green;
    pScrn->mask.red = ((1 << 8) - 1) << pScrn->offset.red;

    if (!xf86SetDepthBpp(pScrn, pScrn->depth, pScrn->bitsPerPixel,
                         pScrn->bitsPerPixel,
                         Support24bppFb | Support32bppFb |
                         SupportConvert32to24 | SupportConvert24to32))
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: xf86SetDepthBpp failed");
        rdpFreeRec(pScrn);
        return FALSE;
    }
    xf86PrintDepthBpp(pScrn);
    if (!xf86SetWeight(pScrn, zeros1, zeros1))
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: xf86SetWeight failed");
        rdpFreeRec(pScrn);
        return FALSE;
    }
    if (!xf86SetGamma(pScrn, zeros2))
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: xf86SetGamma failed");
        rdpFreeRec(pScrn);
        return FALSE;
    }
    if (!xf86SetDefaultVisual(pScrn, -1))
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: xf86SetDefaultVisual failed");
        rdpFreeRec(pScrn);
        return FALSE;
    }
    xf86SetDpi(pScrn, 0, 0);
    if (0 == pScrn->display->modes)
    {
        LOG(LOG_LEVEL_INFO, "rdpPreInit: modes error");
        rdpFreeRec(pScrn);
        return FALSE;
    }

    pScrn->virtualX = pScrn->display->virtualX;
    pScrn->virtualY = pScrn->display->virtualY;

    got_res_match = 0;
    for (modename = pScrn->display->modes; *modename != 0; modename++)
    {
        for (mode = pScrn->monitor->Modes; mode != 0; mode = mode->next)
        {
            LOG(LOG_LEVEL_TRACE, "%s %s", mode->name, *modename);
            if (0 == strcmp(mode->name, *modename))
            {
                break;
            }
        }
        if (0 == mode)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\tmode \"%s\" not found\n",
                       *modename);
            continue;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\tmode \"%s\" ok\n", *modename);
        LOG(LOG_LEVEL_TRACE, "%d %d %d %d", mode->HDisplay, dev->width,
            mode->VDisplay, dev->height);
        if ((mode->HDisplay == dev->width) && (mode->VDisplay == dev->height))
        {
            pScrn->virtualX = mode->HDisplay;
            pScrn->virtualY = mode->VDisplay;
            got_res_match = 1;
        }
        if (got_res_match)
        {
            pScrn->modes = xf86DuplicateMode(mode);
            pScrn->modes->next = pScrn->modes;
            pScrn->modes->prev = pScrn->modes;
            dev->num_modes = 1;
            break;
        }
    }
    pScrn->currentMode = pScrn->modes;
    xf86PrintModes(pScrn);
    LOG(LOG_LEVEL_TRACE, "rdpPreInit: out fPtr->num_modes %d", dev->num_modes);
    if (!got_res_match)
    {
        LOG(LOG_LEVEL_INFO,
            "rdpPreInit: could not find screen resolution %dx%d",
            dev->width, dev->height);
        return FALSE;
    }
    if (dev->glamor)
    {
#if defined(XORGXRDP_GLAMOR)
        if (xf86LoadSubModule(pScrn, GLAMOR_EGL_MODULE_NAME))
        {
            LOG(LOG_LEVEL_INFO, "rdpPreInit: glamor module load ok");
            if (glamor_egl_init(pScrn, dev->fd))
            {
                LOG(LOG_LEVEL_INFO, "rdpPreInit: glamor init ok");
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "rdpPreInit: glamor init failed");
                dev->glamor = FALSE;
            }
        }
        else
        {
            LOG(LOG_LEVEL_INFO, "rdpPreInit: glamor module load failed");
            dev->glamor = FALSE;
        }
#endif
    }
    return TRUE;
}

/******************************************************************************/
static miPointerSpriteFuncRec g_rdpSpritePointerFuncs =
{
    /* these are in rdpCursor.c */
    .RealizeCursor = rdpSpriteRealizeCursor,
    .UnrealizeCursor = rdpSpriteUnrealizeCursor,
    .SetCursor = rdpSpriteSetCursor,
    .MoveCursor = rdpSpriteMoveCursor,
    .DeviceCursorInitialize = rdpSpriteDeviceCursorInitialize,
    .DeviceCursorCleanup = rdpSpriteDeviceCursorCleanup
};

/******************************************************************************/
static Bool
rdpSaveScreen(ScreenPtr pScreen, int on)
{
    LOG(LOG_LEVEL_TRACE, "rdpSaveScreen:");
    return TRUE;
}

/******************************************************************************/
static Bool
rdpResizeSession(rdpPtr dev, int width, int height)
{
    int mmwidth;
    int mmheight;
    ScrnInfoPtr pScrn;
    Bool ok;

    LOG(LOG_LEVEL_INFO, "rdpResizeSession: width %d height %d", width, height);
    pScrn = xf86Screens[dev->pScreen->myNum];
    mmwidth = PixelToMM(width, pScrn->xDpi);
    mmheight = PixelToMM(height, pScrn->yDpi);

    ok = TRUE;
    if ((dev->width != width) || (dev->height != height))
    {
        LOG(LOG_LEVEL_INFO, "  calling RRScreenSizeSet");
        dev->allow_screen_resize = 1;
        ok = RRScreenSizeSet(dev->pScreen, width, height, mmwidth, mmheight);
        dev->allow_screen_resize = 0;
        LOG(LOG_LEVEL_INFO, "  RRScreenSizeSet ok %d", ok);
    }
    return ok;
}

/*****************************************************************************/
static void
xorgxrdpDamageReport(DamagePtr pDamage, RegionPtr pRegion, void *closure)
{
    rdpPtr dev;
    ScreenPtr pScreen;

    LOG(LOG_LEVEL_TRACE, "xorgxrdpDamageReport:");
    pScreen = (ScreenPtr)closure;
    dev = rdpGetDevFromScreen(pScreen);
    rdpClientConAddAllReg(dev, pRegion, &(pScreen->root->drawable));
}

/*****************************************************************************/
static void
xorgxrdpDamageDestroy(DamagePtr pDamage, void *closure)
{
    LOG(LOG_LEVEL_TRACE, "xorgxrdpDamageDestroy:");
}

/******************************************************************************/
/* returns error */
static CARD32
rdpDeferredDamage(OsTimerPtr timer, CARD32 now, pointer arg)
{
    ScreenPtr pScreen;
    rdpPtr dev;

    pScreen = (ScreenPtr) arg;
    dev = rdpGetDevFromScreen(pScreen);
    dev->damage = DamageCreate(xorgxrdpDamageReport, xorgxrdpDamageDestroy,
                               DamageReportRawRegion, TRUE,
                               pScreen, pScreen);
    if (dev->damage != NULL)
    {
        DamageSetReportAfterOp(dev->damage, TRUE);
        DamageRegister(&(pScreen->root->drawable), dev->damage);
    }
    return 0;
}

/******************************************************************************/
/* returns error */
static CARD32
rdpDeferredRandR(OsTimerPtr timer, CARD32 now, pointer arg)
{
    ScreenPtr pScreen;
    rrScrPrivPtr pRRScrPriv;
    rdpPtr dev;
    char *envvar;
    int width;
    int height;

    pScreen = (ScreenPtr) arg;
    dev = rdpGetDevFromScreen(pScreen);
    LOG(LOG_LEVEL_TRACE, "rdpDeferredRandR:");
    pRRScrPriv = rrGetScrPriv(pScreen);
    if (pRRScrPriv == 0)
    {
        LOG(LOG_LEVEL_INFO, "rdpDeferredRandR: rrGetScrPriv failed");
        return 1;
    }

    dev->rrSetConfig          = pRRScrPriv->rrSetConfig;
    dev->rrGetInfo            = pRRScrPriv->rrGetInfo;
    dev->rrScreenSetSize      = pRRScrPriv->rrScreenSetSize;
    dev->rrCrtcSet            = pRRScrPriv->rrCrtcSet;
    dev->rrCrtcSetGamma       = pRRScrPriv->rrCrtcSetGamma;
    dev->rrCrtcGetGamma       = pRRScrPriv->rrCrtcGetGamma;
    dev->rrOutputSetProperty  = pRRScrPriv->rrOutputSetProperty;
    dev->rrOutputValidateMode = pRRScrPriv->rrOutputValidateMode;
    dev->rrModeDestroy        = pRRScrPriv->rrModeDestroy;
    dev->rrOutputGetProperty  = pRRScrPriv->rrOutputGetProperty;
    dev->rrGetPanning         = pRRScrPriv->rrGetPanning;
    dev->rrSetPanning         = pRRScrPriv->rrSetPanning;

    LOG(LOG_LEVEL_TRACE, "  rrSetConfig = %p", dev->rrSetConfig);
    LOG(LOG_LEVEL_TRACE, "  rrGetInfo = %p", dev->rrGetInfo);
    LOG(LOG_LEVEL_TRACE, "  rrScreenSetSize = %p", dev->rrScreenSetSize);
    LOG(LOG_LEVEL_TRACE, "  rrCrtcSet = %p", dev->rrCrtcSet);
    LOG(LOG_LEVEL_TRACE, "  rrCrtcSetGamma = %p", dev->rrCrtcSetGamma);
    LOG(LOG_LEVEL_TRACE, "  rrCrtcGetGamma = %p", dev->rrCrtcGetGamma);
    LOG(LOG_LEVEL_TRACE, "  rrOutputSetProperty = %p", dev->rrOutputSetProperty);
    LOG(LOG_LEVEL_TRACE, "  rrOutputValidateMode = %p", dev->rrOutputValidateMode);
    LOG(LOG_LEVEL_TRACE, "  rrModeDestroy = %p", dev->rrModeDestroy);
    LOG(LOG_LEVEL_TRACE, "  rrOutputGetProperty = %p", dev->rrOutputGetProperty);
    LOG(LOG_LEVEL_TRACE, "  rrGetPanning = %p", dev->rrGetPanning);
    LOG(LOG_LEVEL_TRACE, "  rrSetPanning = %p", dev->rrSetPanning);

    pRRScrPriv->rrSetConfig          = rdpRRSetConfig;
    pRRScrPriv->rrGetInfo            = rdpRRGetInfo;
    pRRScrPriv->rrScreenSetSize      = rdpRRScreenSetSize;
    pRRScrPriv->rrCrtcSet            = rdpRRCrtcSet;
    pRRScrPriv->rrCrtcSetGamma       = rdpRRCrtcSetGamma;
    pRRScrPriv->rrCrtcGetGamma       = rdpRRCrtcGetGamma;
    pRRScrPriv->rrOutputSetProperty  = rdpRROutputSetProperty;
    pRRScrPriv->rrOutputValidateMode = rdpRROutputValidateMode;
    pRRScrPriv->rrModeDestroy        = rdpRRModeDestroy;
    pRRScrPriv->rrOutputGetProperty  = rdpRROutputGetProperty;
    pRRScrPriv->rrGetPanning         = rdpRRGetPanning;
    pRRScrPriv->rrSetPanning         = rdpRRSetPanning;

    rdpResizeSession(dev, 1024, 768);

    envvar = getenv("XRDP_START_WIDTH");
    if (envvar != 0)
    {
        width = atoi(envvar);
        if ((width >= 16) && (width < 8192))
        {
            envvar = getenv("XRDP_START_HEIGHT");
            if (envvar != 0)
            {
                height = atoi(envvar);
                if ((height >= 16) && (height < 8192))
                {
                    rdpResizeSession(dev, width, height);
                }
            }
        }
    }

    RRScreenSetSizeRange(pScreen, 256, 256, 16 * 1024, 16 * 1024);
    rdpRRSetRdpOutputs(dev);
    RRTellChanged(pScreen);

    return 0;
}

/******************************************************************************/
static void
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 18, 5, 0, 0)
rdpBlockHandler1(pointer blockData, OSTimePtr pTimeout, pointer pReadmask)
#else
rdpBlockHandler1(void *blockData, void *pTimeout)
#endif
{
}

/******************************************************************************/
static void
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 18, 5, 0, 0)
rdpWakeupHandler1(pointer blockData, int result, pointer pReadmask)
#else
rdpWakeupHandler1(void *blockData, int result)
#endif
{
    rdpClientConCheck((ScreenPtr)blockData);
}

/*****************************************************************************/
static Bool
rdpCreateScreenResources(ScreenPtr pScreen)
{
    Bool ret;
    rdpPtr dev;
    PixmapPtr screenPixmap;

    LOG(LOG_LEVEL_TRACE, "rdpCreateScreenResources:");
    dev = rdpGetDevFromScreen(pScreen);
    pScreen->CreateScreenResources = dev->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = rdpCreateScreenResources;
    if (!ret)
    {
        return FALSE;
    }

    if (!rdpRRScreenCreateBacking(pScreen))
    {
        LOG(LOG_LEVEL_ERROR, "rdpCreateScreenResources: rdpRRScreenCreateBacking failed");
        return FALSE;
    }

    screenPixmap = pScreen->GetScreenPixmap(pScreen);
    if (screenPixmap == NULL)
    {
        LOG(LOG_LEVEL_ERROR, "rdpCreateScreenResources: GetScreenPixmap failed");
        return FALSE;
    }

    return TRUE;
}

/*****************************************************************************/
static Bool
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
#else
rdpScreenInit(ScreenPtr pScreen, int argc, char **argv)
#endif
{
    ScrnInfoPtr pScrn;
    rdpPtr dev;
    VisualPtr vis;
    Bool vis_found;
    PictureScreenPtr ps;

    pScrn = xf86Screens[pScreen->myNum];
    dev = XRDPPTR(pScrn);

    dev->pScreen = pScreen;

    miClearVisualTypes();
    miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
                     pScrn->rgbBits, TrueColor);
    miSetPixmapDepths();
    LOG(LOG_LEVEL_INFO,
        "rdpScreenInit: virtualX %d virtualY %d rgbBits %d depth %d",
        pScrn->virtualX, pScrn->virtualY, pScrn->rgbBits, pScrn->depth);

    dev->depth = pScrn->depth;
    dev->paddedWidthInBytes = PixmapBytePad(dev->width, dev->depth);
    dev->bitsPerPixel = rdpBitsPerPixel(dev->depth);
    dev->sizeInBytes = dev->paddedWidthInBytes * dev->height;
    LOG(LOG_LEVEL_INFO, "rdpScreenInit: pfbMemory bytes %d", dev->sizeInBytes);
    if (!fbScreenInit(pScreen, NULL,
                      pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi, pScrn->displayWidth,
                      pScrn->bitsPerPixel))
    {
        LOG(LOG_LEVEL_INFO, "rdpScreenInit: fbScreenInit failed");
        return FALSE;
    }
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 14, 0, 0, 0)
    /* 1.13 has this function, 1.14 and up does not */
    miInitializeBackingStore(pScreen);
#endif

    /* try to init simd functions */
    rdpSimdInit(pScreen, pScrn);

    vis = pScreen->visuals + (pScreen->numVisuals - 1);
    while (vis >= pScreen->visuals)
    {
        if ((vis->class | DynamicClass) == DirectColor)
        {
            vis->offsetBlue = pScrn->offset.blue;
            vis->blueMask = pScrn->mask.blue;
            vis->offsetGreen = pScrn->offset.green;
            vis->greenMask = pScrn->mask.green;
            vis->offsetRed = pScrn->offset.red;
            vis->redMask = pScrn->mask.red;
        }
        vis--;
    }
    fbPictureInit(pScreen, 0, 0);
    if (dev->glamor)
    {
#if defined(XORGXRDP_GLAMOR)
        /* it's not that we don't want dri3, we just want to init it ourself */
        if (glamor_init(pScreen, GLAMOR_USE_EGL_SCREEN | GLAMOR_NO_DRI3))
        {
            LOG(LOG_LEVEL_INFO, "rdpScreenInit: glamor_init ok");
            dev->gbm = glamor_egl_get_gbm_device(pScreen);
            if (dev->gbm == NULL)
            {
                LOG(LOG_LEVEL_INFO, "rdpScreenInit: glamor_egl_get_gbm_device failed");
                return FALSE;
            }
        }
        else
        {
            LOG(LOG_LEVEL_INFO, "rdpScreenInit: glamor_init failed");
        }
        if (g_use_dri2)
        {
            if (rdpDri2Init(pScreen) != 0)
            {
                LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpDri2Init failed");
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpDri2Init ok");
            }
        }
        if (g_use_dri3)
        {
            if (rdpDri3Init(pScreen) != 0)
            {
                LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpDri3Init failed");
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpDri3Init ok");
            }
        }
#endif
    }
    xf86SetBlackWhitePixels(pScreen);
    xf86SetBackingStore(pScreen);

#if 1
    /* hardware cursor */
    dev->pCursorFuncs = xf86GetPointerScreenFuncs();
    miPointerInitialize(pScreen, &g_rdpSpritePointerFuncs,
                        dev->pCursorFuncs, 0);
#else
    /* software cursor */
    dev->pCursorFuncs = xf86GetPointerScreenFuncs();
    miDCInitialize(pScreen, dev->pCursorFuncs);
#endif

    fbCreateDefColormap(pScreen);

    /* must assign this one */
    pScreen->SaveScreen = rdpSaveScreen;

    vis_found = FALSE;
    vis = pScreen->visuals + (pScreen->numVisuals - 1);
    while (vis >= pScreen->visuals)
    {
        if (vis->vid == pScreen->rootVisual)
        {
            vis_found = TRUE;
        }
        vis--;
    }
    if (!vis_found)
    {
        LOG(LOG_LEVEL_INFO, "rdpScreenInit: no root visual");
        return FALSE;
    }

    dev->privateKeyRecGC = rdpAllocateGCPrivate(pScreen, sizeof(rdpGCRec));
    dev->privateKeyRecPixmap = rdpAllocatePixmapPrivate(pScreen, sizeof(rdpPixmapRec));

    dev->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = rdpCloseScreen;

    dev->CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = rdpCopyWindow;

    dev->CreateGC = pScreen->CreateGC;
    pScreen->CreateGC = rdpCreateGC;

    dev->CreatePixmap = pScreen->CreatePixmap;
    pScreen->CreatePixmap = rdpCreatePixmap;

    dev->DestroyPixmap = pScreen->DestroyPixmap;
    pScreen->DestroyPixmap = rdpDestroyPixmap;

    dev->ModifyPixmapHeader = pScreen->ModifyPixmapHeader;
    pScreen->ModifyPixmapHeader = rdpModifyPixmapHeader;

    ps = GetPictureScreenIfSet(pScreen);
    if (ps != 0)
    {
        /* composite */
        dev->Composite = ps->Composite;
        ps->Composite = rdpComposite;
        /* glyphs */
        dev->Glyphs = ps->Glyphs;
        ps->Glyphs = rdpGlyphs;
        /* trapezoids */
        dev->Trapezoids = ps->Trapezoids;
        ps->Trapezoids = rdpTrapezoids;
        /* triangles */
        dev->Triangles = ps->Triangles;
        ps->Triangles = rdpTriangles;
        /* composite rects */
        dev->CompositeRects = ps->CompositeRects;
        ps->CompositeRects = rdpCompositeRects;
    }

    dev->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = rdpCreateScreenResources;

    RegisterBlockAndWakeupHandlers(rdpBlockHandler1, rdpWakeupHandler1, pScreen);

    g_randr_timer = TimerSet(g_randr_timer, 0, 10, rdpDeferredRandR, pScreen);
    g_damage_timer = TimerSet(g_damage_timer, 0, 10, rdpDeferredDamage, pScreen);

    if (rdpClientConInit(dev) != 0)
    {
        LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpClientConInit failed");
    }

    dev->Bpp_mask = 0x00FFFFFF;
    dev->Bpp = 4;
    dev->bitsPerPixel = 32;

#if defined(XvExtension)
    /* XVideo */
    if (!rdpXvInit(pScreen, pScrn))
    {
        LOG(LOG_LEVEL_INFO, "rdpScreenInit: rdpXvInit failed");
    }
#endif

    if (dev->glamor)
    {
#if defined(XORGXRDP_GLAMOR)
        dev->egl = rdpEglCreate(pScreen);
#endif
    }

    LOG(LOG_LEVEL_INFO, "rdpScreenInit: out");
    return TRUE;
}

/*****************************************************************************/
static Bool
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpSwitchMode(int a, DisplayModePtr b, int c)
#else
rdpSwitchMode(ScrnInfoPtr a, DisplayModePtr b)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpSwitchMode:");
    return TRUE;
}

/*****************************************************************************/
static void
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpAdjustFrame(int a, int b, int c, int d)
#else
rdpAdjustFrame(ScrnInfoPtr a, int b, int c)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpAdjustFrame:");
}

/*****************************************************************************/
static Bool
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpEnterVT(int a, int b)
#else
rdpEnterVT(ScrnInfoPtr a)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpEnterVT:");
    return TRUE;
}

/*****************************************************************************/
static void
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpLeaveVT(int a, int b)
#else
rdpLeaveVT(ScrnInfoPtr a)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpLeaveVT:");
}

/*****************************************************************************/
static ModeStatus
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpValidMode(int a, DisplayModePtr b, Bool c, int d)
#else
rdpValidMode(ScrnInfoPtr a, DisplayModePtr b, Bool c, int d)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpValidMode:");
    return 0;
}

/*****************************************************************************/
static void
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
rdpFreeScreen(int a, int b)
#else
rdpFreeScreen(ScrnInfoPtr a)
#endif
{
    LOG(LOG_LEVEL_TRACE, "rdpFreeScreen:");
}

/*****************************************************************************/
static Bool
rdpProbe(DriverPtr drv, int flags)
{
    int num_dev_sections;
    int i;
    int entity;
    GDevPtr *dev_sections;
    Bool found_screen;
    ScrnInfoPtr pscrn;
    const char *val;

    LOG(LOG_LEVEL_TRACE, "rdpProbe:");
    if (flags & PROBE_DETECT)
    {
        return FALSE;
    }
    /* fbScreenInit, fbPictureInit, ... */
    if (!xf86LoadDrvSubModule(drv, "fb"))
    {
        LOG(LOG_LEVEL_INFO, "rdpProbe: xf86LoadDrvSubModule for fb failed");
        return FALSE;
    }

    num_dev_sections = xf86MatchDevice(XRDP_DRIVER_NAME, &dev_sections);
    if (num_dev_sections <= 0)
    {
        LOG(LOG_LEVEL_INFO, "rdpProbe: xf86MatchDevice failed");
        return FALSE;
    }

    pscrn = 0;
    found_screen = FALSE;
    for (i = 0; i < num_dev_sections; i++)
    {
        val = xf86FindOptionValue(dev_sections[i]->options, "DRMDevice");
        if (val != NULL)
        {
#if defined(XORGXRDP_GLAMOR)
            strncpy(g_drm_device, val, 127);
            g_drm_device[127] = 0;
            LOG(LOG_LEVEL_INFO,
                "rdpProbe: found DRMDevice xorg.conf value [%s]", val);
#endif
        }
        val = xf86FindOptionValue(dev_sections[i]->options, "DRI2");
        if (val != NULL)
        {
#if defined(XORGXRDP_GLAMOR)
            if ((strcmp(val, "0") == 0) ||
                (strcmp(val, "no") == 0) ||
                (strcmp(val, "false") == 0))
            {
               g_use_dri2 = 0;
            }
            LOG(LOG_LEVEL_INFO,
                "rdpProbe: found DRI2 xorg.conf value [%s]", val);
#endif
        }
        val = xf86FindOptionValue(dev_sections[i]->options, "DRI3");
        if (val != NULL)
        {
#if defined(XORGXRDP_GLAMOR)
            if ((strcmp(val, "0") == 0) ||
                (strcmp(val, "no") == 0) ||
                (strcmp(val, "false") == 0))
            {
               g_use_dri3 = 0;
            }
            LOG(LOG_LEVEL_INFO,
                "rdpProbe: found DRI3 xorg.conf value [%s]", val);
#endif
        }
        val = xf86FindOptionValue(dev_sections[i]->options, "DRMAllowList");
        if (val != NULL)
        {
#if defined(XORGXRDP_GLAMOR)
            strncpy(g_drm_allow_list, val, 127);
            g_drm_allow_list[127] = 0;
            LOG(LOG_LEVEL_INFO,
                "rdpProbe: found DRMAllowList xorg.conf value [%s]", val);
#endif
        }
        entity = xf86ClaimFbSlot(drv, 0, dev_sections[i], 1);
        pscrn = xf86ConfigFbEntity(pscrn, 0, entity, 0, 0, 0, 0);
        if (pscrn)
        {
            LOG(LOG_LEVEL_TRACE, "rdpProbe: found screen");
            found_screen = 1;
            pscrn->driverVersion = XRDP_VERSION;
            pscrn->driverName    = g_xrdp_driver_name;
            pscrn->name          = g_xrdp_driver_name;
            pscrn->Probe         = rdpProbe;
            pscrn->PreInit       = rdpPreInit;
            pscrn->ScreenInit    = rdpScreenInit;
            pscrn->SwitchMode    = rdpSwitchMode;
            pscrn->AdjustFrame   = rdpAdjustFrame;
            pscrn->EnterVT       = rdpEnterVT;
            pscrn->LeaveVT       = rdpLeaveVT;
            pscrn->ValidMode     = rdpValidMode;
            pscrn->FreeScreen    = rdpFreeScreen;
            xf86DrvMsg(pscrn->scrnIndex, X_INFO, "%s", "using default device\n");
        }
    }
    free(dev_sections);
    return found_screen;
}

/*****************************************************************************/
static const OptionInfoRec *
rdpAvailableOptions(int chipid, int busid)
{
    LOG(LOG_LEVEL_TRACE, "rdpAvailableOptions:");
    return 0;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

/*****************************************************************************/
static Bool
rdpDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flags;
    int rv;

    rv = FALSE;
    LOG(LOG_LEVEL_INFO, "rdpDriverFunc: op %d", (int)op);
    if (op == GET_REQUIRED_HW_INTERFACES)
    {
        flags = (xorgHWFlags *) ptr;
        *flags = HW_SKIP_CONSOLE;
        rv = TRUE;
    }
    return rv;
}

/*****************************************************************************/
static void
rdpIdentify(int flags)
{
    LOG(LOG_LEVEL_TRACE, "rdpIdentify:");
    xf86PrintChipsets(XRDP_DRIVER_NAME, "driver for xrdp", g_Chipsets);
}

/*****************************************************************************/
_X_EXPORT DriverRec g_DriverRec =
{
    .driverVersion = XRDP_VERSION,
    .driverName = g_xrdp_driver_name,
    .Identify = rdpIdentify,
    .Probe = rdpProbe,
    .AvailableOptions = rdpAvailableOptions,
    .driverFunc = rdpDriverFunc
};

/*****************************************************************************/
static pointer
xrdpdevSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    LOG(LOG_LEVEL_TRACE, "xrdpdevSetup:");
    if (!g_setup_done)
    {
        g_setup_done = 1;
        xf86AddDriver(&g_DriverRec, module, HaveDriverFuncs);
        return (pointer)1;
    }
    else
    {
        if (errmaj != 0)
        {
            *errmaj = LDR_ONCEONLY;
        }
        return 0;
    }
}

/*****************************************************************************/
static void
xrdpdevTearDown(pointer Module)
{
    LOG(LOG_LEVEL_TRACE, "xrdpdevTearDown:");
}

/* <drivername>ModuleData */
_X_EXPORT XF86ModuleData xrdpdevModuleData =
{
    .vers = &g_VersRec,
    .setup = xrdpdevSetup,
    .teardown = xrdpdevTearDown
};

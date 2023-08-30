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

rdp module main

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
#include <mipointrst.h>

#include <xf86xv.h>
#include <xf86Crtc.h>

#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(21, 1, 4, 0, 0)
#define XACE_DISABLE_DRI3_PRESENT
#endif

#ifdef XACE_DISABLE_DRI3_PRESENT
#include <xacestr.h>
#include <xace.h>
#endif

#include "rdp.h"
#include "rdpInput.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpMain.h"
#include "rdpPri.h"
#include "rdpPixmap.h"
#include "rdpGC.h"
#include "rdpMisc.h"
#include "rdpComposite.h"
#include "rdpGlyphs.h"
#include "rdpTrapezoids.h"
#include "rdpTriangles.h"
#include "rdpCompositeRects.h"
#include "rdpCursor.h"
#include "rdpSimd.h"
#include "rdpReg.h"
#ifdef XORGXRDP_LRANDR
#include "rdpLRandR.h"
#else
#include "rdpRandR.h"
#endif

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

static Bool g_initialised = FALSE;

static Bool g_nvidia_wrap_done = FALSE;
static DriverRec g_saved_driver;

static OsTimerPtr g_timer = NULL;
static xf86PreInitProc *g_orgPreInit;
static xf86ScreenInitProc *g_orgScreenInit;

extern DriverPtr *xf86DriverList;
extern int xf86NumDrivers;

/*****************************************************************************/
static Bool
xorgxrdpPreInit(ScrnInfoPtr pScrn, int flags)
{
    Bool rv;

    LLOGLN(0, ("xorgxrdpPreInit:"));
    rv = g_orgPreInit(pScrn, flags);
    if (rv)
    {
        pScrn->reservedPtr[0] = xnfcalloc(sizeof(rdpRec), 1);
#if defined(RANDR) && defined(XORGXRDP_LRANDR)
        noRRExtension = TRUE; /* do not use build in randr */
#endif
    }
    return rv;
}

/******************************************************************************/
static miPointerSpriteFuncRec g_rdpSpritePointerFuncs =
{
    /* these are in rdpCursor.c */
    rdpSpriteRealizeCursor,
    rdpSpriteUnrealizeCursor,
    rdpSpriteSetCursor,
    rdpSpriteMoveCursor,
    rdpSpriteDeviceCursorInitialize,
    rdpSpriteDeviceCursorCleanup
};

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

    LLOGLN(0, ("rdpCreateScreenResources:"));
    dev = rdpGetDevFromScreen(pScreen);
    pScreen->CreateScreenResources = dev->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = rdpCreateScreenResources;
    if (!ret)
    {
        return FALSE;
    }
    dev->screenSwPixmap = pScreen->CreatePixmap(pScreen,
                                                dev->width, dev->height,
                                                dev->depth,
                                                CREATE_PIXMAP_USAGE_SHARED);
    dev->pfbMemory = dev->screenSwPixmap->devPrivate.ptr;
    dev->paddedWidthInBytes = dev->screenSwPixmap->devKind;
    dev->sizeInBytes = dev->paddedWidthInBytes * dev->height;
    return TRUE;
}

/******************************************************************************/
static Bool
xorgxrdpRRScreenSetSize(ScreenPtr pScreen, CARD16 width, CARD16 height,
                        CARD32 mmWidth, CARD32 mmHeight)
{
    Bool rv;
    rdpPtr dev;
    rrScrPrivPtr pRRScrPriv;

    LLOGLN(0, ("xorgxrdpRRScreenSetSize: width %d height %d", width, height));
    dev = rdpGetDevFromScreen(pScreen);

    pRRScrPriv = rrGetScrPriv(pScreen);
    pRRScrPriv->rrScreenSetSize = dev->rrScreenSetSize;
    rv = pRRScrPriv->rrScreenSetSize(pScreen, width, height, mmWidth, mmHeight);
    pRRScrPriv->rrScreenSetSize = xorgxrdpRRScreenSetSize;

    dev->width = width;
    dev->height = height;

    pScreen->DestroyPixmap(dev->screenSwPixmap);
    dev->screenSwPixmap = pScreen->CreatePixmap(pScreen,
                                                dev->width, dev->height,
                                                dev->depth,
                                                CREATE_PIXMAP_USAGE_SHARED);
    dev->pfbMemory = dev->screenSwPixmap->devPrivate.ptr;
    dev->paddedWidthInBytes = dev->screenSwPixmap->devKind;
    dev->sizeInBytes = dev->paddedWidthInBytes * dev->height;
    return rv;
}

/*****************************************************************************/
static void
xorgxrdpDamageReport(DamagePtr pDamage, RegionPtr pRegion, void *closure)
{
    rdpPtr dev;
    ScreenPtr pScreen;

    LLOGLN(10, ("xorgxrdpDamageReport:"));
    pScreen = (ScreenPtr)closure;
    dev = rdpGetDevFromScreen(pScreen);
    rdpClientConAddAllReg(dev, pRegion, &(pScreen->root->drawable));
}

/*****************************************************************************/
static void
xorgxrdpDamageDestroy(DamagePtr pDamage, void *closure)
{
    LLOGLN(0, ("xorgxrdpDamageDestroy:"));
}

#ifdef XACE_DISABLE_DRI3_PRESENT
/*****************************************************************************/
static void
xorgxrdpExtension(CallbackListPtr *pcbl, void *unused, void *calldata)
{
    XaceExtAccessRec *rec = calldata;
    LLOGLN(10, ("xorgxrdpExtension:"));
    LLOGLN(10, ("  name %s", rec->ext->name));
    if (strcmp(rec->ext->name, "DRI3") == 0)
    {
        LLOGLN(10, ("  disabling name %s", rec->ext->name));
        rec->status = BadValue;
    }
    if (strcmp(rec->ext->name, "Present") == 0)
    {
        LLOGLN(10, ("  disabling name %s", rec->ext->name));
        rec->status = BadValue;
    }
}
#endif

/******************************************************************************/
/* returns error */
static CARD32
xorgxrdpDeferredStartup(OsTimerPtr timer, CARD32 now, pointer arg)
{
    rdpPtr dev;
    ScreenPtr pScreen;

    LLOGLN(0, ("xorgxrdpDeferredStartup:"));
    pScreen = (ScreenPtr)arg;
    if (pScreen->root != NULL)
    {
        dev = rdpGetDevFromScreen(pScreen);
#if defined(XORGXRDP_LRANDR)
        rdpLRRInit(dev);
#endif
        dev->damage = DamageCreate(xorgxrdpDamageReport, xorgxrdpDamageDestroy,
                                   DamageReportRawRegion, TRUE,
                                   pScreen, pScreen);
        if (dev->damage != NULL)
        {
            DamageSetReportAfterOp(dev->damage, TRUE);
            DamageRegister(&(pScreen->root->drawable), dev->damage);
            LLOGLN(0, ("xorgxrdpSetupDamage: DamageRegister ok"));
            TimerFree(g_timer);
            g_timer = NULL;
#ifdef XACE_DISABLE_DRI3_PRESENT
            if (getenv("XORGXRDP_NO_XACE_DISABLE_DRI3_PRESENT") == NULL)
            {
                XaceRegisterCallback(XACE_EXT_ACCESS, xorgxrdpExtension, NULL);
            }
#endif
            return 0;
        }
    }
    g_timer = TimerSet(g_timer, 0, 1, xorgxrdpDeferredStartup, pScreen);
    return 0;
}

/*****************************************************************************/
static Bool
xorgxrdpScreenInit(ScreenPtr pScreen, int argc, char** argv)
{
    Bool rv;
    rdpPtr dev;
    ScrnInfoPtr pScrn;
    PictureScreenPtr ps;
    miPointerScreenPtr PointPriv;
    rrScrPrivPtr pRRScrPriv;

    LLOGLN(0, ("xorgxrdpScreenInit:"));
    rv = g_orgScreenInit(pScreen, argc, argv);
    if (rv)
    {
        pScrn = xf86Screens[pScreen->myNum];
        dev = XRDPPTR(pScrn);
        dev->nvidia = TRUE;
        dev->pScreen = pScreen;
        dev->depth = pScrn->depth;
        dev->width = pScrn->virtualX;
        dev->height = pScrn->virtualY;
        dev->paddedWidthInBytes = PixmapBytePad(dev->width, dev->depth);
        dev->bitsPerPixel = rdpBitsPerPixel(dev->depth);
        dev->sizeInBytes = dev->paddedWidthInBytes * dev->height;

        LLOGLN(0, ("xorgxrdpScreenInit: width %d height %d", dev->width, dev->height));

        PointPriv = dixLookupPrivate(&pScreen->devPrivates, miPointerScreenKey);
        PointPriv->spriteFuncs = &g_rdpSpritePointerFuncs;

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

        if (rdpClientConInit(dev) != 0)
        {
            LLOGLN(0, ("xorgxrdpScreenInit: rdpClientConInit failed"));
        }

        dev->Bpp_mask = 0x00FFFFFF;
        dev->Bpp = 4;
        dev->bitsPerPixel = 32;

        rdpSimdInit(pScreen, pScrn);
        pRRScrPriv = rrGetScrPriv(pScreen);
        if (pRRScrPriv != NULL)
        {
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
            pRRScrPriv->rrScreenSetSize = xorgxrdpRRScreenSetSize;
        }
        g_timer = TimerSet(g_timer, 0, 1, xorgxrdpDeferredStartup, pScreen);
    }
    return rv;
}

/*****************************************************************************/
static Bool
xorgxrdpWrapPreIntScreenInit(Bool ok)
{
    if (ok && (g_orgPreInit == NULL))
    {
        if ((xf86Screens != NULL) && (xf86Screens[0] != NULL))
        {
            if ((xf86Screens[0]->PreInit != NULL) &&
                (xf86Screens[0]->ScreenInit != NULL))
            {
                g_orgPreInit = xf86Screens[0]->PreInit;
                xf86Screens[0]->PreInit = xorgxrdpPreInit;
                g_orgScreenInit = xf86Screens[0]->ScreenInit;
                xf86Screens[0]->ScreenInit = xorgxrdpScreenInit;
            }
            else
            {
                LLOGLN(0, ("xorgxrdpWrapPreIntScreenInit: error"));
            }
        }
        else
        {
            LLOGLN(0, ("xorgxrdpWrapPreIntScreenInit: error"));
        }
    }
    return ok;
}

/*****************************************************************************/
static Bool
xorgxrdpPciProbe(struct _DriverRec * drv, int entity_num,
                 struct pci_device * dev, intptr_t match_data)
{
    Bool rv;

    LLOGLN(0, ("xorgxrdpPciProbe:"));
    rv = g_saved_driver.PciProbe(drv, entity_num, dev, match_data);
    return xorgxrdpWrapPreIntScreenInit(rv);
}

/*****************************************************************************/
static Bool
xorgxrdpPlatformProbe(struct _DriverRec * drv, int entity_num, int flags,
                      struct xf86_platform_device * dev, intptr_t match_data)
{
    Bool rv;

    LLOGLN(0, ("xorgxrdpPlatformProbe:"));
    rv = g_saved_driver.platformProbe(drv, entity_num, flags, dev, match_data);
    return xorgxrdpWrapPreIntScreenInit(rv);
}

/*****************************************************************************/
static Bool
xorgxrdpDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    xorgHWFlags *flags;
    Bool rv;

    LLOGLN(0, ("xorgxrdpDriverFunc:"));
    rv = g_saved_driver.driverFunc(pScrn, op, ptr);
    if (op == GET_REQUIRED_HW_INTERFACES)
    {
        flags = (xorgHWFlags *) ptr;
        *flags = HW_SKIP_CONSOLE;
        rv = TRUE;
    }
    return rv;
}

/*****************************************************************************/
int
xorgxrdpCheckWrap(void)
{
    if (g_nvidia_wrap_done)
    {
        return 0;
    }
    if (xf86NumDrivers < 1)
    {
        return 0;
    }
    if ((xf86DriverList == NULL) || (xf86DriverList[0] == NULL) ||
        (xf86DriverList[0]->driverName == NULL))
    {
        return 0;
    }
    if (strcmp(xf86DriverList[0]->driverName, "NVIDIA") == 0)
    {
        g_saved_driver = *(xf86DriverList[0]);
        g_nvidia_wrap_done = TRUE;
        LLOGLN(0, ("xorgxrdpCheckWrap: NVIDIA driver found"));
        xf86DriverList[0]->PciProbe = xorgxrdpPciProbe;
        xf86DriverList[0]->platformProbe = xorgxrdpPlatformProbe;
        xf86DriverList[0]->driverFunc = xorgxrdpDriverFunc;
    }
    return 0;
}

/*****************************************************************************/
static pointer
xorgxrdpSetup(pointer Module, pointer Options,
              int *ErrorMajor, int *ErrorMinor)
{
    LLOGLN(0, ("xorgxrdpSetup:"));
    if (!g_initialised)
    {
        g_initialised = TRUE;
    }
    rdpInputInit();
    rdpPrivateInit();
    return (pointer) 1;
}

/*****************************************************************************/
static void
xorgxrdpTearDown(pointer Module)
{
    LLOGLN(0, ("xorgxrdpTearDown:"));
}

/*****************************************************************************/
void
xorgxrdpDownDown(ScreenPtr pScreen)
{
    LLOGLN(0, ("xorgxrdpDownDown:"));
    if (g_initialised)
    {
        g_initialised = FALSE;
        LLOGLN(0, ("xorgxrdpDownDown: 1"));
        rdpClientConDeinit(rdpGetDevFromScreen(pScreen));
    }
}

static MODULESETUPPROTO(xorgxrdpSetup);
static XF86ModuleVersionInfo RDPVersRec =
{
    XRDP_MODULE_NAME,
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

_X_EXPORT XF86ModuleData xorgxrdpModuleData =
{
    &RDPVersRec,
    xorgxrdpSetup,
    xorgxrdpTearDown
};

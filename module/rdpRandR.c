/*
Copyright 2011-2014 Jay Sorg

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

RandR draw calls

*/

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

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpReg.h"
#include "rdpMisc.h"
#include "rdpRandR.h"

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/******************************************************************************/
Bool
rdpRRRegisterSize(ScreenPtr pScreen, int width, int height)
{
    int mmwidth;
    int mmheight;
    RRScreenSizePtr pSize;

    LLOGLN(0, ("rdpRRRegisterSize: width %d height %d", width, height));
    mmwidth = PixelToMM(width);
    mmheight = PixelToMM(height);
    pSize = RRRegisterSize(pScreen, width, height, mmwidth, mmheight);
    /* Tell RandR what the current config is */
    RRSetCurrentConfig(pScreen, RR_Rotate_0, 0, pSize);
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRSetConfig(ScreenPtr pScreen, Rotation rotateKind, int rate,
               RRScreenSizePtr pSize)
{
    LLOGLN(0, ("rdpRRSetConfig:"));
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRGetInfo(ScreenPtr pScreen, Rotation *pRotations)
{
    int width;
    int height;
    rdpPtr dev;

    LLOGLN(0, ("rdpRRGetInfo:"));
    dev = rdpGetDevFromScreen(pScreen);
    *pRotations = RR_Rotate_0;
    width = dev->width;
    height = dev->height;
    rdpRRRegisterSize(pScreen, width, height);
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRScreenSetSize(ScreenPtr pScreen, CARD16 width, CARD16 height,
                   CARD32 mmWidth, CARD32 mmHeight)
{
    WindowPtr root;
    PixmapPtr screenPixmap;
    BoxRec box;
    rdpPtr dev;

    LLOGLN(0, ("rdpRRScreenSetSize: width %d height %d mmWidth %d mmHeight %d",
           width, height, (int)mmWidth, (int)mmHeight));
    dev = rdpGetDevFromScreen(pScreen);
    root = rdpGetRootWindowPtr(pScreen);
    if ((width < 1) || (height < 1))
    {
        LLOGLN(10, ("  error width %d height %d", width, height));
        return FALSE;
    }
    dev->width = width;
    dev->height = height;
    dev->paddedWidthInBytes = PixmapBytePad(dev->width, dev->depth);
    dev->sizeInBytes = dev->paddedWidthInBytes * dev->height;
    pScreen->width = width;
    pScreen->height = height;
    pScreen->mmWidth = mmWidth;
    pScreen->mmHeight = mmHeight;
    screenPixmap = pScreen->GetScreenPixmap(pScreen);
    g_free(dev->pfbMemory_alloc);
    dev->pfbMemory_alloc = (char *) g_malloc(dev->sizeInBytes + 16, 1);
    dev->pfbMemory = (char *) RDPALIGN(dev->pfbMemory_alloc, 16);
    if (screenPixmap != 0)
    {
        pScreen->ModifyPixmapHeader(screenPixmap, width, height,
                                    -1, -1,
                                    dev->paddedWidthInBytes,
                                    dev->pfbMemory);
    }
    box.x1 = 0;
    box.y1 = 0;
    box.x2 = width;
    box.y2 = height;
    rdpRegionInit(&root->winSize, &box, 1);
    rdpRegionInit(&root->borderSize, &box, 1);
    rdpRegionReset(&root->borderClip, &box);
    rdpRegionBreak(&root->clipList);
    root->drawable.width = width;
    root->drawable.height = height;
    ResizeChildrenWinSize(root, 0, 0, 0, 0);
    RRGetInfo(pScreen, 1);
    LLOGLN(0, ("  screen resized to %dx%d", pScreen->width, pScreen->height));
    RRScreenSizeNotify(pScreen);
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 13, 0, 0, 0)
    xf86EnableDisableFBAccess(pScreen->myNum, FALSE);
    xf86EnableDisableFBAccess(pScreen->myNum, TRUE);
#else
    xf86EnableDisableFBAccess(xf86Screens[pScreen->myNum], FALSE);
    xf86EnableDisableFBAccess(xf86Screens[pScreen->myNum], TRUE);
#endif
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRCrtcSet(ScreenPtr pScreen, RRCrtcPtr crtc, RRModePtr mode,
             int x, int y, Rotation rotation, int numOutputs,
             RROutputPtr *outputs)
{
    LLOGLN(0, ("rdpRRCrtcSet:"));
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRCrtcSetGamma(ScreenPtr pScreen, RRCrtcPtr crtc)
{
    LLOGLN(0, ("rdpRRCrtcSetGamma:"));
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRCrtcGetGamma(ScreenPtr pScreen, RRCrtcPtr crtc)
{
    LLOGLN(0, ("rdpRRCrtcGetGamma: %p %p %p %p", crtc, crtc->gammaRed,
           crtc->gammaBlue, crtc->gammaGreen));
    crtc->gammaSize = 1;
    if (crtc->gammaRed == NULL)
    {
        crtc->gammaRed = g_malloc(32, 1);
    }
    if (crtc->gammaBlue == NULL)
    {
        crtc->gammaBlue = g_malloc(32, 1);
    }
    if (crtc->gammaGreen == NULL)
    {
        crtc->gammaGreen = g_malloc(32, 1);
    }
    return TRUE;
}

/******************************************************************************/
Bool
rdpRROutputSetProperty(ScreenPtr pScreen, RROutputPtr output, Atom property,
                       RRPropertyValuePtr value)
{
    LLOGLN(0, ("rdpRROutputSetProperty:"));
    return TRUE;
}

/******************************************************************************/
Bool
rdpRROutputValidateMode(ScreenPtr pScreen, RROutputPtr output,
                        RRModePtr mode)
{
    LLOGLN(0, ("rdpRROutputValidateMode:"));
    return TRUE;
}

/******************************************************************************/
void
rdpRRModeDestroy(ScreenPtr pScreen, RRModePtr mode)
{
    LLOGLN(0, ("rdpRRModeDestroy:"));
}

/******************************************************************************/
Bool
rdpRROutputGetProperty(ScreenPtr pScreen, RROutputPtr output, Atom property)
{
    LLOGLN(0, ("rdpRROutputGetProperty:"));
    return TRUE;
}

/******************************************************************************/
static int
get_rect(rdpPtr dev, const char *name, BoxPtr rect)
{
    int index;
    char text[256];

    if (strcmp("default", name) == 0)
    {
        /* should be primary */
        rect->x1 = dev->minfo[0].left;
        rect->y1 = dev->minfo[0].top;
        rect->x2 = dev->minfo[0].right + 1;
        rect->y2 = dev->minfo[0].bottom + 1;
        return 0;
    }
    for (index = 1; index < 16; index++)
    {
        snprintf(text, 255, "default%d", index);
        if (strcmp(text, name) == 0)
        {
            rect->x1 = dev->minfo[index].left;
            rect->y1 = dev->minfo[index].top;
            rect->x2 = dev->minfo[index].right + 1;
            rect->y2 = dev->minfo[index].bottom + 1;
            return 0;
        }
    }
    /* not found */
    return 1;
}

/******************************************************************************/
Bool
rdpRRGetPanning(ScreenPtr pScreen, RRCrtcPtr crtc, BoxPtr totalArea,
                BoxPtr trackingArea, INT16 *border)
{
    rdpPtr dev;
    BoxRec totalAreaRect;
    BoxRec trackingAreaRect;

    LLOGLN(0, ("rdpRRGetPanning: %p", crtc));
    dev = rdpGetDevFromScreen(pScreen);

    totalAreaRect.x1 = 0;
    totalAreaRect.y1 = 0;
    totalAreaRect.x2 = dev->width;
    totalAreaRect.y2 = dev->height;

    trackingAreaRect.x1 = 0;
    trackingAreaRect.y1 = 0;
    trackingAreaRect.x2 = dev->width;
    trackingAreaRect.y2 = dev->height;

    if (dev->doMultimon)
    {
        if (crtc != 0)
        {
            if (crtc->numOutputs > 0)
            {
                if (crtc->outputs != 0)
                {
                    if (crtc->outputs[0] != 0)
                    {
                        if (crtc->outputs[0]->name != 0)
                        {
                            if (get_rect(dev, crtc->outputs[0]->name,
                                         &totalAreaRect) == 0)
                            {
                                LLOGLN(0, ("rdpRRGetPanning: get_rect ok"));
                                trackingAreaRect = totalAreaRect;
                            }
                            else
                            {
                                LLOGLN(0, ("rdpRRGetPanning: %s not found", crtc->outputs[0]->name));
                            }
                        }
                    }
                }
            }
        }
    }

    if (totalArea != 0)
    {
        *totalArea = totalAreaRect;
    }

    if (trackingArea != 0)
    {
        *trackingArea = trackingAreaRect;
    }

    if (border != 0)
    {
        border[0] = 0;
        border[1] = 0;
        border[2] = 0;
        border[3] = 0;
    }
    return TRUE;
}

/******************************************************************************/
Bool
rdpRRSetPanning(ScreenPtr pScreen, RRCrtcPtr crtc, BoxPtr totalArea,
                BoxPtr trackingArea, INT16 *border)
{
    LLOGLN(0, ("rdpRRSetPanning:"));
    return TRUE;
}

/******************************************************************************/
static int
rdpRRAddOutput(rdpPtr dev, const char *aname)
{
    RRModePtr mode;
    RRCrtcPtr crtc;
    RROutputPtr output;
    xRRModeInfo modeInfo;
    char name[64];

    sprintf (name, "%dx%d", dev->width, dev->height);
    memset (&modeInfo, 0, sizeof(modeInfo));
    modeInfo.width = dev->width;
    modeInfo.height = dev->height;
    modeInfo.nameLength = strlen(name);
    mode = RRModeGet(&modeInfo, name);
    if (mode == 0)
    {
        LLOGLN(0, ("rdpRRAddOutput: RRModeGet failed"));
        return 1;
    }
    crtc = RRCrtcCreate(dev->pScreen, NULL);
    if (crtc == 0)
    {
        LLOGLN(0, ("rdpRRAddOutput: RRCrtcCreate failed"));
        RRModeDestroy(mode);
        return 1;
    }
    output = RROutputCreate(dev->pScreen, aname, strlen(aname), NULL);
    if (output == 0)
    {
        LLOGLN(0, ("rdpRRAddOutput: RROutputCreate failed"));
        RRCrtcDestroy(crtc);
        RRModeDestroy(mode);
        return 1;
    }
    if (!RROutputSetClones(output, NULL, 0))
    {
        LLOGLN(0, ("rdpRRAddOutput: RROutputSetClones failed"));
    }
    if (!RROutputSetModes(output, &mode, 1, 0))
    {
        LLOGLN(0, ("rdpRRAddOutput: RROutputSetModes failed"));
    }
    if (!RROutputSetCrtcs(output, &crtc, 1))
    {
        LLOGLN(0, ("rdpRRAddOutput: RROutputSetCrtcs failed"));
    }
    if (!RROutputSetConnection(output, RR_Connected))
    {
        LLOGLN(0, ("rdpRRAddOutput: RROutputSetConnection failed"));
    }
    RRCrtcNotify(crtc, mode, 0, 0, RR_Rotate_0, NULL, 1, &output);

    dev->output[dev->extra_outputs] = output;
    dev->crtc[dev->extra_outputs] = crtc;
    dev->extra_outputs++;

    return 0;
}

/******************************************************************************/
int
rdpRRSetExtraOutputs(rdpPtr dev, int extra_outputs)
{
    char text[256];
    RRCrtcPtr crtc;
    RROutputPtr output;

    if (dev->extra_outputs == extra_outputs)
    {
        return 0;
    }
    while (dev->extra_outputs < extra_outputs)
    {
        snprintf(text, 255, "default%d", dev->extra_outputs + 1);
        if (rdpRRAddOutput(dev, text) != 0)
        {
            LLOGLN(0, ("rdpRRSetNumOutputs: rdpRRAddOutput failed"));
            return 1;
        }
    }
    while ((dev->extra_outputs > 0) && (dev->extra_outputs > extra_outputs))
    {
        dev->extra_outputs--;
        crtc = dev->crtc[dev->extra_outputs];
        RRCrtcDestroy(crtc);
        output = dev->output[dev->extra_outputs];
        RROutputDestroy(output);
    }
    return 0;
}


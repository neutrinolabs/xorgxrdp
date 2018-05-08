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

#include <mipict.h>
#include <picture.h>

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpReg.h"
#include "rdpCompositeRects.h"

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/******************************************************************************/
static void
rdpCompositeRectsOrg(PictureScreenPtr ps, rdpPtr dev, CARD8 op,
                     PicturePtr dst, xRenderColor * color,
                     int num_rects, xRectangle *rects)
{
    ps->CompositeRects = dev->CompositeRects;
    ps->CompositeRects(op, dst, color, num_rects, rects);
    ps->CompositeRects = rdpCompositeRects;
}

/******************************************************************************/
void
rdpCompositeRects(CARD8 op, PicturePtr dst, xRenderColor * color,
                  int num_rects, xRectangle *rects)
{
    ScreenPtr pScreen;
    rdpPtr dev;
    PictureScreenPtr ps;
    RegionPtr reg;

    LLOGLN(10, ("rdpCompositeRects:"));
    pScreen = dst->pDrawable->pScreen;
    dev = rdpGetDevFromScreen(pScreen);
    dev->counts.rdpCompositeRectsCallCount++;
    reg = rdpRegionFromRects(num_rects, rects, CT_NONE);
    rdpRegionTranslate(reg, dst->pDrawable->x, dst->pDrawable->y);
    if (dst->pCompositeClip != NULL)
    {
        rdpRegionIntersect(reg, dst->pCompositeClip, reg);
    }
    ps = GetPictureScreen(pScreen);
    /* do original call */
    rdpCompositeRectsOrg(ps, dev, op, dst, color, num_rects, rects);
    rdpClientConAddAllReg(dev, reg, dst->pDrawable);
    rdpRegionDestroy(reg);
}

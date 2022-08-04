/*
Copyright 2005-2017 Jay Sorg

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

#include "rdp.h"
#include "rdpDraw.h"
#include "rdpClientCon.h"
#include "rdpReg.h"
#include "rdpPutImage.h"

#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/******************************************************************************/
static void
rdpPutImageOrg(DrawablePtr pDst, GCPtr pGC, int depth, int x, int y,
               int w, int h, int leftPad, int format, char *pBits)
{
    GC_OP_VARS;

    GC_OP_PROLOGUE(pGC);
    pGC->ops->PutImage(pDst, pGC, depth, x, y, w, h, leftPad,
                       format, pBits);
    GC_OP_EPILOGUE(pGC);
}

/******************************************************************************/
void
rdpPutImage(DrawablePtr pDst, GCPtr pGC, int depth, int x, int y,
            int w, int h, int leftPad, int format, char *pBits)
{
    rdpPtr dev;
    RegionRec clip_reg;
    RegionRec reg;
    int cd;
    BoxRec box;
    PixmapPtr pixmap;
    int *pBits32;
    rdpClientCon *clientCon;
    ScreenPtr pScreen;

    LLOGLN(10, ("rdpPutImage:"));
    pScreen = pGC->pScreen;
    dev = rdpGetDevFromScreen(pScreen);
    if ((x == 0) && (y == 0) && (w == 4) && (h == 4) && (depth >= 24) &&
        (pDst->type == DRAWABLE_PIXMAP))
    {
        pBits32 = (int *) pBits;
        if (pBits32[0] == 0xDEADBEEF)
        {
            clientCon = dev->clientConHead;
            while (clientCon != NULL)
            {
                if (clientCon->conNumber == pBits32[1])
                {
                    /* free old */
                    pixmap = clientCon->helperPixmaps[pBits32[2] & 0xF];
                    if (pixmap != NULL)
                    {
                        pScreen->DestroyPixmap(pixmap);
                    }
                    /* set new */
                    pixmap = (PixmapPtr) pDst;
                    LLOGLN(0, ("rdpPutImage: setting conNumber %d, monitor num %d "
                           "to pixmap %p", pBits32[1], pBits32[2], pixmap));
                    clientCon->helperPixmaps[pBits32[2] & 0xF] = pixmap;
                    /* so it can not get freed early */
                    pixmap->refcnt++;
                    break;
                }
                clientCon = clientCon->next;
            }
            return;
        }
    }
    dev->counts.rdpPutImageCallCount++;
    box.x1 = x + pDst->x;
    box.y1 = y + pDst->y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;
    rdpRegionInit(&reg, &box, 0);
    rdpRegionInit(&clip_reg, NullBox, 0);
    cd = rdpDrawGetClip(dev, &clip_reg, pDst, pGC);
    LLOGLN(10, ("rdpPutImage: cd %d", cd));
    if (cd == XRDP_CD_CLIP)
    {
        rdpRegionIntersect(&reg, &clip_reg, &reg);
    }
    /* do original call */
    rdpPutImageOrg(pDst, pGC, depth, x, y, w, h, leftPad, format, pBits);
    if (cd != XRDP_CD_NODRAW)
    {
        rdpClientConAddAllReg(dev, &reg, pDst);
    }
    rdpRegionUninit(&clip_reg);
    rdpRegionUninit(&reg);
}

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

#ifndef _RDP_H
#define _RDP_H

#include <xorg-server.h>
#include <xorgVersion.h>
#include <xf86.h>

#include <scrnintstr.h>
#include <gcstruct.h>
#include <mipointer.h>
#include <randrstr.h>
#include <damage.h>

#include "rdpPri.h"

#include "xrdp_client_info.h"
#include "xrdp_constants.h"

#define XRDP_MODULE_NAME "XORGXRDP"
#define XRDP_DRIVER_NAME "XRDPDEV"
#define XRDP_MOUSE_NAME "XRDPMOUSE"
#define XRDP_KEYB_NAME "XRDPKEYB"
#define XRDP_VERSION 1000

#define RDP_MAX_TILES 4096

#define COLOR8(r, g, b) \
    ((((r) >> 5) << 0)  | (((g) >> 5) << 3) | (((b) >> 6) << 6))
#define COLOR15(r, g, b) \
    ((((r) >> 3) << 10) | (((g) >> 3) << 5) | (((b) >> 3) << 0))
#define COLOR16(r, g, b) \
    ((((r) >> 3) << 11) | (((g) >> 2) << 5) | (((b) >> 3) << 0))
#define COLOR24(r, g, b) \
    ((((r) >> 0) << 0)  | (((g) >> 0) << 8) | (((b) >> 0) << 16))
#define SPLITCOLOR32(r, g, b, c) \
    do { \
        r = ((c) >> 16) & 0xff; \
        g = ((c) >> 8) & 0xff; \
        b = (c) & 0xff; \
    } while (0)

#define PixelToMM(_size, _dpi) (((_size) * 254 + (_dpi) * 5) / ((_dpi) * 10))

#define RDPMIN(_val1, _val2) ((_val1) < (_val2) ? (_val1) : (_val2))
#define RDPMAX(_val1, _val2) ((_val1) < (_val2) ? (_val2) : (_val1))
#define RDPCLAMP(_val, _lo, _hi) \
    ((_val) < (_lo) ? (_lo) : (_val) > (_hi) ? (_hi) : (_val))
#define RDPALIGN(_val, _al) ((((uintptr_t)(_val)) + ((_al) - 1)) & ~((_al) - 1))

#define XRDP_RFX_ALIGN 64
#define XRDP_H264_ALIGN 16

#define XRDP_CD_NODRAW 0
#define XRDP_CD_NOCLIP 1
#define XRDP_CD_CLIP   2

#if 0
#define RegionCopy DONOTUSE
#define RegionTranslate DONOTUSE
#define RegionNotEmpty DONOTUSE
#define RegionIntersect DONOTUSE
#define RegionContainsRect DONOTUSE
#define RegionInit DONOTUSE
#define RegionUninit DONOTUSE
#define RegionFromRects DONOTUSE
#define RegionDestroy DONOTUSE
#define RegionCreate DONOTUSE
#define RegionUnion DONOTUSE
#define RegionSubtract DONOTUSE
#define RegionInverse DONOTUSE
#define RegionExtents DONOTUSE
#define RegionReset DONOTUSE
#define RegionBreak DONOTUSE
#define RegionUnionRect DONOTUSE
#endif

struct image_data
{
    int left;
    int top;
    int width;
    int height;
    int bpp;
    int Bpp;
    int lineBytes;
    int flags;
    uint8_t *pixels;
    uint8_t *shmem_pixels;
    int shmem_fd;
    int shmem_bytes;
    int shmem_offset;
    int shmem_lineBytes;
};

/* defined in rdpClientCon.h */
typedef struct _rdpClientCon rdpClientCon;

struct _rdpPointer
{
    int cursor_x;
    int cursor_y;
    int old_button_mask;
    int button_mask;
    DeviceIntPtr device;
    int old_cursor_x;
    int old_cursor_y;
};
typedef struct _rdpPointer rdpPointer;

struct _rdpKeyboard
{
    int pause_spe;
    int ctrl_down;
    int alt_down;
    int shift_down;
    int tab_down;
    /* this is toggled every time num lock key is released, not like the
       above *_down vars */
    int scroll_lock_down;
    DeviceIntPtr device;
};
typedef struct _rdpKeyboard rdpKeyboard;


struct _rdpPixmapRec
{
    int status;
    int rdpindex;
    int con_number;
    int is_dirty;
    int is_scratch;
    int is_alpha_dirty_not;
    /* number of times used in a remote operation
       if this gets above XRDP_USE_COUNT_THRESHOLD
       then we force remote the pixmap */
    int use_count;
    int kind_width;
    struct rdp_draw_item *draw_item_head;
    struct rdp_draw_item *draw_item_tail;
};
typedef struct _rdpPixmapRec rdpPixmapRec;
typedef struct _rdpPixmapRec * rdpPixmapPtr;
#define GETPIXPRIV(_dev, _pPixmap) (rdpPixmapPtr) \
rdpGetPixmapPrivate(&((_pPixmap)->devPrivates),  (_dev)->privateKeyRecPixmap)

struct _rdpCounts
{
    CARD32 rdpFillSpansCallCount; /* 1 */
    CARD32 rdpSetSpansCallCount;
    CARD32 rdpPutImageCallCount;
    CARD32 rdpCopyAreaCallCount;
    CARD32 rdpCopyPlaneCallCount;
    CARD32 rdpPolyPointCallCount;
    CARD32 rdpPolylinesCallCount;
    CARD32 rdpPolySegmentCallCount;
    CARD32 rdpPolyRectangleCallCount;
    CARD32 rdpPolyArcCallCount; /* 10 */
    CARD32 rdpFillPolygonCallCount;
    CARD32 rdpPolyFillRectCallCount;
    CARD32 rdpPolyFillArcCallCount;
    CARD32 rdpPolyText8CallCount;
    CARD32 rdpPolyText16CallCount;
    CARD32 rdpImageText8CallCount;
    CARD32 rdpImageText16CallCount;
    CARD32 rdpImageGlyphBltCallCount;
    CARD32 rdpPolyGlyphBltCallCount;
    CARD32 rdpPushPixelsCallCount; /* 20 */
    CARD32 rdpCompositeCallCount;
    CARD32 rdpCopyWindowCallCount; /* 22 */
    CARD32 rdpTrapezoidsCallCount;
    CARD32 rdpTrianglesCallCount;
    CARD32 rdpCompositeRectsCallCount;
    CARD32 callCount[64 - 25];
};

typedef int (*yuv_to_rgb32_proc)(const uint8_t *yuvs, int width, int height, int *rgbs);

typedef int (*copy_box_proc)(const uint8_t *s8, int src_stride,
                             uint8_t *d8, int dst_stride,
                             int width, int height);
/* copy_box_proc but 2 dest */
typedef int (*copy_box_dst2_proc)(const uint8_t *s8, int src_stride,
                                  uint8_t *d8_y, int dst_stride_y,
                                  uint8_t *d8_uv, int dst_stride_uv,
                                  int width, int height);

/* move this to common header */
struct _rdpRec
{
    int width;
    int height;
    int depth;
    int paddedWidthInBytes;
    int sizeInBytes;
    int num_modes;
    int bitsPerPixel;
    int Bpp;
    int Bpp_mask;
    uint8_t *pfbMemory_alloc;
    uint8_t *pfbMemory;
    ScreenPtr pScreen;
    rdpDevPrivateKey privateKeyRecGC;
    rdpDevPrivateKey privateKeyRecPixmap;

    CopyWindowProcPtr CopyWindow;
    CreateGCProcPtr CreateGC;
    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;
    ModifyPixmapHeaderProcPtr ModifyPixmapHeader;
    CloseScreenProcPtr CloseScreen;
    CompositeProcPtr Composite;
    GlyphsProcPtr Glyphs;
    TrapezoidsProcPtr Trapezoids;
    CreateScreenResourcesProcPtr CreateScreenResources;
    TrianglesProcPtr Triangles;
    CompositeRectsProcPtr CompositeRects;

    /* keyboard and mouse */
    miPointerScreenFuncPtr pCursorFuncs;
    /* mouse */
    rdpPointer pointer;
    /* keyboard */
    rdpKeyboard keyboard;

    /* RandR */
    RRSetConfigProcPtr rrSetConfig;
    RRGetInfoProcPtr rrGetInfo;
    RRScreenSetSizeProcPtr rrScreenSetSize;
    RRCrtcSetProcPtr rrCrtcSet;
    RRCrtcSetGammaProcPtr rrCrtcSetGamma;
    RRCrtcGetGammaProcPtr rrCrtcGetGamma;
    RROutputSetPropertyProcPtr rrOutputSetProperty;
    RROutputValidateModeProcPtr rrOutputValidateMode;
    RRModeDestroyProcPtr rrModeDestroy;
    RROutputGetPropertyProcPtr rrOutputGetProperty;
    RRGetPanningProcPtr rrGetPanning;
    RRSetPanningProcPtr rrSetPanning;
    int allow_screen_resize;

    int listen_sck;
    char uds_data[256];
    int disconnect_sck;
    char disconnect_uds[256];
    rdpClientCon *clientConHead;
    rdpClientCon *clientConTail;

    rdpPixmapRec screenPriv;
    int sendUpdateScheduled; /* boolean */
    OsTimerPtr sendUpdateTimer;

    int do_dirty_ons; /* boolean */
    int disconnect_scheduled; /* boolean */
    int do_kill_disconnected; /* boolean */

    OsTimerPtr disconnectTimer;
    int disconnect_timeout_s;
    CARD32 disconnect_time_ms;

    OsTimerPtr idleDisconnectTimer;
    int idle_disconnect_timeout_s;
    CARD32 last_event_time_ms;
    CARD32 last_wheel_time_ms;

    int conNumber;

    struct _rdpCounts counts;

    yuv_to_rgb32_proc i420_to_rgb32;
    yuv_to_rgb32_proc yv12_to_rgb32;
    yuv_to_rgb32_proc yuy2_to_rgb32;
    yuv_to_rgb32_proc uyvy_to_rgb32;
    uint8_t *xv_data;
    int xv_data_bytes;
    int xv_timer_scheduled;
    OsTimerPtr xv_timer;

    copy_box_proc a8r8g8b8_to_a8b8g8r8_box;
    copy_box_dst2_proc a8r8g8b8_to_nv12_box;

    /* multimon */
    struct monitor_info minfo[16]; /* client monitor data */
    int doMultimon;
    int monitorCount;
    /* glamor */
    Bool glamor;
    PixmapPtr screenSwPixmap;
    void *xvPutImage;
    /* dri */
    int fd;
    /* egl */
    void *egl;
    DamagePtr damage;
};
typedef struct _rdpRec rdpRec;
typedef struct _rdpRec * rdpPtr;
#define XRDPPTR(_p) ((rdpPtr)((_p)->driverPrivate))

struct _rdpGCRec
{
/* changed to const in d89b42b */
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 15, 99, 901, 0)
    GCFuncs *funcs;
    GCOps *ops;
#else
    const GCFuncs *funcs;
    const GCOps *ops;
#endif
};
typedef struct _rdpGCRec rdpGCRec;
typedef struct _rdpGCRec * rdpGCPtr;

#define RDI_FILL 1
#define RDI_IMGLL 2 /* lossless */
#define RDI_IMGLY 3 /* lossy */
#define RDI_LINE 4
#define RDI_SCRBLT 5
#define RDI_TEXT 6

struct urdp_draw_item_fill
{
    int opcode;
    int fg_color;
    int bg_color;
    int pad0;
};

struct urdp_draw_item_img
{
    int opcode;
    int pad0;
};

struct urdp_draw_item_line
{
    int opcode;
    int fg_color;
    int bg_color;
    int width;
    xSegment* segs;
    int nseg;
    int flags;
};

struct urdp_draw_item_scrblt
{
    int srcx;
    int srcy;
    int dstx;
    int dsty;
    int cx;
    int cy;
};

struct urdp_draw_item_text
{
    int opcode;
    int fg_color;
    struct rdp_text* rtext; /* in rdpglyph.h */
};

union urdp_draw_item
{
    struct urdp_draw_item_fill fill;
    struct urdp_draw_item_img img;
    struct urdp_draw_item_line line;
    struct urdp_draw_item_scrblt scrblt;
    struct urdp_draw_item_text text;
};

struct rdp_draw_item
{
    int type; /* RDI_FILL, RDI_IMGLL, ... */
    int flags;
    struct rdp_draw_item* prev;
    struct rdp_draw_item* next;
    RegionPtr reg;
    union urdp_draw_item u;
};

#define XRDP_USE_COUNT_THRESHOLD 1
#endif

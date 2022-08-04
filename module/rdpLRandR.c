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
#include "rdpMisc.h"
#include "rdpReg.h"

#if defined(XORGXRDP_GLAMOR)
#include <glamor.h>
#endif

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/* 
            start   end
crtc ids    1       16
output ids  17      32
mode ids    33      48
*/

#define LRANDR_NAME                     "RANDR"
#define SERVER_LRANDR_MAJOR_VERSION     1
#define SERVER_LRANDR_MINOR_VERSION     3
#define LRRNumberEvents                 2
#define LRRNumberErrors                 4
#define LRRNumberRequests               32 /* 1.3 */
#define LRRMaxCrtcs                     16
#define LRRMaxOutputs                   16
#define LRRMaxOutputNameLength          16
#define LRRMaxModes                     16
#define LRRMaxModesNameLength           16
#define LRRCrtcStart                    1
#define LRROutputStart                  17
#define LRRModeStart                    33

#define OUTPUT2CRTC(_output)    ((_output) - LRRMaxCrtcs)
#define OUTPUT2MODE(_output)    ((_output) + LRRMaxOutputs)

#define CRTC2OUTPUT(_crtc)      ((_crtc) + LRRMaxCrtcs)
#define CRTC2MODE(_crtc)        ((_crtc) + LRRMaxCrtcs + LRRMaxOutputs)

struct _interestedClientRec
{
    ClientPtr pClient;
    XID window;
    CARD32 mask;
    struct xorg_list entry;
};
typedef struct _interestedClientRec interestedClientRec;

struct _LRRCrtcRec
{
    RRCrtc id; /* XID */
    int x;
    int y;
    int width;
    int height;
};
typedef struct _LRRCrtcRec LRRCrtcRec;

struct _LRROutputRec
{
    RROutput id; /* XID */
    char name[LRRMaxOutputNameLength];
};
typedef struct _LRROutputRec LRROutputRec;

static int g_numCrtcs = 0;
static LRRCrtcRec g_crtcs[LRRMaxCrtcs];

static int g_numOutputs = 0;
static LRROutputRec g_outputs[LRRMaxOutputs];
static RROutput g_primaryOutput = None; /* XID */

static int g_numModes = 0;
static xRRModeInfo g_modes[LRRMaxModes];
static char g_modeNames[LRRMaxModes][LRRMaxModesNameLength];

static CARD32 g_updateTime;

static int (*g_procLRandrVector[LRRNumberRequests]) (ClientPtr);

static struct xorg_list g_interestedClients;

static int LRRErrorBase;
static int LRREventBase;

static int g_width;
static int g_height;
static int g_mmWidth;
static int g_mmHeight;

/******************************************************************************/
static int
remove_client(ClientPtr pClient)
{
    interestedClientRec *iterator;
    interestedClientRec *next;

    xorg_list_for_each_entry_safe(iterator, next, &g_interestedClients, entry)
    {
        if (iterator->pClient == pClient)
        {
            LLOGLN(0, ("remove_client:                      client %p found "
                   "pClient, removing", pClient));
            xorg_list_del(&(iterator->entry));
            free(iterator);
        }
    }
    return 0;
}

/******************************************************************************/
static int
LRRDeliverScreenEvent(interestedClientRec *ic, ScreenPtr pScreen)
{
    xRRScreenChangeNotifyEvent se;
    WindowPtr pRoot;
    WindowPtr pWin;

    LLOGLN(10, ("LRRDeliverScreenEvent:              client %p", ic->pClient));
    if (dixLookupWindow(&pWin, ic->window, ic->pClient,
                        DixGetAttrAccess) != Success)
    {
        return 1;
    }
    memset(&se, 0, sizeof(se));
    pRoot = pScreen->root;
    LLOGLN(10, ("LRRDeliverScreenEvent: root id 0x%8.8x win id 0x%8.8x "
           "width %d height %d",
           pRoot->drawable.id, ic->window,
           pScreen->width, pScreen->height));
    se.type = RRScreenChangeNotify + LRREventBase;
    se.rotation = RR_Rotate_0;
    se.timestamp = g_updateTime;
    se.configTimestamp = g_updateTime;
    se.root = pRoot->drawable.id;
    se.window = ic->window;
    //se.sizeID = RR10CurrentSizeID(pScreen);
    se.widthInPixels = pScreen->width;
    se.heightInPixels = pScreen->height;
    se.widthInMillimeters = pScreen->mmWidth;
    se.heightInMillimeters = pScreen->mmHeight;
    WriteEventsToClient(ic->pClient, 1, (xEvent *) &se);
    return 0;
}

/******************************************************************************/
static int
LRRDeliverCrtcEvent(interestedClientRec *ic, LRRCrtcRec *pCrtc)
{
    xRRCrtcChangeNotifyEvent ce;
    WindowPtr pWin;

    LLOGLN(10, ("LRRDeliverCrtcEvent:                client %p", ic->pClient));
    if (dixLookupWindow(&pWin, ic->window, ic->pClient,
                        DixGetAttrAccess) != Success)
    {
        return 1;
    }
    LLOGLN(10, ("LRRDeliverCrtcEvent: x %d y %d width %d height %d",
           pCrtc->x, pCrtc->y, pCrtc->width, pCrtc->height));
    memset(&ce, 0, sizeof(ce));
    ce.type = RRNotify + LRREventBase;
    ce.subCode = RRNotify_CrtcChange;
    ce.timestamp = g_updateTime;
    ce.window = ic->window;
    ce.crtc = pCrtc->id;
    ce.mode = CRTC2MODE(ce.crtc);
    ce.rotation = RR_Rotate_0;
    ce.x = pCrtc->x;
    ce.y = pCrtc->y;
    ce.width = pCrtc->width;
    ce.height = pCrtc->height;
    WriteEventsToClient(ic->pClient, 1, (xEvent *) &ce);
    return 0;
}

/******************************************************************************/
static int
LRRDeliverOutputEvent(interestedClientRec *ic, LRROutputRec *pOutput)
{
    xRROutputChangeNotifyEvent oe;
    WindowPtr pWin;

    LLOGLN(10, ("LRRDeliverOutputEvent:              client %p", ic->pClient));
    if (dixLookupWindow(&pWin, ic->window, ic->pClient,
                        DixGetAttrAccess) != Success)
    {
        return 1;
    }
    memset(&oe, 0, sizeof(oe));
    oe.type = RRNotify + LRREventBase;
    oe.subCode = RRNotify_OutputChange;
    oe.timestamp = g_updateTime;
    oe.configTimestamp = g_updateTime;
    oe.window = ic->window;
    oe.output = pOutput->id;
    oe.crtc = OUTPUT2CRTC(oe.output);
    oe.mode = OUTPUT2MODE(oe.output);
    oe.rotation = RR_Rotate_0;
    oe.connection = RR_Connected;
    WriteEventsToClient(ic->pClient, 1, (xEvent *) &oe);
    return 0;
}

/******************************************************************************/
/* 0 */
/*  RRQueryVersion
        client-major-version:    CARD32
        client-minor-version:    CARD32
        x
        major-version:           CARD32
        minor-version:           CARD32 */
static int
ProcLRRQueryVersion(ClientPtr client)
{
    xRRQueryVersionReply rep;
    REQUEST(xRRQueryVersionReq);

    REQUEST_SIZE_MATCH(xRRQueryVersionReq);
    LLOGLN(10, ("ProcLRRQueryVersion:                client %p version %d %d",
           client, stuff->majorVersion, stuff->minorVersion));
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    if (version_compare(stuff->majorVersion, stuff->minorVersion,
                        SERVER_LRANDR_MAJOR_VERSION,
                        SERVER_LRANDR_MINOR_VERSION) < 0)
    {
        rep.majorVersion = stuff->majorVersion;
        rep.minorVersion = stuff->minorVersion;
    }
    else
    {
        rep.majorVersion = SERVER_LRANDR_MAJOR_VERSION;
        rep.minorVersion = SERVER_LRANDR_MINOR_VERSION;
    }
    /* require 1.1 or greater randr client */
    if (version_compare(rep.majorVersion, rep.minorVersion, 1, 1) < 0)
    {
        LLOGLN(0, ("ProcLRRQueryVersion: bad version"));
        return BadValue;
    }
    /* don't allow swapping */
    if (client->swapped)
    {
        LLOGLN(0, ("ProcLRRQueryVersion: no swap support"));
        return BadValue;
    }
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 4 */
/*  RRSelectInput
        window: WINDOW
        enable: SETofRRSELECTMASK */
static int
ProcLRRSelectInput(ClientPtr client)
{
    int rc;
    WindowPtr pWin;
    interestedClientRec* ic;
    REQUEST(xRRSelectInputReq);

    LLOGLN(10, ("ProcLRRSelectInput:                 client %p enable 0x%8.8x", client,
           stuff->enable));
    REQUEST_SIZE_MATCH(xRRSelectInputReq);

    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
    {
        return rc;
    }

    if (stuff->enable & (RRScreenChangeNotifyMask |
                         RRCrtcChangeNotifyMask |
                         RROutputChangeNotifyMask |
                         RROutputPropertyNotifyMask))
    {
        ic = (interestedClientRec *) calloc(1, sizeof(interestedClientRec));
        if (ic == NULL)
        {
            return BadAlloc;
        }
        remove_client(client);
        ic->pClient = client;
        ic->mask = stuff->enable;
        ic->window = stuff->window;
        LLOGLN(0, ("ProcLRRSelectInput:                 client %p adding "
               "pClient to list", client));
        xorg_list_add(&(ic->entry), &g_interestedClients);
    }
    else if (stuff->enable == 0)
    {
        /* delete the interest */
        remove_client(client);
    }
    else
    {
        LLOGLN(0, ("ProcLRRSelectInput: bad enable 0x%8.8x", stuff->enable));
        client->errorValue = stuff->enable;
        return BadValue;
    }
    return Success;
}

/******************************************************************************/
/* 5 */
/*  RRGetScreenInfo
        window: WINDOW
        x
        rotations: SETofROTATION
        root: WINDOW
        timestamp: TIMESTAMP
        config-timestamp: TIMESTAMP
        size-id: SIZEID
        rotation: ROTATION
        rate: CARD16
        sizes: LISTofSCREENSIZE
        refresh: LISTofREFRESH */
static int
ProcLRRGetScreenInfo(ClientPtr client)
{
    int rc;
    WindowPtr pWin;
    xRRGetScreenInfoReply rep;
    unsigned long extraLen;
    CARD8 *extra;
    xScreenSizes *size;
    CARD16 *rates;
    REQUEST(xRRGetScreenInfoReq);

    LLOGLN(10, ("ProcLRRGetScreenInfo:               client %p", client));
    REQUEST_SIZE_MATCH(xRRGetScreenInfoReq);
    rc = dixLookupWindow(&pWin, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
    {
        return rc;
    }
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.setOfRotations = RR_Rotate_0;
    rep.sequenceNumber = client->sequence;
    rep.root = pWin->drawable.pScreen->root->drawable.id;
    rep.timestamp = g_updateTime;
    rep.configTimestamp = g_updateTime;
    rep.rotation = RR_Rotate_0;
    rep.nSizes = 1;
    rep.nrateEnts = 1 + 1;
    rep.sizeID = 0;
    rep.rate = 50;
    extraLen = rep.nSizes * sizeof(xScreenSizes);
    extraLen += rep.nrateEnts * sizeof(CARD16);
    extra = (CARD8 *) calloc(extraLen, 1);
    if (extra == NULL)
    {
        return BadAlloc;
    }
    size = (xScreenSizes *) extra;
    rates = (CARD16 *) (size + rep.nSizes);
    size->widthInPixels = g_width;
    size->heightInPixels = g_height;
    size->widthInMillimeters = g_mmWidth;
    size->heightInMillimeters = g_mmHeight;
    size++;
    *rates = 1; /* number of rates */
    rates++;
    *rates = 50;
    rep.length = bytes_to_int32(extraLen);
    WriteToClient(client, sizeof(rep), &rep);
    if (extraLen != 0)
    {
        WriteToClient(client, extraLen, extra);
        free(extra);
    }
    return Success;
}

/******************************************************************************/
/* 6 */
/*  RRGetScreenSizeRange
        window: WINDOW
        x
        CARD16 minWidth, minHeight
        CARD16 maxWidth, maxHeight */
static int
ProcLRRGetScreenSizeRange(ClientPtr client)
{
    xRRGetScreenSizeRangeReply rep;
    REQUEST(xRRGetScreenSizeRangeReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetScreenSizeRange:          client %p", client));
    REQUEST_SIZE_MATCH(xRRGetScreenSizeRangeReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.minWidth = 64;
    rep.minHeight = 64;
    rep.maxWidth = 8192;
    rep.maxHeight = 8192;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 8 */
/*  RRGetScreenResources
        window: WINDOW
        x
        timestamp: TIMESTAMP
        config-timestamp: TIMESTAMP
        crtcs: LISTofCRTC
        outputs: LISTofOUTPUT
        modes: LISTofMODEINFO */
static int
ProcLRRGetScreenResources(ClientPtr client)
{
    int index;
    CARD8 *extra;
    unsigned long extraLen;
    RRCrtc *crtcs;
    RROutput *outputs;
    xRRModeInfo *modeinfos;
    CARD8 *names;
    xRRGetScreenResourcesReply rep;
    REQUEST(xRRGetScreenResourcesReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetScreenResources:          client %p", client));
    REQUEST_SIZE_MATCH(xRRGetScreenResourcesReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.timestamp = g_updateTime;
    rep.configTimestamp = g_updateTime;
    rep.nCrtcs = g_numCrtcs;
    rep.nOutputs = g_numOutputs;
    rep.nModes = g_numModes;
    for (index = 0; index < g_numModes; index++)
    {
        rep.nbytesNames += g_modes[index].nameLength;
    }
    LLOGLN(10, ("ProcLRRGetScreenResources: rep.nbytesNames %d",
           rep.nbytesNames));
    rep.length = (g_numCrtcs + g_numOutputs +
                  g_numModes * bytes_to_int32(SIZEOF(xRRModeInfo)) +
                  bytes_to_int32(rep.nbytesNames));
    LLOGLN(10, ("ProcLRRGetScreenResources: rep.length %d", rep.length));
    extraLen = rep.length << 2;
    if (extraLen != 0)
    {
        extra = (CARD8 *) calloc(1, extraLen);
        if (extra == NULL)
        {
            return BadAlloc;
        }
    }
    else
    {
        extra = NULL;
    }
    LLOGLN(10, ("ProcLRRGetScreenResources: extraLen %d", (int) extraLen));
    crtcs = (RRCrtc *) extra;
    outputs = (RROutput *) (crtcs + g_numCrtcs);
    modeinfos = (xRRModeInfo *) (outputs + g_numOutputs);
    names = (CARD8 *) (modeinfos + g_numModes);
    for (index = 0; index < g_numCrtcs; index++)
    {
        crtcs[index] = g_crtcs[index].id;
    }
    for (index = 0; index < g_numOutputs; index++)
    {
        outputs[index] = g_outputs[index].id;
    }
    for (index = 0; index < g_numModes; index++)
    {
        modeinfos[index] = g_modes[index];
        memcpy(names, g_modeNames[index], g_modes[index].nameLength);
        names += g_modes[index].nameLength;
    }
    WriteToClient(client, sizeof(rep), &rep);
    if (extraLen != 0)
    {
        WriteToClient(client, extraLen, extra);
        free(extra);
    }
    return Success;
}

#define OutputInfoExtra (SIZEOF(xRRGetOutputInfoReply) - 32)

/******************************************************************************/
/* 9 */
/*  RRGetOutputInfo
        output: OUTPUT
        config-timestamp: TIMESTAMP
        x
        status: RRCONFIGSTATUS
        timestamp: TIMESTAMP
        crtc: CRTC
        name: STRING
        connection: CONNECTION
        subpixel-order: SUBPIXELORDER
        widthInMillimeters, heightInMillimeters: CARD32
        crtcs: LISTofCRTC
        clones: LISTofOUTPUT
        modes: LISTofMODE
        num-preferred: CARD16 */
int
ProcLRRGetOutputInfo(ClientPtr client)
{
    int index;
    CARD8 *extra;
    unsigned long extraLen;
    LRROutputRec *output;
    RRCrtc *crtcs;
    RRMode *modes;
    char *name;
    xRRGetOutputInfoReply rep;
    REQUEST(xRRGetOutputInfoReq);

    LLOGLN(10, ("ProcLRRGetOutputInfo:               client %p", client));
    REQUEST_SIZE_MATCH(xRRGetOutputInfoReq);

    if ((stuff->output < LRROutputStart) ||
        (stuff->output >= LRROutputStart + LRRMaxOutputs))
    {
        return BadRequest;
    }
    output = g_outputs + (stuff->output - LRROutputStart);

    memset(&rep, 0, sizeof(rep));
    LLOGLN(10, ("ProcLRRGetOutputInfo: stuff->output %d", stuff->output));
    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    rep.length = bytes_to_int32(OutputInfoExtra);
    rep.timestamp = g_updateTime;
    rep.crtc = OUTPUT2CRTC(stuff->output);
    rep.mmWidth = 0;
    rep.mmHeight = 0;
    rep.connection = RR_Connected;
    rep.subpixelOrder = SubPixelUnknown;
    rep.nCrtcs = 1;
    rep.nModes = 1;
    rep.nPreferred = 1;
    rep.nClones = 0;
    rep.nameLength = strlen(output->name);
    extraLen = (rep.nCrtcs + rep.nModes + bytes_to_int32(rep.nameLength)) << 2;
    if (extraLen != 0)
    {
        rep.length += bytes_to_int32(extraLen);
        extra = calloc(1, extraLen);
        if (extra == NULL)
        {
            return BadAlloc;
        }
    }
    else
    {
        extra = NULL;
    }
    crtcs = (RRCrtc *) extra;
    modes = (RRMode *) (crtcs + rep.nCrtcs);
    name = (char *) (modes + rep.nModes);
    for (index = 0; index < rep.nCrtcs; index++)
    {
        crtcs[index] = OUTPUT2CRTC(stuff->output);
    }
    for (index = 0; index < rep.nModes; index++)
    {
        modes[index] = OUTPUT2MODE(stuff->output);
    }
    memcpy(name, output->name, rep.nameLength);
    WriteToClient(client, sizeof(rep), &rep);
    if (extraLen != 0)
    {
        WriteToClient(client, extraLen, extra);
        free(extra);
    }
    return Success;

}

/******************************************************************************/
/* 10 */
/*  RRListOutputProperties
        output:OUTPUT
        x
        atoms: LISTofATOM */
static int
ProcLRRListOutputProperties(ClientPtr client)
{
    xRRListOutputPropertiesReply rep;
    REQUEST(xRRListOutputPropertiesReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRListOutputProperties:        client %p", client));
    REQUEST_SIZE_MATCH(xRRListOutputPropertiesReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 11 */
/*  RRQueryOutputProperty
        output: OUTPUT
        property: ATOM
        x
        pending: BOOL
        range: BOOL
        immutable: BOOL
        valid-values: LISTofINT32 */
static int
ProcLRRQueryOutputProperty(ClientPtr client)
{
    xRRQueryOutputPropertyReply rep;

    LLOGLN(10, ("ProcLRRQueryOutputProperty:         client %p", client));
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 15 */
/*  RRGetOutputProperty
        output: OUTPUT
        property: ATOM
        type: ATOM or AnyPropertyType
        long-offset, long-length: CARD32
        delete: BOOL
        pending: BOOL
        x
        type: ATOM or None
        format: {0, 8, 16, 32}
        bytes-after: CARD32
        value: LISTofINT8 or LISTofINT16 or LISTofINT32 */
static int
ProcLRRGetOutputProperty(ClientPtr client)
{
    xRRGetOutputPropertyReply rep;
    REQUEST(xRRGetOutputPropertyReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetOutputProperty:           client %p", client));
    REQUEST_SIZE_MATCH(xRRGetOutputPropertyReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 20 */
/*  RRGetCrtcInfo
        crtc: CRTC
        config-timestamp: TIMESTAMP
        x
        status: RRCONFIGSTATUS
        timestamp: TIMESTAMP
        x, y: INT16
        width, height: CARD16
        mode: MODE
        rotation: ROTATION
        outputs: LISTofOUTPUT
        rotations: SETofROTATION
        possible-outputs: LISTofOUTPUT */
int
ProcLRRGetCrtcInfo(ClientPtr client)
{
    RROutput output;
    LRRCrtcRec *crtc;
    xRRGetCrtcInfoReply rep;
    REQUEST(xRRGetCrtcInfoReq);

    LLOGLN(10, ("ProcLRRGetCrtcInfo:                 client %p crtc %d", client, stuff->crtc));
    REQUEST_SIZE_MATCH(xRRGetCrtcInfoReq);

    if ((stuff->crtc < LRRCrtcStart) ||
        (stuff->crtc >= LRRCrtcStart + LRRMaxCrtcs))
    {
        return BadRequest;
    }
    crtc = g_crtcs + (stuff->crtc - LRRCrtcStart);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    rep.timestamp = g_updateTime;
    rep.x = crtc->x;
    rep.y = crtc->y;
    rep.width = crtc->width;
    rep.height = crtc->height;
    rep.mode = CRTC2MODE(stuff->crtc);
    rep.nOutput = 1;
    rep.nPossibleOutput = 1;
    rep.length = rep.nOutput + rep.nPossibleOutput;
    rep.rotation = RR_Rotate_0;
    rep.rotations = RR_Rotate_0;
    output = CRTC2OUTPUT(stuff->crtc);
    WriteToClient(client, sizeof(rep), &rep);
    WriteToClient(client, sizeof(output), &output);
    WriteToClient(client, sizeof(output), &output);
    return 0;
}

/******************************************************************************/
/* 21 */
/*  RRSetCrtcConfig
        crtc: CRTC
        timestamp: TIMESTAMP
        config-timestamp: TIMESTAMP
        x, y: INT16
        mode: MODE
        rotation: ROTATION
        outputs: LISTofOUTPUT
        x
        status: RRCONFIGSTATUS
        new-timestamp: TIMESTAMP */
int
ProcLRRSetCrtcConfig(ClientPtr client)
{
    xRRSetCrtcConfigReply rep;
    REQUEST(xRRSetCrtcConfigReq);
    TimeStamp ts;

    (void) stuff;

    LLOGLN(10, ("ProcLRRSetCrtcConfig:               client %p", client));
    REQUEST_SIZE_MATCH(xRRSetCrtcConfigReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    ts = ClientTimeToServerTime(stuff->timestamp);
    g_updateTime = ts.milliseconds;
    rep.newTimestamp = g_updateTime;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 22 */
/*  RRGetCrtcGammaSize
        crtc: CRTC
        x
        size: CARD16 */
int
ProcLRRGetCrtcGammaSize(ClientPtr client)
{
    xRRGetCrtcGammaSizeReply rep;
    REQUEST(xRRGetCrtcGammaSizeReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetCrtcGammaSize:            client %p", client));
    REQUEST_SIZE_MATCH(xRRGetCrtcGammaSizeReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.size = 256;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 23 */
/*  RRGetCrtcGamma
        crtc: CRTC
        x
        red: LISTofCARD16
        green: LISTofCARD16
        blue: LISTofCARD16 */
int
ProcLRRGetCrtcGamma(ClientPtr client)
{
    xRRGetCrtcGammaReply rep;
    unsigned long len;
    unsigned short *vals;
    unsigned short val;
    char *extra;
    int index;
    REQUEST(xRRGetCrtcGammaReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetCrtcGamma:                client %p", client));
    REQUEST_SIZE_MATCH(xRRGetCrtcGammaReq);
    len = 256 * 3 * 2;
    extra = (char *) malloc(len);
    if (extra == NULL)
    {
        return BadAlloc;
    }
    vals = (unsigned short *) extra;
    /* red */
    for (index = 0; index < 256; index++)
    {
        val = (0xffff * index) / 255;
        vals[0] = val;
        vals += 1;
    }
    /* green */
    for (index = 0; index < 256; index++)
    {
        val = (0xffff * index) / 255;
        vals[0] = val;
        vals += 1;
    }
    /* blue */
    for (index = 0; index < 256; index++)
    {
        val = (0xffff * index) / 255;
        vals[0] = val;
        vals += 1;
    }
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.length = bytes_to_int32(len);
    rep.size = 256;
    WriteToClient(client, sizeof(rep), &rep);
    WriteToClient(client, len, extra);
    return Success;
}

/******************************************************************************/
/* 25 */
/*  RRGetScreenResourcesCurrent
        window: WINDOW
        x
        timestamp: TIMESTAMP
        config-timestamp: TIMESTAMP
        crtcs: LISTofCRTC
        outputs: LISTofOUTPUT
        modes: LISTofMODEINFO */
int
ProcLRRGetScreenResourcesCurrent(ClientPtr client)
{
    LLOGLN(10, ("ProcLRRGetScreenResourcesCurrent:   client %p", client));
    return ProcLRRGetScreenResources(client);
}

#define CrtcTransformExtra  (SIZEOF(xRRGetCrtcTransformReply) - 32)
#define ToFixed(f) ((int) ((f) * 65536))

/******************************************************************************/
/* 27 */
/*  RRGetCrtcTransform
        crtc: CRTC
        x
        pending-transform: TRANSFORM
        pending-filter: STRING8
        pending-values: LISTofFIXED
        current-transform: TRANSFORM
        current-filter: STRING8
        current-values: LISTofFIXED */
int
ProcLRRGetCrtcTransform(ClientPtr client)
{
    xRRGetCrtcTransformReply rep;
    REQUEST(xRRGetCrtcTransformReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetCrtcTransform:            client %p", client));
    REQUEST_SIZE_MATCH(xRRGetPanningReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.currentTransform.matrix11 = ToFixed(1);
    rep.currentTransform.matrix22 = ToFixed(1);
    rep.currentTransform.matrix33 = ToFixed(1);
    rep.length = bytes_to_int32(CrtcTransformExtra);
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 28 */
/*  RRGetPanning
        crtc: CRTC
        x
        status: RRCONFIGSTATUS
        timestamp: TIMESTAMP
        left, top, width, height: CARD16
        track_left, track_top, track_width, track_height: CARD16
        border_left, border_top, border_right, border_bottom: INT16 */
int
ProcLRRGetPanning(ClientPtr client)
{
    xRRGetPanningReply rep;
    REQUEST(xRRGetPanningReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetPanning:                  client %p", client));
    REQUEST_SIZE_MATCH(xRRGetPanningReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    rep.length = 1,
    rep.timestamp = g_updateTime;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
/* 31 */
/*  RRGetOutputPrimary
        window: WINDOW
        x
        output: OUTPUT */
int
ProcLRRGetOutputPrimary(ClientPtr client)
{
    xRRGetOutputPrimaryReply rep;
    REQUEST(xRRGetOutputPrimaryReq);

    (void) stuff;

    LLOGLN(10, ("ProcLRRGetOutputPrimary:            client %p", client));
    REQUEST_SIZE_MATCH(xRRGetOutputPrimaryReq);
    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.output = g_primaryOutput;
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

/******************************************************************************/
static int
ProcLRRDispatch(ClientPtr client)
{
    REQUEST(xReq);

    LLOGLN(10, ("ProcLRRDispatch: data %d", stuff->data));
    if (stuff->data >= LRRNumberRequests)
    {
        LLOGLN(0, ("ProcLRRDispatch: returning BadRequest, data %d",
               stuff->data));
        return BadRequest;
    }
    if (g_procLRandrVector[stuff->data] == NULL)
    {
        LLOGLN(0, ("ProcLRRDispatch: returning Success, data %d",
               stuff->data));
        return Success;
    }
    return g_procLRandrVector[stuff->data](client);
}

/******************************************************************************/
static int
SProcLRRDispatch(ClientPtr client)
{
    LLOGLN(0, ("SProcLRRDispatch:"));
    return 0;
}

/******************************************************************************/
static void
LRRClientCallback(CallbackListPtr *list, void *closure, void *data)
{
    NewClientInfoRec *clientinfo;
    ClientPtr pClient;

    LLOGLN(10, ("LRRClientCallback: list %p closure %p data %p",
           list, closure, data));
    if (data != NULL)
    {
        clientinfo = (NewClientInfoRec *) data;
        if (clientinfo->client != NULL)
        {
            pClient = clientinfo->client;
            LLOGLN(10, ("LRRClientCallback: clientState %d clientGone %d",
                   pClient->clientState, pClient->clientGone));
            if (pClient->clientGone ||
                (pClient->clientState == ClientStateRetained) ||
                (pClient->clientState == ClientStateGone))
            {
                LLOGLN(10, ("LRRClientCallback: client gone"));
                remove_client(pClient);
            }
        }
    }
}

/******************************************************************************/
int
rdpLRRInit(rdpPtr dev)
{
    ExtensionEntry *extEntry;
    int index;

    LLOGLN(10, ("rdpLRRInit:"));
    if (!AddCallback(&ClientStateCallback, LRRClientCallback, 0))
    {
        LLOGLN(0, ("rdpLRRInit: AddCallback failed"));
        return 1;
    }
    LLOGLN(0, ("rdpLRRInit: AddCallback ok"));

    extEntry = AddExtension(LRANDR_NAME,
                            LRRNumberEvents, LRRNumberErrors,
                            ProcLRRDispatch, SProcLRRDispatch,
                            NULL, StandardMinorOpcode);
    if (extEntry == NULL)
    {
        LLOGLN(0, ("rdpLRRInit: AddExtension failed"));
        return 1;
    }
    LLOGLN(0, ("rdpLRRInit: AddExtension ok"));

    LRRErrorBase = extEntry->errorBase;
    LRREventBase = extEntry->eventBase;

    for (index = 0; index < LRRMaxCrtcs; index++)
    {
        g_crtcs[index].id = index + LRRCrtcStart;
    }

    for (index = 0; index < LRRMaxOutputs; index++)
    {
        g_outputs[index].id = index + LRROutputStart;
        snprintf(g_outputs[index].name, LRRMaxOutputNameLength,
                 "rdp%d", index);
    }

    for (index = 0; index < LRRMaxModes; index++)
    {
        g_modes[index].id = index + LRRModeStart;
    }

    xorg_list_init(&g_interestedClients);

    memset(g_procLRandrVector, 0, sizeof(g_procLRandrVector));
    g_procLRandrVector[0] = ProcLRRQueryVersion;
    //g_procLRandrVector[2] = ProcLRRSetScreenConfig; TODO
    g_procLRandrVector[4] = ProcLRRSelectInput;
    g_procLRandrVector[5] = ProcLRRGetScreenInfo;
    /* V1.2 additions */
    g_procLRandrVector[6] = ProcLRRGetScreenSizeRange;
    //g_procLRandrVector[7] = ProcLRRSetScreenSize; ok
    g_procLRandrVector[8] = ProcLRRGetScreenResources;
    g_procLRandrVector[9] = ProcLRRGetOutputInfo;
    g_procLRandrVector[10] = ProcLRRListOutputProperties;
    g_procLRandrVector[11] = ProcLRRQueryOutputProperty;
    //g_procLRandrVector[12] = ProcLRRConfigureOutputProperty; ok
    //g_procLRandrVector[13] = ProcLRRChangeOutputProperty; ok
    //g_procLRandrVector[14] = ProcLRRDeleteOutputProperty; ok
    g_procLRandrVector[15] = ProcLRRGetOutputProperty;
    //g_procLRandrVector[16] = ProcLRRCreateMode; ok
    //g_procLRandrVector[17] = ProcLRRDestroyMode; ok
    //g_procLRandrVector[18] = ProcLRRAddOutputMode; ok
    //g_procLRandrVector[19] = ProcLRRDeleteOutputMode; ok
    g_procLRandrVector[20] = ProcLRRGetCrtcInfo;
    g_procLRandrVector[21] = ProcLRRSetCrtcConfig;
    g_procLRandrVector[22] = ProcLRRGetCrtcGammaSize;
    g_procLRandrVector[23] = ProcLRRGetCrtcGamma;
    //g_procLRandrVector[24] = ProcLRRSetCrtcGamma; ok
    /* V1.3 additions */
    g_procLRandrVector[25] = ProcLRRGetScreenResourcesCurrent;
    //g_procLRandrVector[26] = ProcLRRSetCrtcTransform; ok
    g_procLRandrVector[27] = ProcLRRGetCrtcTransform;
    g_procLRandrVector[28] = ProcLRRGetPanning;
    //g_procLRandrVector[29] = ProcLRRSetPanning; TODO
    //g_procLRandrVector[30] = ProcLRRSetOutputPrimary; ok
    g_procLRandrVector[31] = ProcLRRGetOutputPrimary;
    return 0;
}

#if defined(XORGXRDP_GLAMOR)
/*****************************************************************************/
static int
rdpLRRSetPixmapVisitWindow(WindowPtr window, void *data)
{
    ScreenPtr screen;

    LLOGLN(10, ("rdpLRRSetPixmapVisitWindow:"));
    screen = window->drawable.pScreen;
    if (screen->GetWindowPixmap(window) == data)
    {
        screen->SetWindowPixmap(window, screen->GetScreenPixmap(screen));
        return WT_WALKCHILDREN;
    }
    return WT_DONTWALKCHILDREN;
}
#endif

/*
 * Edit connection information block so that new clients
 * see the current screen size on connect
 */
/* from rrscreen.c */
static void
LRREditConnectionInfo(ScreenPtr pScreen)
{
    xConnSetup *connSetup;
    char *vendor;
    xPixmapFormat *formats;
    xWindowRoot *root;
    xDepth *depth;
    xVisualType *visual;
    int screen = 0;
    int d;

    if (ConnectionInfo == NULL)
        return;

    connSetup = (xConnSetup *) ConnectionInfo;
    vendor = (char *) connSetup + sizeof(xConnSetup);
    formats = (xPixmapFormat *) ((char *) vendor +
                                 pad_to_int32(connSetup->nbytesVendor));
    root = (xWindowRoot *) ((char *) formats +
                            sizeof(xPixmapFormat) *
                            screenInfo.numPixmapFormats);
    while (screen != pScreen->myNum) {
        depth = (xDepth *) ((char *) root + sizeof(xWindowRoot));
        for (d = 0; d < root->nDepths; d++) {
            visual = (xVisualType *) ((char *) depth + sizeof(xDepth));
            depth = (xDepth *) ((char *) visual +
                                depth->nVisuals * sizeof(xVisualType));
        }
        root = (xWindowRoot *) ((char *) depth);
        screen++;
    }
    root->pixWidth = pScreen->width;
    root->pixHeight = pScreen->height;
    root->mmWidth = pScreen->mmWidth;
    root->mmHeight = pScreen->mmHeight;
}

/******************************************************************************/
static void
LRRSendConfigNotify(ScreenPtr pScreen)
{
    WindowPtr pWin;
    xEvent event;

    pWin = pScreen->root;
    memset(&event, 0, sizeof(event));
    event.u.configureNotify.window = pWin->drawable.id;
    event.u.configureNotify.width = pWin->drawable.width;
    event.u.configureNotify.height = pWin->drawable.height;
    event.u.configureNotify.borderWidth = wBorderWidth(pWin);
    event.u.configureNotify.override = pWin->overrideRedirect;
    event.u.u.type = ConfigureNotify;
    DeliverEvents(pWin, &event, 1, NullWindow);
}

/******************************************************************************/
Bool
rdpLRRScreenSizeSet(rdpPtr dev, int width, int height,
                    int mmWidth, int mmHeight)
{
    WindowPtr root;
    PixmapPtr screenPixmap;
    BoxRec box;
    ScreenPtr pScreen;

    LLOGLN(10, ("rdpLRRScreenSizeSet: width %d height %d mmWidth %d mmHeight %d",
           width, height, mmWidth, mmHeight));
    pScreen = dev->pScreen;
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

    g_width = width;
    g_height = height;
    g_mmWidth = mmWidth;
    g_mmHeight = mmHeight;

    screenPixmap = dev->screenSwPixmap;
    free(dev->pfbMemory_alloc);
    dev->pfbMemory_alloc = g_new0(uint8_t, dev->sizeInBytes + 16);
    dev->pfbMemory = (uint8_t *) RDPALIGN(dev->pfbMemory_alloc, 16);
    pScreen->ModifyPixmapHeader(screenPixmap, width, height,
                                -1, -1,
                                dev->paddedWidthInBytes,
                                dev->pfbMemory);
    if (dev->glamor)
    {
#if defined(XORGXRDP_GLAMOR)
        PixmapPtr old_screen_pixmap;
        uint32_t screen_tex;
        old_screen_pixmap = pScreen->GetScreenPixmap(pScreen);
        screenPixmap = pScreen->CreatePixmap(pScreen,
                                             pScreen->width,
                                             pScreen->height,
                                             pScreen->rootDepth,
                                             GLAMOR_CREATE_NO_LARGE);
        if (screenPixmap == NULL)
        {
            return FALSE;
        }
        screen_tex = glamor_get_pixmap_texture(screenPixmap);
        LLOGLN(0, ("rdpLRRScreenSizeSet: screen_tex 0x%8.8x", screen_tex));
        pScreen->SetScreenPixmap(screenPixmap);
        if ((pScreen->root != NULL) && (pScreen->SetWindowPixmap != NULL))
        {
            TraverseTree(pScreen->root, rdpLRRSetPixmapVisitWindow, old_screen_pixmap);
        }
        pScreen->DestroyPixmap(old_screen_pixmap);
#endif
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
    LLOGLN(0, ("  screen resized to %dx%d", pScreen->width, pScreen->height));
    LRREditConnectionInfo(pScreen);
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
rdpLRRSetRdpOutputs(rdpPtr dev)
{
    interestedClientRec *iterator;
    interestedClientRec *next;
    char modeName[LRRMaxModesNameLength];
    int width;
    int height;
    int index;
    int count;
    int cont;

    LLOGLN(10, ("rdpLRRSetRdpOutputs: numCrtcs %d numOutputs %d "
           "monitorCount %d",
           g_numCrtcs, g_numOutputs, dev->monitorCount));
    LRRSendConfigNotify(dev->pScreen);
    g_primaryOutput = None;
    width = dev->width;
    height = dev->height;
    LLOGLN(10, ("rdpLRRSetRdpOutputs: width %d height %d", width, height));
    if (dev->monitorCount <= 0)
    {
        g_numCrtcs = 1;
        g_crtcs[0].x = 0;
        g_crtcs[0].y = 0;
        g_crtcs[0].width = width;
        g_crtcs[0].height = height;
        g_numOutputs = 1;
        g_numModes = 1;
        g_modes[0].width = width;
        g_modes[0].height = height;
        g_modes[0].hTotal = width;
        g_modes[0].vTotal = height;
        g_modes[0].dotClock = 50 * width * height;
        snprintf(modeName, LRRMaxModesNameLength, "%dx%d", width, height);
        g_modes[0].nameLength = strlen(modeName);
        memcpy(g_modeNames[0], modeName, g_modes[0].nameLength);
    }
    else
    {
        count = dev->monitorCount;
        if (count > 16)
        {
            count = 16;
        }
        g_numCrtcs = count;
        g_numOutputs = count;
        g_numModes = count;
        for (index = 0; index < count; index++)
        {
            g_crtcs[index].x = dev->minfo[index].left;
            g_crtcs[index].y = dev->minfo[index].top;
            width = dev->minfo[index].right - dev->minfo[index].left;
            height = dev->minfo[index].bottom - dev->minfo[index].top;
            g_crtcs[index].width = width;
            g_crtcs[index].height = height;
            g_modes[index].width = width;
            g_modes[index].height = height;
            g_modes[index].hTotal = width;
            g_modes[index].vTotal = height;
            g_modes[index].dotClock = 50 * width * height;
            snprintf(modeName, LRRMaxModesNameLength, "%dx%d", width, height);
            g_modes[index].nameLength = strlen(modeName);
            memcpy(g_modeNames[index], modeName, g_modes[index].nameLength);
            if (dev->minfo[index].is_primary)
            {
                g_primaryOutput = g_outputs[index].id;
            }
        }
    }
    g_updateTime = GetTimeInMillis();
    xorg_list_for_each_entry_safe(iterator, next, &g_interestedClients, entry)
    {
        cont = 0;
        LLOGLN(10, ("rdpLRRSetRdpOutputs:                client %p",
               iterator->pClient));
        if (iterator->mask & RRScreenChangeNotifyMask)
        {
            if (LRRDeliverScreenEvent(iterator, dev->pScreen) != 0)
            {
                LLOGLN(0, ("rdpLRRSetRdpOutputs: error removing from "
                       "interested list"));
                xorg_list_del(&(iterator->entry));
                free(iterator);
                continue;
            }
        }
        if (iterator->mask & RRCrtcChangeNotifyMask)
        {
            for (index = 0; index < g_numCrtcs; index++)
            {
                if (LRRDeliverCrtcEvent(iterator, g_crtcs + index) != 0)
                {
                    LLOGLN(0, ("rdpLRRSetRdpOutputs: error removing from "
                           "interested list"));
                    xorg_list_del(&(iterator->entry));
                    free(iterator);
                    cont = 1;
                    break;
                }
            }
            if (cont)
            {
                continue;
            }
        }
        if (iterator->mask & RROutputChangeNotifyMask)
        {
            for (index = 0; index < g_numOutputs; index++)
            {
                if (LRRDeliverOutputEvent(iterator, g_outputs + index) != 0)
                {
                    LLOGLN(0, ("rdpLRRSetRdpOutputs: error removing from "
                           "interested list"));
                    xorg_list_del(&(iterator->entry));
                    free(iterator);
                    cont = 1;
                    break;
                }
            }
            if (cont)
            {
                continue;
            }
        }
    }
    return TRUE;
}

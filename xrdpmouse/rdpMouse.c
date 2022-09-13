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

xrdp mouse module

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

#include <xf86Xinput.h>

#include <mipointer.h>
#include <fb.h>
#include <micmap.h>
#include <mi.h>
#include <exevents.h>
#include <xserver-properties.h>

#include "rdp.h"
#include "rdpInput.h"
#include "rdpDraw.h"

#define NBUTTONS 9
#define NAXES 4

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

static char g_Mouse_str[] = "Mouse";
static char g_xrdp_mouse_name[] = XRDP_MOUSE_NAME;

/******************************************************************************/
static void
rdpmouseDeviceInit(void)
{
    LLOGLN(0, ("rdpmouseDeviceInit:"));
}

/******************************************************************************/
static void
rdpmouseDeviceOn(DeviceIntPtr pDev)
{
    LLOGLN(0, ("rdpmouseDeviceOn:"));
}

/******************************************************************************/
static void
rdpmouseDeviceOff(void)
{
    LLOGLN(0, ("rdpmouseDeviceOff:"));
}

/******************************************************************************/
static void
rdpmouseCtrl(DeviceIntPtr pDevice, PtrCtrl *pCtrl)
{
    LLOGLN(0, ("rdpmouseCtrl:"));
}

/******************************************************************************/
static int
l_bound_by(int val, int low, int high)
{
    val = RDPCLAMP(val, low, high);
    return val;
}

/******************************************************************************/
static void
rdpEnqueueMotion(DeviceIntPtr device, int x, int y)
{
    LLOGLN(10, ("rdpEnqueueMotion:"));
    xf86PostMotionEvent(device, TRUE, 0, 2, x, y);
}

/******************************************************************************/
static void
rdpEnqueueButton(DeviceIntPtr device, int type, int buttons)
{
    LLOGLN(10, ("rdpEnqueueButton:"));
    xf86PostButtonEvent(device, FALSE, buttons, type == ButtonPress, 0, 0);
}

/******************************************************************************/
static void
PtrAddEvent(rdpPointer *pointer)
{
    int i;
    int type;
    int buttons;

    LLOGLN(10, ("PtrAddEvent: x %d y %d", pointer->cursor_x, pointer->cursor_y));

    if ((pointer->old_cursor_x != pointer->cursor_x) ||
            (pointer->old_cursor_y != pointer->cursor_y))
    {
        rdpEnqueueMotion(pointer->device, pointer->cursor_x, pointer->cursor_y);
        pointer->old_cursor_x = pointer->cursor_x;
        pointer->old_cursor_y = pointer->cursor_y;
    }

    for (i = 0; i < NBUTTONS; i++)
    {
        if ((pointer->button_mask ^ pointer->old_button_mask) & (1 << i))
        {
            if (pointer->button_mask & (1 << i))
            {
                type = ButtonPress;
                buttons = i + 1;
                rdpEnqueueButton(pointer->device, type, buttons);
            }
            else
            {
                type = ButtonRelease;
                buttons = i + 1;
                rdpEnqueueButton(pointer->device, type, buttons);
            }
        }
    }

    pointer->old_button_mask = pointer->button_mask;
}

/******************************************************************************/
// Maybe make it configurable later
#define SCALE_FACTOR 10

static void
PtrAddScrollEvent(rdpPointer *pointer, int vertical, int delta)
{
    ValuatorMask *scroll_events_mask;
    int mask_pos;
    int scaled_delta;

    LLOGLN(10, ("PtrAddScrollEvent: vertical %d y %d", vertical, delta));

    scroll_events_mask = valuator_mask_new(NAXES);
    mask_pos = vertical ? 2 : 3;
    scaled_delta = delta / SCALE_FACTOR == 0 ? delta > 0 ? 1 : -1 : delta / SCALE_FACTOR;

    // XWindow's and RDP's scrolling directions are exactly opposite
    // on vertical(Need document references).
    if (vertical)
    {
        scaled_delta = -scaled_delta;
    }

    valuator_mask_zero(scroll_events_mask);
    valuator_mask_set_double(scroll_events_mask, mask_pos, scaled_delta);

    xf86PostMotionEventM(pointer->device, FALSE, scroll_events_mask);

    valuator_mask_free(&scroll_events_mask);

    pointer->old_button_mask = pointer->button_mask;
}

/******************************************************************************/
static int
rdpInputMouse(rdpPtr dev, int msg,
              long param1, long param2,
              long param3, long param4)
{
    rdpPointer *pointer;

    LLOGLN(10, ("rdpInputMouse: msg %d param1 %ld param2 %ld param3 %ld param4 %ld",
                msg, param1, param2, param3, param4));
    pointer = &(dev->pointer);
    switch (msg)
    {
        case WM_MOUSEMOVE:
            /* without the minus 2, strange things happen when dragging
               past the width or height */
            pointer->cursor_x = l_bound_by(param1, 0, dev->width - 2);
            pointer->cursor_y = l_bound_by(param2, 0, dev->height - 2);
            PtrAddEvent(pointer);
            break;
        case WM_LBUTTONUP:
            pointer->button_mask = pointer->button_mask & (~1);
            PtrAddEvent(pointer);
            break;
        case WM_LBUTTONDOWN:
            pointer->button_mask = pointer->button_mask | 1;
            PtrAddEvent(pointer);
            break;
        case WM_RBUTTONUP:
            pointer->button_mask = pointer->button_mask & (~4);
            PtrAddEvent(pointer);
            break;
        case WM_RBUTTONDOWN:
            pointer->button_mask = pointer->button_mask | 4;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON3UP:
            pointer->button_mask = pointer->button_mask & (~2);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON3DOWN:
            pointer->button_mask = pointer->button_mask | 2;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON4UP:
            pointer->button_mask = pointer->button_mask & (~8);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON4DOWN:
            pointer->button_mask = pointer->button_mask | 8;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON5UP:
            pointer->button_mask = pointer->button_mask & (~16);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON5DOWN:
            pointer->button_mask = pointer->button_mask | 16;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON6UP:
            pointer->button_mask = pointer->button_mask & (~32);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON6DOWN:
            pointer->button_mask = pointer->button_mask | 32;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON7UP:
            pointer->button_mask = pointer->button_mask & (~64);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON7DOWN:
            pointer->button_mask = pointer->button_mask | 64;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON8UP:
            pointer->button_mask = pointer->button_mask & (~128);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON8DOWN:
            pointer->button_mask = pointer->button_mask | 128;
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON9UP:
            pointer->button_mask = pointer->button_mask & (~256);
            PtrAddEvent(pointer);
            break;
        case WM_BUTTON9DOWN:
            pointer->button_mask = pointer->button_mask | 256;
            PtrAddEvent(pointer);
            break;
        case WM_TOUCH_VSCROLL:
            PtrAddScrollEvent(pointer, TRUE, param3);
            break;
        case WM_TOUCH_HSCROLL:
            PtrAddScrollEvent(pointer, FALSE, param3);
            break;
    }
    return 0;
}

/******************************************************************************/
static int
rdpmouseControl(DeviceIntPtr device, int what)
{
    BYTE map[NBUTTONS + 1]; /* Indexed from 1 */
    DevicePtr pDev;
    Atom btn_labels[NBUTTONS];
    Atom axes_labels[NAXES];
    rdpPtr dev;
    int i;

    LLOGLN(0, ("rdpmouseControl: what %d", what));
    pDev = (DevicePtr)device;

    switch (what)
    {
        case DEVICE_INIT:
            rdpmouseDeviceInit();
            for (i = 0 ; i <= NBUTTONS; ++i) /* element 0 not used */
            {
                map[i] = i;
            }

            btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
            btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
            btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
            btn_labels[3] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_UP);
            btn_labels[4] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_WHEEL_DOWN);
            btn_labels[5] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_LEFT);
            btn_labels[6] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_HWHEEL_RIGHT);
            btn_labels[7] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_BACK);
            btn_labels[8] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_FORWARD);

            axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_X);
            axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_Y);
            axes_labels[2] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_VSCROLL);
            axes_labels[3] = XIGetKnownProperty(AXIS_LABEL_PROP_REL_HSCROLL);

            InitPointerDeviceStruct(pDev, map, NBUTTONS, btn_labels, rdpmouseCtrl,
                                    GetMotionHistorySize(), NAXES, axes_labels);

            dev = rdpGetDevFromScreen(NULL);
            dev->pointer.device = device;

            // Initialize scroll valuators
            xf86InitValuatorAxisStruct(device, 2, axes_labels[2]
                                       , 0, -1, 0, 0, 0, Relative);
            xf86InitValuatorAxisStruct(device, 3, axes_labels[3]
                                       , 0, -1, 0, 0, 0, Relative);

            SetScrollValuator(device, 2, SCROLL_TYPE_VERTICAL, 10, 0);
            SetScrollValuator(device, 3, SCROLL_TYPE_HORIZONTAL, 10, 0);

            rdpRegisterInputCallback(1, rdpInputMouse);
            break;
        case DEVICE_ON:
            pDev->on = 1;
            rdpmouseDeviceOn(device);
            break;
        case DEVICE_OFF:
            pDev->on = 0;
            rdpmouseDeviceOff();
            break;
        case DEVICE_CLOSE:

            if (pDev->on)
            {
                rdpmouseDeviceOff();
            }

            break;
    }

    return Success;
#undef NBUTTONS
}

#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 9, 0, 1, 0)

/* debian 6
   ubuntu 10.04 */

/******************************************************************************/
static InputInfoPtr
rdpmousePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr info;

    LLOGLN(0, ("rdpmousePreInit: drv %p dev %p, flags 0x%x",
               drv, dev, flags));
    info = xf86AllocateInput(drv, 0);
    info->name = dev->identifier;
    info->device_control = rdpmouseControl;
    info->flags = XI86_CONFIGURED | XI86_ALWAYS_CORE | XI86_SEND_DRAG_EVENTS |
                  XI86_CORE_POINTER | XI86_POINTER_CAPABLE;
    info->type_name = "Mouse";
    info->fd = -1;
    info->conf_idev = dev;

    return info;
}

#else

/* debian 7
   ubuntu 12.04 */

/******************************************************************************/
static int
rdpmousePreInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    LLOGLN(0, ("rdpmousePreInit: drv %p info %p, flags 0x%x",
               drv, info, flags));
    info->device_control = rdpmouseControl;
    info->type_name = g_Mouse_str;
    return 0;
}

#endif

/******************************************************************************/
static void
rdpmouseUnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    LLOGLN(0, ("rdpmouseUnInit: drv %p info %p, flags 0x%x",
               drv, info, flags));
    rdpUnregisterInputCallback(rdpInputMouse);
}

/******************************************************************************/
static InputDriverRec rdpmouse =
{
    PACKAGE_VERSION_MAJOR,    /* version   */
    g_xrdp_mouse_name,        /* name      */
    NULL,                     /* identify  */
    rdpmousePreInit,          /* preinit   */
    rdpmouseUnInit,           /* uninit    */
    NULL,                     /* module    */
    0                         /* ref count */
};

/******************************************************************************/
static pointer
rdpmousePlug(pointer module, pointer options, int *errmaj, int *errmin)
{
    LLOGLN(0, ("rdpmousePlug:"));
    xf86AddInputDriver(&rdpmouse, module, 0);
    return module;
}

/******************************************************************************/
static void
rdpmouseUnplug(pointer p)
{
    LLOGLN(0, ("rdpmouseUnplug:"));
}

/******************************************************************************/
static XF86ModuleVersionInfo rdpmouseVersionRec =
{
    XRDP_MOUSE_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR,
    PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    { 0, 0, 0, 0 }
};

/******************************************************************************/
_X_EXPORT XF86ModuleData xrdpmouseModuleData =
{
    &rdpmouseVersionRec,
    rdpmousePlug,
    rdpmouseUnplug
};

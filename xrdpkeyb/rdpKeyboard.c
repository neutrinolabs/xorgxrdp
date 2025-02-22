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

xrdp keyboard module

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

#include <xkbsrv.h>

#include <X11/keysym.h>

#include "rdp.h"
#include "rdpInput.h"
#include "rdpDraw.h"
#include "rdpMisc.h"
#include "rdpMain.h"

// Check the minimum xrdp client version.
#if CLIENT_INFO_CURRENT_VERSION < 20240805
#error "xrdp is too old to contain evdev keyboard support"
#endif

#include "xrdp_scancode_defs.h"

/******************************************************************************/
#define LOG_LEVEL 1
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

/* A few hard-coded evdev keycodes (see g_evdev_str) */
#define CAPS_LOCK_KEY_CODE 66
#define NUM_LOCK_KEY_CODE 77
#define SCROLL_LOCK_KEY_CODE 78

static char g_evdev_str[] = "evdev";
static char g_pc104_str[] = "pc104";
static char g_us_str[] = "us";
static char g_empty_str[] = "";
static char g_Keyboard_str[] = "Keyboard";

static char g_xrdp_keyb_name[] = XRDP_KEYB_NAME;

static int
rdpLoadLayout(rdpKeyboard *keyboard, struct xrdp_client_info *client_info);

/******************************************************************************/
static void
rdpEnqueueKey(DeviceIntPtr device, int type, int scancode)
{
    if (type == KeyPress)
    {
        xf86PostKeyboardEvent(device, scancode, TRUE);
    }
    else
    {
        xf86PostKeyboardEvent(device, scancode, FALSE);
    }
}

/******************************************************************************/
static void
sendDownUpKeyEvent(DeviceIntPtr device, int type, int x_scancode)
{
    /* need this cause rdp and X11 repeats are different */
    /* if type is keydown, send keyup + keydown */
    if (type == KeyPress)
    {
        rdpEnqueueKey(device, KeyRelease, x_scancode);
        rdpEnqueueKey(device, KeyPress, x_scancode);
    }
    else
    {
        rdpEnqueueKey(device, KeyRelease, x_scancode);
    }
}

/******************************************************************************/
static void
check_keysa(rdpKeyboard *keyboard)
{
    // Terminate any pause sequence in progress
    keyboard->skip_numlock = 0;
}

/**
 * @param down   - true for KeyDown events, false otherwise
 * @param param1 - X11 keycode of pressed key
 * @param param2 -
 * @param param3 - keyCode from TS_KEYBOARD_EVENT
 * @param param4 - keyboardFlags from TS_KEYBOARD_EVENT
 ******************************************************************************/
static void
KbdAddEvent(rdpKeyboard *keyboard, int down, int param1, int param2,
            int param3, int param4)
{
    int x_keycode = param1;
    int rdp_scancode = SCANCODE_FROM_KBD_EVENT(param3, param4);
    int type = down ? KeyPress : KeyRelease;

    LLOGLN(1, ("KbdAddEvent: down=%d RDP scancode=%03x "
           "PDU keyCode=%04x PDU keyboardFlags=%04x X11 keycode=%04x",
           down, rdp_scancode,
           param3, param4, x_keycode));

    if (keyboard->skip_numlock)
    {
        keyboard->skip_numlock = 0;
        if (rdp_scancode == SCANCODE_NUMLOCK_KEY)
        {
            return;
        }
    }
    switch (rdp_scancode)
    {
        /* Non-repeating keys
         *
         * From Windows, these repeat anyway, so if the left-shift is
         * held down we get a stream of LeftShift KeyPress events. We just
         * pass these on to the X server to make sense of them */
        case SCANCODE_LSHIFT_KEY:
        case SCANCODE_RSHIFT_KEY:
        case SCANCODE_LCTRL_KEY:
        case SCANCODE_RCTRL_KEY:
        case SCANCODE_LALT_KEY:
        case SCANCODE_RALT_KEY:
        case SCANCODE_CAPS_KEY:
        case SCANCODE_NUMLOCK_KEY:
        case SCANCODE_LWIN_KEY:
        case SCANCODE_RWIN_KEY:
        case SCANCODE_MENU_KEY:
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            break;

        case SCANCODE_SCROLL_KEY:
            // Scroll lock is also non-repeating, but we need to keep
            // track of the key state to handle a TS_SYNC_EVENT from
            // the client.
            if (type == KeyPress)
            {
                if (keyboard->scroll_lock_down)
                {
                    // Key already down - ignore this one.
                }
                else
                {
                    // Debounced keypress
                    keyboard->scroll_lock_down = 1;
                    keyboard->scroll_lock_state = !keyboard->scroll_lock_state;
                }
            }
            else
            {
                keyboard->scroll_lock_down = 0;
            }
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            break;

        case SCANCODE_TAB_KEY:
            if (!down && !keyboard->tab_down)
            {
                /* mstsc.exe sends a tab up before and after a TS_SYNC_EVENT */
                check_keysa(keyboard);
            }
            else
            {
                sendDownUpKeyEvent(keyboard->device, type, x_keycode);
            }

            keyboard->tab_down = down;
            break;

        case SCANCODE_PAUSE_KEY:
            /* The pause key (0xE1 0x1D from the keyboard controller)
             * is mapped to a combination of ctrl and numlock events -
             * see [MS-RDPBCGR] 2.2.8.1.1.3.1.1.1 for details */
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            keyboard->skip_numlock = 1;
            break;

        default:
            if (x_keycode > 0)
            {
                sendDownUpKeyEvent(keyboard->device, type, x_keycode);
            }

            break;
    }
}

/******************************************************************************/
static void
KbdSync(rdpKeyboard *keyboard, int param1)
{
    int xkb_state;

    xkb_state = XkbStateFieldFromRec(&(keyboard->device->key->xkbInfo->state));
    LLOGLN(10, ("KbdSync: xkb_state=%04X", xkb_state));

    // Make sure the modifiers are released
    rdpEnqueueKey(keyboard->device, KeyRelease,
                  keyboard->x11_keycode_caps_lock);
    rdpEnqueueKey(keyboard->device, KeyRelease,
                  keyboard->x11_keycode_num_lock);
    rdpEnqueueKey(keyboard->device, KeyRelease,
                  keyboard->x11_keycode_scroll_lock);
    keyboard->scroll_lock_down = 0;

    // Caps_Lock is a specific modifier */
    if ((!(xkb_state & LockMask)) != (!(param1 & TS_SYNC_CAPS_LOCK)))
    {
        LLOGLN(0, ("KbdSync: toggling caps lock"));
        rdpEnqueueKey(keyboard->device, KeyPress,
                      keyboard->x11_keycode_caps_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease,
                      keyboard->x11_keycode_caps_lock);
    }

    // Num_Lock is normally mapped to mod2 (see 'xmodmap -pm')
    if ((!(xkb_state & Mod2Mask)) != (!(param1 & TS_SYNC_NUM_LOCK)))
    {
        LLOGLN(0, ("KbdSync: toggling num lock"));
        rdpEnqueueKey(keyboard->device, KeyPress,
                      keyboard->x11_keycode_num_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease,
                      keyboard->x11_keycode_num_lock);
    }

    // Scroll lock doesn't have its own modifier, so we need to track
    // it ourselves
    if ((!(keyboard->scroll_lock_state)) != (!(param1 & TS_SYNC_SCROLL_LOCK)))
    {
        LLOGLN(0, ("KbdSync: toggling scroll lock"));
        rdpEnqueueKey(keyboard->device, KeyPress,
                      keyboard->x11_keycode_scroll_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease,
                      keyboard->x11_keycode_scroll_lock);

        keyboard->scroll_lock_state = !keyboard->scroll_lock_state;
    }
}

/******************************************************************************/
static int
rdpInputKeyboard(rdpPtr dev, int msg, long param1, long param2,
                 long param3, long param4)
{
    rdpKeyboard *keyboard;

    keyboard = &(dev->keyboard);
    LLOGLN(10, ("rdpInputKeyboard:"));
    switch (msg)
    {
        case 15: /* key down */
        case 16: /* key up */
            KbdAddEvent(keyboard, msg == 15, param1, param2, param3, param4);
            break;
        case 17: /* from RDP_INPUT_SYNCHRONIZE */
            KbdSync(keyboard, param1);
            break;
        case 18:
            rdpLoadLayout(keyboard, (struct xrdp_client_info *) param1);
            break;

    }
    return 0;
}

/******************************************************************************/
static void
rdpkeybDeviceOn(void)
{
    LLOGLN(0, ("rdpkeybDeviceOn:"));
}

/******************************************************************************/
static void
rdpkeybDeviceOff(void)
{
    LLOGLN(0, ("rdpkeybDeviceOff:"));
}

/******************************************************************************/
static void
rdpkeybBell(int volume, DeviceIntPtr pDev, pointer ctrl, int cls)
{
    LLOGLN(0, ("rdpkeybBell:"));
}

/******************************************************************************/
static CARD32
rdpInDeferredRepeatCallback(OsTimerPtr timer, CARD32 now, pointer arg)
{
    DeviceIntPtr pDev;
    DeviceIntPtr it;
    Bool found;

    LLOGLN(0, ("rdpInDeferredRepeatCallback:"));
    TimerFree(timer);
    pDev = (DeviceIntPtr) arg;
    found = FALSE;
    it = inputInfo.devices;
    while (it != NULL)
    {
        if (it == pDev)
        {
            found = TRUE;
            break;
        }
        it = it->next;
    }
    if (found)
    {
        XkbSetRepeatKeys(pDev, -1, AutoRepeatModeOff);
    }
    return 0;
}

/******************************************************************************/
static void
rdpkeybChangeKeyboardControl(DeviceIntPtr pDev, KeybdCtrl *ctrl)
{
    XkbControlsPtr ctrls;

    LLOGLN(0, ("rdpkeybChangeKeyboardControl:"));
    ctrls = 0;
    if (pDev != 0)
    {
        if (pDev->key != 0)
        {
            if (pDev->key->xkbInfo != 0)
            {
                if (pDev->key->xkbInfo->desc != 0)
                {
                    if (pDev->key->xkbInfo->desc->ctrls != 0)
                    {
                        ctrls = pDev->key->xkbInfo->desc->ctrls;
                    }
                }
            }
        }
    }
    if (ctrls != 0)
    {
        if (ctrls->enabled_ctrls & XkbRepeatKeysMask)
        {
            LLOGLN(0, ("rdpkeybChangeKeyboardControl: autoRepeat on"));
            /* schedule to turn off the autorepeat after 100 ms so any app
             * polling it will be happy it's on */
            TimerSet(NULL, 0, 100, rdpInDeferredRepeatCallback, pDev);
        }
        else
        {
            LLOGLN(0, ("rdpkeybChangeKeyboardControl: autoRepeat off"));
        }
    }
}

/******************************************************************************/
static int
rdpkeybControl(DeviceIntPtr device, int what)
{
    DevicePtr pDev;
    rdpPtr dev;

    LLOGLN(0, ("rdpkeybControl: what %d", what));
    pDev = (DevicePtr)device;

    switch (what)
    {
        case DEVICE_INIT:
            dev = rdpGetDevFromScreen(NULL);
            dev->keyboard.device = device;
            rdpLoadLayout(&(dev->keyboard), NULL);
            rdpRegisterInputCallback(0, rdpInputKeyboard);
            break;
        case DEVICE_ON:
            pDev->on = 1;
            rdpkeybDeviceOn();
            break;
        case DEVICE_OFF:
            pDev->on = 0;
            rdpkeybDeviceOff();
            break;
        case DEVICE_CLOSE:
            if (pDev->on)
            {
                rdpkeybDeviceOff();
            }
            break;
    }
    return Success;
}

#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1, 9, 0, 1, 0)

/* debian 6
   ubuntu 10.04 */

/******************************************************************************/
static InputInfoPtr
rdpkeybPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr info;

    LLOGLN(0, ("rdpkeybPreInit: drv %p dev %p, flags 0x%x",
           drv, dev, flags));
    info = xf86AllocateInput(drv, 0);
    info->name = dev->identifier;
    info->device_control = rdpkeybControl;
    info->flags = XI86_CONFIGURED | XI86_ALWAYS_CORE | XI86_SEND_DRAG_EVENTS |
                  XI86_CORE_KEYBOARD | XI86_KEYBOARD_CAPABLE;
    info->type_name = "Keyboard";
    info->fd = -1;
    info->conf_idev = dev;

    return info;
}

#else

/* debian 7
   ubuntu 12.04 */

/******************************************************************************/
static int
rdpkeybPreInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    LLOGLN(0, ("rdpkeybPreInit: drv %p info %p, flags 0x%x",
           drv, info, flags));
    info->device_control = rdpkeybControl;
    info->type_name = g_Keyboard_str;

    return 0;
}

#endif

/******************************************************************************/
static void
rdpkeybUnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    LLOGLN(0, ("rdpkeybUnInit: drv %p info %p, flags 0x%x",
           drv, info, flags));
    rdpUnregisterInputCallback(rdpInputKeyboard);
}

/******************************************************************************/
static InputDriverRec rdpkeyb =
{
    .driverVersion = PACKAGE_VERSION_MAJOR,
    .driverName = g_xrdp_keyb_name,
    .PreInit = rdpkeybPreInit,
    .UnInit = rdpkeybUnInit
};

/******************************************************************************/
static pointer
rdpkeybPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
    LLOGLN(0, ("rdpkeybPlug:"));
    xf86AddInputDriver(&rdpkeyb, module, 0);
    xorgxrdpCheckWrap();
    return module;
}

/******************************************************************************/
static void
rdpkeybUnplug(pointer p)
{
    LLOGLN(0, ("rdpkeybUnplug:"));
}

/******************************************************************************/
static int
reload_xkb(DeviceIntPtr keyboard, XkbRMLVOSet *set)
{
    KeySymsPtr keySyms;
    KeyCode first_key;
    CARD8 num_keys;
    DeviceIntPtr pDev;

    /* free some stuff so we can call InitKeyboardDeviceStruct again */
    if (keyboard->key != NULL)
    {
        XkbSrvInfoPtr xkbi = keyboard->key->xkbInfo;
        if (xkbi != NULL)
        {
            XkbDescPtr xkb = xkbi->desc;
            if (xkb != NULL)
            {
                XkbFreeKeyboard(xkb, 0, TRUE);
            }
            free(xkbi);
        }
        free(keyboard->kbdfeed);
        keyboard->kbdfeed = NULL;
        free(keyboard->key);
        keyboard->key = NULL;
    }

    /* init keyboard and reload the map */
    if (!InitKeyboardDeviceStruct(keyboard, set, rdpkeybBell,
                                  rdpkeybChangeKeyboardControl))
    {
        LLOGLN(0, ("reload_xkb: InitKeyboardDeviceStruct failed"));
        return 1;
    }

    /* notify the X11 clients eg. X_ChangeKeyboardMapping */
    keySyms = XkbGetCoreMap(keyboard);
    if (keySyms)
    {
        first_key = keySyms->minKeyCode;
        num_keys = (keySyms->maxKeyCode - keySyms->minKeyCode) + 1;
        XkbApplyMappingChange(keyboard, keySyms, first_key, num_keys,
                              NULL, serverClient);
        for (pDev = inputInfo.devices; pDev; pDev = pDev->next)
        {
            if ((pDev->coreEvents || pDev == keyboard) && pDev->key)
            {
                XkbApplyMappingChange(pDev, keySyms, first_key, num_keys,
                                      NULL, serverClient);
            }
        }
        free(keySyms->map);
        free(keySyms);
    }
    else
    {
        return 1;
    }
    return 0;
}

/******************************************************************************/
static int
rdpLoadLayout(rdpKeyboard *keyboard, struct xrdp_client_info *client_info)
{
    // Load default layout parameters
    XkbRMLVOSet set =
    {
        .rules = g_evdev_str,
        .model = g_pc104_str,
        .layout = g_us_str,
        .variant = g_empty_str,
        .options = g_empty_str
    };

    if (client_info != NULL)
    {
        if (strlen(client_info->model) > 0)
        {
            set.model = client_info->model;
        }
        if (strlen(client_info->variant) > 0)
        {
            set.variant = client_info->variant;
        }
        if (strlen(client_info->layout) > 0)
        {
            set.layout = client_info->layout;
        }
        if (strlen(client_info->options) > 0)
        {
            set.options = client_info->options;
        }
        if (strlen(client_info->xkb_rules) > 0)
        {
            set.rules = client_info->xkb_rules;
        }

        /* X11 keycodes needed to sync the keyboard */
        keyboard->x11_keycode_caps_lock = client_info->x11_keycode_caps_lock;
        keyboard->x11_keycode_num_lock = client_info->x11_keycode_num_lock;
        keyboard->x11_keycode_scroll_lock = client_info->x11_keycode_scroll_lock;
    }
    else
    {
        keyboard->x11_keycode_caps_lock = CAPS_LOCK_KEY_CODE;
        keyboard->x11_keycode_num_lock = NUM_LOCK_KEY_CODE;
        keyboard->x11_keycode_scroll_lock = SCROLL_LOCK_KEY_CODE;
    }

    LLOGLN(0, ("rdpLoadLayout: rules=\"%s\" model=\"%s\" variant=\"%s\""
               "layout=\"%s\" options=\"%s\"",
               set.rules, set.model, set.variant, set.layout, set.options));

    reload_xkb(keyboard->device, &set);
    reload_xkb(inputInfo.keyboard, &set);

    return 0;
}

/******************************************************************************/
static XF86ModuleVersionInfo rdpkeybVersionRec =
{
    .modname = XRDP_KEYB_NAME,
    .vendor = MODULEVENDORSTRING,
    ._modinfo1_ = MODINFOSTRING1,
    ._modinfo2_ = MODINFOSTRING2,
    .xf86version = XORG_VERSION_CURRENT,
    .majorversion = PACKAGE_VERSION_MAJOR,
    .minorversion = PACKAGE_VERSION_MINOR,
    .patchlevel = PACKAGE_VERSION_PATCHLEVEL,
    .abiclass = ABI_CLASS_XINPUT,
    .abiversion = ABI_XINPUT_VERSION,
    .moduleclass = MOD_CLASS_XINPUT
};

/******************************************************************************/
_X_EXPORT XF86ModuleData xrdpkeybModuleData =
{
    .vers = &rdpkeybVersionRec,
    .setup = rdpkeybPlug,
    .teardown = rdpkeybUnplug
};

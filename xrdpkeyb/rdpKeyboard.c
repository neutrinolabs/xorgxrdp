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

#include "xrdp_scancode_defs.h"

/******************************************************************************/
/* A few hard-coded evdev keycodes (see g_evdev_str) */
#define SHIFT_L_KEY_CODE 50
#define CAPS_LOCK_KEY_CODE 66
#define NUM_LOCK_KEY_CODE 77
#define SCROLL_LOCK_KEY_CODE 78

#ifndef WM_UNICODE_INPUT
/* xrdp uses this backend message for RDP Unicode keyboard input. Some installed
 * headers do not define it yet, so keep a local fallback for older builds. */
#define WM_UNICODE_INPUT 19
#endif

static char g_evdev_str[] = "evdev";
static char g_pc104_str[] = "pc104";
static char g_us_str[] = "us";
static char g_empty_str[] = "";
static char g_Keyboard_str[] = XI_KEYBOARD;

static char g_xrdp_keyb_name[] = XRDP_KEYB_NAME;
static int g_initial_sync_completed = 0;
static int g_deferred_sync_flags = 0;
/* Forward declaration for deferred synchronization */
static void KbdSync(rdpKeyboard *keyboard, int param1);

static int
rdpLoadLayout(rdpKeyboard *keyboard, struct xup_client_info *client_info);

/******************************************************************************/
static int
unicode_keycode_reserved(int keycode)
{
    /* Do not use keycodes which the default XKB map assigns to modifiers, IME
     * controls or special keypad variants. Remapping those can leak modifier
     * state into committed Unicode text. */
    switch (keycode)
    {
        case 134: /* Brazilian keypad . */
        case 156: /* meta keys */
        case 211: /* Brazilian / ? */
            return 1;
    }

    if (keycode >= 208 && keycode <= 217)
    {
        /* IME and XF86 keyboard-control keys on the default XKB map */
        return 1;
    }

    return 0;
}

/******************************************************************************/
static void
rdpUnicodeReset(rdpKeyboard *keyboard)
{
    memset(keyboard->unicode_codepoints, 0,
           sizeof(keyboard->unicode_codepoints));
    keyboard->unicode_next_index = 0;
}

/******************************************************************************/
static KeySym
unicode_to_keysym(unsigned int unicode)
{
    /* Keep control characters as their normal X11 keysyms. Printable Unicode
     * outside Latin-1 uses the X11 U+01000000 keysym encoding. */
    switch (unicode)
    {
        case 0x08:
            return XK_BackSpace;
        case 0x09:
            return XK_Tab;
        case 0x0a:
        case 0x0d:
            return XK_Return;
        case 0x1b:
            return XK_Escape;
    }

    if (unicode == 0 || unicode > 0x10ffff ||
            (unicode >= 0xd800 && unicode <= 0xdfff))
    {
        return NoSymbol;
    }

    return (unicode < 0x100) ? unicode : (0x01000000 | unicode);
}

/******************************************************************************/
static int
unicode_ascii_letter_needs_shift(rdpKeyboard *keyboard, unsigned int unicode)
{
    int is_upper;
    int is_lower;
    int caps_lock;
    int xkb_state;

    is_upper = unicode >= 'A' && unicode <= 'Z';
    is_lower = unicode >= 'a' && unicode <= 'z';
    if (!is_upper && !is_lower)
    {
        return 0;
    }

    /* XKB canonicalizes letter keysyms to lower/upper levels. For Unicode
     * ASCII letters, add a temporary Shift press only when it is needed to make
     * XLookupString return the exact character sent by the RDP client. */
    caps_lock = 0;
    if (keyboard != NULL && keyboard->device != NULL &&
            keyboard->device->key != NULL &&
            keyboard->device->key->xkbInfo != NULL)
    {
        xkb_state = XkbStateFieldFromRec(&(keyboard->device->key->xkbInfo->state));
        caps_lock = (xkb_state & LockMask) != 0;
    }

    return is_upper != caps_lock;
}

/******************************************************************************/
static int
rdpUnicodeSetKeySym(DeviceIntPtr device, int keycode, KeySym keysym)
{
    KeySymsPtr keySyms;
    int offset;
    int index;
    DeviceIntPtr pDev;

    if (device == NULL)
    {
        return 1;
    }

    keySyms = XkbGetCoreMap(device);
    if (keySyms == NULL)
    {
        return 1;
    }

    if (keycode < keySyms->minKeyCode || keycode > keySyms->maxKeyCode)
    {
        free(keySyms->map);
        free(keySyms);
        return 1;
    }

    offset = (keycode - keySyms->minKeyCode) * keySyms->mapWidth;
    keySyms->map[offset] = keysym;
    for (index = 1; index < keySyms->mapWidth; ++index)
    {
        keySyms->map[offset + index] = NoSymbol;
    }

    /* Update both the xrdp keyboard device and core-capable keyboard devices.
     * X clients generally resolve keysyms through the core keyboard map, so
     * updating only the extension device can make Unicode events disappear. */
    XkbApplyMappingChange(device, keySyms, keycode, 1, NULL, serverClient);
    for (pDev = inputInfo.devices; pDev; pDev = pDev->next)
    {
        if ((pDev->coreEvents || pDev == device) && pDev->key)
        {
            XkbApplyMappingChange(pDev, keySyms, keycode, 1, NULL,
                                  serverClient);
        }
    }

    free(keySyms->map);
    free(keySyms);
    return 0;
}

/******************************************************************************/
static int
rdpUnicodeFindKeycode(rdpKeyboard *keyboard, unsigned int unicode)
{
    KeySym keysym;
    int index;
    int keycode;
    int attempts;

    /* Reuse an existing scratch keycode for repeated characters. This keeps
     * mobile keyboard repeats and pasted runs from constantly changing XKB. */
    for (index = 0; index < XRDP_UNICODE_KEYCODE_COUNT; ++index)
    {
        keycode = XRDP_UNICODE_KEYCODE_FIRST + index;
        if (!unicode_keycode_reserved(keycode) &&
                keyboard->unicode_codepoints[index] == unicode)
        {
            return keycode;
        }
    }

    keysym = unicode_to_keysym(unicode);
    if (keysym == NoSymbol)
    {
        return 0;
    }

    for (attempts = 0; attempts < XRDP_UNICODE_KEYCODE_COUNT; ++attempts)
    {
        index = keyboard->unicode_next_index++;
        if (keyboard->unicode_next_index >= XRDP_UNICODE_KEYCODE_COUNT)
        {
            keyboard->unicode_next_index = 0;
        }

        keycode = XRDP_UNICODE_KEYCODE_FIRST + index;
        if (unicode_keycode_reserved(keycode))
        {
            continue;
        }

        /* New character: map it onto the next available scratch keycode in a
         * small ring and post a normal key press/release for that keycode. */
        if (rdpUnicodeSetKeySym(keyboard->device, keycode, keysym) == 0)
        {
            keyboard->unicode_codepoints[index] = unicode;
            return keycode;
        }
    }

    return 0;
}

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
    if (keyboard->ctrl_down != 0)
    {
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->ctrl_down);
        keyboard->ctrl_down = 0;
    }

    if (keyboard->alt_down != 0)
    {
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->alt_down);
        keyboard->alt_down = 0;
    }

    if (keyboard->shift_down != 0)
    {
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->shift_down);
        keyboard->shift_down = 0;
    }

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

    /* Perform deferred synchronization on the first keyboard interaction */
    if (!g_initial_sync_completed && down)
    {
        g_initial_sync_completed = 1;
        LOG(LOG_LEVEL_DEBUG, "KbdAddEvent: First key press, enforcing deferred sync");
        KbdSync(keyboard, g_deferred_sync_flags);
    }
    int x_keycode = param1;
    int rdp_scancode = SCANCODE_FROM_KBD_EVENT(param3, param4);
    int type = down ? KeyPress : KeyRelease;

    LOG(LOG_LEVEL_DEBUG, "KbdAddEvent: down=%d RDP scancode=%03x "
        "PDU keyCode=%04x PDU keyboardFlags=%04x X11 keycode=%04x",
        down, rdp_scancode,
        param3, param4, x_keycode);

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
            keyboard->shift_down = down ? x_keycode : 0;
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            break;

        case SCANCODE_LCTRL_KEY:
        case SCANCODE_RCTRL_KEY:
            keyboard->ctrl_down = down ? x_keycode : 0;
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            break;

        case SCANCODE_LALT_KEY:
        case SCANCODE_RALT_KEY:
            keyboard->alt_down = down ? x_keycode : 0;
            rdpEnqueueKey(keyboard->device, type, x_keycode);
            break;

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
            sendDownUpKeyEvent(keyboard->device, type, x_keycode);
            break;
    }
}

/******************************************************************************/
static void
KbdSync(rdpKeyboard *keyboard, int param1)
{
    int xkb_state;
    int target, current;

    if (keyboard->device == NULL || keyboard->device->key == NULL ||
        keyboard->device->key->xkbInfo == NULL)
    {
        /* Save flags to be replayed when device is ready */
        g_deferred_sync_flags = param1;
        return;
    }

    g_deferred_sync_flags = param1;
    xkb_state = XkbStateFieldFromRec(&(keyboard->device->key->xkbInfo->state));

    /* MS-RDPBCGR 2.2.8.1.1.3.1.1.5: Reset lock keys to UP state */
    rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_caps_lock);
    rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_num_lock);
    rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_scroll_lock);

    keyboard->scroll_lock_down = 0;

    /* Caps Lock alignment */
    target = (param1 & TS_SYNC_CAPS_LOCK) ? 1 : 0;
    current = (xkb_state & LockMask) ? 1 : 0;
    if (current != target)
    {
        LOG(LOG_LEVEL_INFO, "KbdSync: Aligning Caps Lock (current=%d, target=%d)", current, target);
        rdpEnqueueKey(keyboard->device, KeyPress, keyboard->x11_keycode_caps_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_caps_lock);
    }

    /* Num Lock alignment */
    target = (param1 & TS_SYNC_NUM_LOCK) ? 1 : 0;
    current = (xkb_state & Mod2Mask) ? 1 : 0;
    if (current != target)
    {
        LOG(LOG_LEVEL_INFO, "KbdSync: Aligning Num Lock (current=%d, target=%d)", current, target);
        rdpEnqueueKey(keyboard->device, KeyPress, keyboard->x11_keycode_num_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_num_lock);
    }

    /* Scroll Lock alignment */
    target = (param1 & TS_SYNC_SCROLL_LOCK) ? 1 : 0;
    current = keyboard->scroll_lock_state ? 1 : 0;
    if (current != target)
    {
        LOG(LOG_LEVEL_INFO, "KbdSync: Aligning Scroll Lock (current=%d, target=%d)", current, target);
        rdpEnqueueKey(keyboard->device, KeyPress, keyboard->x11_keycode_scroll_lock);
        rdpEnqueueKey(keyboard->device, KeyRelease, keyboard->x11_keycode_scroll_lock);
        keyboard->scroll_lock_state = target;
    }
}

/******************************************************************************/
static void
KbdUnicodeEvent(rdpKeyboard *keyboard, unsigned int unicode)
{
    int x_keycode;
    int needs_shift;

    x_keycode = rdpUnicodeFindKeycode(keyboard, unicode);
    if (x_keycode > 0)
    {
        /* Release tracked physical modifiers first so Unicode commits are
         * literal text, not Ctrl/Alt/Shift shortcuts. ASCII letter case is then
         * restored explicitly with a temporary Shift when required. */
        check_keysa(keyboard);
        needs_shift = unicode_ascii_letter_needs_shift(keyboard, unicode);
        if (needs_shift)
        {
            rdpEnqueueKey(keyboard->device, KeyPress, SHIFT_L_KEY_CODE);
        }
        rdpEnqueueKey(keyboard->device, KeyPress, x_keycode);
        rdpEnqueueKey(keyboard->device, KeyRelease, x_keycode);
        if (needs_shift)
        {
            rdpEnqueueKey(keyboard->device, KeyRelease, SHIFT_L_KEY_CODE);
        }
    }
}

/******************************************************************************/
static int
rdpInputKeyboard(rdpPtr dev, int msg, long param1, long param2,
                 long param3, long param4)
{
    rdpKeyboard *keyboard;

    keyboard = &(dev->keyboard);
    LOG(LOG_LEVEL_TRACE, "rdpInputKeyboard:");
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
            rdpUnicodeReset(keyboard);
            rdpLoadLayout(keyboard, (struct xup_client_info *) param1);
            break;
        case WM_UNICODE_INPUT:
            KbdUnicodeEvent(keyboard, (unsigned int) param1);
            break;

    }
    return 0;
}

/******************************************************************************/
static void
rdpkeybDeviceOn(void)
{
    LOG(LOG_LEVEL_TRACE, "rdpkeybDeviceOn:");
}

/******************************************************************************/
static void
rdpkeybDeviceOff(void)
{
    LOG(LOG_LEVEL_TRACE, "rdpkeybDeviceOff:");
}

/******************************************************************************/
static void
rdpkeybBell(int volume, DeviceIntPtr pDev, pointer ctrl, int cls)
{
    LOG(LOG_LEVEL_TRACE, "rdpkeybBell:");
}

/******************************************************************************/
static CARD32
rdpInDeferredRepeatCallback(OsTimerPtr timer, CARD32 now, pointer arg)
{
    DeviceIntPtr pDev;
    DeviceIntPtr it;
    Bool found;

    LOG(LOG_LEVEL_TRACE, "rdpInDeferredRepeatCallback:");
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

    LOG(LOG_LEVEL_TRACE, "rdpkeybChangeKeyboardControl:");
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
            LOG(LOG_LEVEL_INFO, "rdpkeybChangeKeyboardControl: autoRepeat on");
            /* schedule to turn off the autorepeat after 100 ms so any app
             * polling it will be happy it's on */
            TimerSet(NULL, 0, 100, rdpInDeferredRepeatCallback, pDev);
        }
        else
        {
            LOG(LOG_LEVEL_INFO, "rdpkeybChangeKeyboardControl: autoRepeat off");
        }
    }
}

/******************************************************************************/
static int
rdpkeybControl(DeviceIntPtr device, int what)
{
    DevicePtr pDev;
    rdpPtr dev;

    LOG(LOG_LEVEL_INFO, "rdpkeybControl: what %d", what);
    pDev = (DevicePtr)device;

    switch (what)
    {
        case DEVICE_INIT:
            dev = rdpGetDevFromScreen(NULL);
            dev->keyboard.device = device;
            rdpLoadLayout(&(dev->keyboard), NULL);
            rdpUnicodeReset(&(dev->keyboard));
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

    LOG(LOG_LEVEL_INFO, "rdpkeybPreInit: drv %p dev %p, flags 0x%x",
        drv, dev, flags);
    info = xf86AllocateInput(drv, 0);
    info->name = dev->identifier;
    info->device_control = rdpkeybControl;
    info->flags = XI86_CONFIGURED | XI86_ALWAYS_CORE | XI86_SEND_DRAG_EVENTS |
                  XI86_CORE_KEYBOARD | XI86_KEYBOARD_CAPABLE;
    info->type_name = g_Keyboard_str;
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
    LOG(LOG_LEVEL_INFO, "rdpkeybPreInit: drv %p info %p, flags 0x%x",
        drv, info, flags);
    info->device_control = rdpkeybControl;
    info->type_name = g_Keyboard_str;

    return 0;
}

#endif

/******************************************************************************/
static void
rdpkeybUnInit(InputDriverPtr drv, InputInfoPtr info, int flags)
{
    LOG(LOG_LEVEL_INFO, "rdpkeybUnInit: drv %p info %p, flags 0x%x",
        drv, info, flags);
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
    LOG(LOG_LEVEL_TRACE, "rdpkeybPlug:");
    xf86AddInputDriver(&rdpkeyb, module, 0);
    xorgxrdpCheckWrap();
    return module;
}

/******************************************************************************/
static void
rdpkeybUnplug(pointer p)
{
    LOG(LOG_LEVEL_TRACE, "rdpkeybUnplug:");
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
        LOG(LOG_LEVEL_INFO, "reload_xkb: InitKeyboardDeviceStruct failed");
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
rdpLoadLayout(rdpKeyboard *keyboard, struct xup_client_info *client_info)
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

    LOG(LOG_LEVEL_INFO,
        "rdpLoadLayout: rules=\"%s\" model=\"%s\" variant=\"%s\""
        "layout=\"%s\" options=\"%s\"",
        set.rules, set.model, set.variant, set.layout, set.options);

    reload_xkb(keyboard->device, &set);
    reload_xkb(inputInfo.keyboard, &set);

    /* Allow re-synchronization on session reconnect */
    g_initial_sync_completed = 0;

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

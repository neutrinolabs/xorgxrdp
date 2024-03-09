# Release notes for xrdp v0.10.0 (2024/03/10)

This section notes changes since the [v0.10 branch](#branch-v010) was created. 

## General announcements
The biggest news of this release is that [Graphic Pipeline Extension](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/da5c75f9-cd99-450c-98c4-014a496942b0) also called GFX in short has been supported. xrdp v0.10 with GFX achieves more frame rates and less bandwidth compared to v0.9. There is a significant performance improvement especially if the client is Windows 11's mstsc.exe or Microsoft Remote Desktop for Mac. GFX H.264/AVC 444 mode and hardware-accelerated encoding are not supported in this version yet.

GFX implementation in xrdp is sponsored by an enterprise sponsor. We very much appreciate the sponsorship. It helped us to accelerate xrdp development and land GFX earlier!

Please consider sponsoring or making a donation to the project if you like xrdp. We accept financial contributions via [Open Collective](https://opencollective.com/xrdp-project). Direct donations to each developer via GitHub Sponsors are also welcomed.

## Security fixes
None

## New features
None

## Bug fixes
* Fix some monitor hotplug issues (#287)
- RandR outputs and CRT controllers are not now deleted on a resize, allowing `xev` to be used to monitor RandR events (#284)

## Internal changes
None

## Known issues
None

## Changes for packagers or developers
* If moving from v0.9.x, read the '[Significant changes for packagers or developers section](#significant-changes-for-packagers-or-developers)' for the v0.10 branch below.

-----------------------
# Branch v0.10

This branch was forked from development on 2024-02-08 in preparation for testing and release of v0.10.1.

The changes in this section are relative to version v0.9.23 of xorgxrdp.

## General announcements
This software release is intended for use with xrdp v10.y.z

It has not been tested with previous versions of xrdp.

## New features
- Intel hardware is supported for VDPAU (#215 #216 #218) - thanks to @akarl10
- Use damage to track any lost screen changes (#186, #244)
- Touchpad inertial scrolling is now supported (#234)

## Bug fixes
- Ignore screen size changes which don't change anything (#203)
- Made sure xdpyinfo was available for CI test (#225)
- fix mouse scrolling too fast and implement inertial scrolling (#227, #234). Thanks to @seflerZ for this development.
## Internal changes
- A fix was made to the GitHub CI workflow to update the package cache (#213)
- A fix was made to better support xrdp PR #1895 (#212)
- The CI build now checks Glamor compilation works (#219)
- Updated github actions for CI to address warnings (#240)
- Disabled some auto-add hardware features (#241)
- and RandR output is now created before the client connects (#254)

- sh improvements(#228)
- Use xorg version to see if glamor_egl_get_driver_name() exists (#239)

## Known issues
None

## Changes for packagers or developers
- Build now works on OpenIndiana (#267)
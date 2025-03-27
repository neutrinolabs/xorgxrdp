# Release notes for xorgxrdp v0.10.4 (2025/03/30)

## General announcements

[Power Up Privacy](https://powerupprivacy.com/) and [Cybertrust Japan](https://www.cybertrust.co.jp/english/) sponsored H.264 encoding.  We greatly appreciate the sponsorship. 

Please consider sponsoring or making a donation to the project if you like xrdp. We accept financial contributions via [Open Collective](https://opencollective.com/xrdp-project). Direct donations to each developer via GitHub Sponsors are also welcomed.

## Security fixes
None

## New features
None

## Bug fixes
None

## Internal changes
* The dixGetDisplayName() function is used to access the display name where available (#367)
* FreeBSD CI testing is now performed (#379)
* autoconf version is changed from 2.65 to 2.69 (#378)

## Known issues
None

## Changes for packagers or developers

- This version is intended to be used together with xrdp v0.10.3 or later. Please build against xrdp v0.10.3 and provide both xrdp v0.10.3 and xorgxrdp v0.10.4 at the same time.

-----------------------

# Release notes for xorgxrdp v0.10.3 (2024/12/15)

## General announcements

[Power Up Privacy](https://powerupprivacy.com/) and @CyberTrust sponsored H.264 encoding.  We greatly appreciate the sponsorship. 

Please consider sponsoring or making a donation to the project if you like xrdp. We accept financial contributions via [Open Collective](https://opencollective.com/xrdp-project). Direct donations to each developer via GitHub Sponsors are also welcomed.

## Security fixes
None

## New features
- H.264 capture is now supported, see xrdp v0.10.2 release note for details (#355)
- Frame capture interval (frame rate) can now be configured separately via xrdp for H.264 and RFX (#347 #353)

## Bug fixes
- Fix dependency when building without glamor (#330)
- RandR is now aware of physical monitor sizes if these have been passed to the client (#337)
- Glamor whitelist now supports `amdgpu` (#329) and `msm` (#346) drivers. Some users of this hardware had reported a regression following the introduction of #322 in v0.10.2

## Internal changes
None

## Known issues
None

## Changes for packagers or developers

- This version is intended to be used together with xrdp v0.10.2 or later. Please build against xrdp v0.10.2 and provide both xrdp v0.10.2 and xorgxrdp v0.10.3 at the same time.

-----------------------

# Release notes for xorgxrdp v0.10.2 (2024/07/30)

## General announcements
The biggest news of v0.10 is that [Graphic Pipeline Extension](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/da5c75f9-cd99-450c-98c4-014a496942b0) also called GFX in short has been supported. xrdp v0.10 with GFX achieves more frame rates and less bandwidth compared to v0.9. There is a significant performance improvement especially if the client is Windows 11's mstsc.exe or Microsoft Remote Desktop for Mac. GFX H.264/AVC 444 mode and hardware-accelerated encoding are not supported in this version yet.

GFX implementation in xrdp is sponsored by an enterprise sponsor. @CyberTrust is also one of the sponsors. We very much appreciate the sponsorship. It helped us to accelerate xrdp development and land GFX earlier!

Please consider sponsoring or making a donation to the project if you like xrdp. We accept financial contributions via [Open Collective](https://opencollective.com/xrdp-project). Direct donations to each developer via GitHub Sponsors are also welcomed.

## Security fixes
None

## New features
- Check list of support devices in glamor (backport of #322)
- Support NULL cursors and large mono cursors (#320, backport of #323)

## Bug fixes
- Separate out key frame request from MAX_INT frame ACK. This prevents some cases of screen corruption on multi-monitor setups (backport of #288)
- Fixes complex dirty region causing overflow of xrdp comms buffer (#318, backport of #319)

## Internal changes
None

## Known issues
None

## Changes for packagers or developers
- If moving from v0.9.x, read the '[Significant changes for packagers or developers section](#significant-changes-for-packagers-or-developers)' for the v0.10 branch below.

-----------------------
# Release notes for xorgxrdp v0.10.1 (2024/04/20)

## General announcements
The biggest news of v0.10 is that [Graphic Pipeline Extension](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/da5c75f9-cd99-450c-98c4-014a496942b0) also called GFX in short has been supported. xrdp v0.10 with GFX achieves more frame rates and less bandwidth compared to v0.9. There is a significant performance improvement especially if the client is Windows 11's mstsc.exe or Microsoft Remote Desktop for Mac. GFX H.264/AVC 444 mode and hardware-accelerated encoding are not supported in this version yet.

GFX implementation in xrdp is sponsored by an enterprise sponsor. @CyberTrust is also one of the sponsors. We very much appreciate the sponsorship. It helped us to accelerate xrdp development and land GFX earlier!

Please consider sponsoring or making a donation to the project if you like xrdp. We accept financial contributions via [Open Collective](https://opencollective.com/xrdp-project). Direct donations to each developer via GitHub Sponsors are also welcomed.

## Security fixes
None

## New features
- Switch to wyhash from CRC for capture tile diff and introduce lazy color conversion (#167 #301)
    - Thanks to @trishume!

## Bug fixes
None

## Internal changes
None

## Known issues
None

## Changes for packagers or developers
- If moving from v0.9.x, read the '[Significant changes for packagers or developers section](#significant-changes-for-packagers-or-developers)' for the v0.10 branch below.

-----------------------

# Release notes for xorgxrdp v0.10.0 (2024/03/10)

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
[![Build Status](https://github.com/neutrinolabs/xorgxrdp/actions/workflows/build.yml/badge.svg)](https://github.com/neutrinolabs/xorgxrdp/actions)

*Current Version:* 0.2.18

# xorgxrdp

## Overview

**xorgxrdp** is a collection of modules to be used with a pre-existing X.Org
install to make the X server act like X11rdp. Unlike X11rdp, you don't have to
recompile the whole X Window System. Instead, additional modules are installed to
a location where the existing Xorg installation would pick them.

xorgxrdp is to be used together with [xrdp](https://github.com/neutrinolabs/xrdp)
and X.Org Server. It is pretty useless using xorgxrdp alone.

![xorgxrdp overview](https://github.com/neutrinolabs/xorgxrdp/raw/gh-pages/docs/xorgxrdp_overview.png)

## Features

xorgxrdp supports screen resizing. When an RDP client connects, the screen is
resized to the size supplied by the client.

xorgxrdp uses 24 bits per pixel internally. xrdp translates the color depth for
the RDP client as requested. RDP clients can disconnect and reconnect to the same
session even if they use different color depths.

## Compiling

### Pre-requisites

To compile xorgxrdp from the packaged sources, you need basic build tools - a
compiler (**gcc** or **clang**) and the **make** program. Additionally, you would
need **nasm** (Netwide Assembler) and the development package for X Window System
(look for **xserver-xorg-dev**, **xorg-x11-server-sdk** or
**xorg-x11-server-devel** in your distro).

To compile xorgxrdp from a checked out git repository, you would additionally
need **autoconf**, **automake**, **libtool** and **pkgconfig**.

### Get the source and build it

xorgxrdp requires a header file from xrdp. So it's preferred that xrdp is
compiled and installed first.

If compiling from the packaged source, unpack the tarball and change to the
resulting directory.

If compiling from a checked out repository, run `./bootstrap` first to create
`configure` and other required files.

Then run following commands to compile and install xorgxrdp:

```
./configure
make
sudo make install
```

If you don't want to install xrdp first, you can compile xorgxrdp against xrdp
sources by specifying XRDP_CFLAGS on the `configure` command line.

```
./configure XRDP_CFLAGS=-I/path/to/xrdp/common
```

## Usage

When logging in to xrdp using an RDP client, make sure to select Xorg on the
login screen. xrdp will tell xrdp-sesman to start Xorg with the configuration
file that activates the xorgxrdp modules.

Make sure your system has the X.Org server (typically **xserver-xorg-core** or
**xorg-x11-server-Xorg** package). Check that Xorg is in the standard path
(normally in `/usr/bin`).

## Contributing

First off, thanks for taking your time for improve xorgxrdp.

If you contribute to xorgxrdp, checkout **devel** branch and make changes to
the branch. Please make pull requests also versus **devel** branch.

To debug xorgxrdp, you can run Xorg with xorgxrdp manually:

```
Xorg :10 -config xrdp/xorg.conf
```

See also the `tests` directory for the tests that exercise xorgxrdp modules.

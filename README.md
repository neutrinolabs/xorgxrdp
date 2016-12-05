# xorgxrdp

## Overview
**xorgxrdp** is a collection of modules to be used with the pre-existing X.Org install to make the X server act like X11rdp. Unlike X11rdp, you don't have to recompile the whole X Window System. Instead, additional drivers are installed to a location where the existing Xorg installation would pick them.

xorgxrdp is to be used together with [xrdp](https://github.com/neutrinolabs/xrdp) and X.Org Server. It is pretty useless using xorgxrdp alone.

## Compiling

### Pre-requisites

To compile from the packaged sources, you would need basic build tools - a compiler (**gcc** or **clang**) and the **make** program. Additionally, you would need **nasm** (Netwide Assembler) and the headers for X Window System (look for **xserver-xorg-dev**, **xorg-x11-server-sdk** or **xorg-x11-server-devel** in your distro).

To compile from the checked out directory, you would additionally need **autoconf**, **automake**, **libtool** and **pkgconfig**.

### Get the source & build it

xorgxrdp requires xrdp header files. There are different procedures to build xorgxrdp whether you have installed xrdp headers or not.

If you have already installed xrdp,
```
git clone https://github.com/neutrinolabs/xorgxrdp.git
cd xorgxrdp
./bootstrap && ./configure && make
```

If you haven't installed xrdp and don't want to install xrdp,
```
git clone https://github.com/neutrinolabs/xorgxrdp.git
cd xorgxrdp
git clone --depth 1 --branch=devel https://github.com/neutrinolabs/xrdp.git xrdp
sed -e 's|@VERSION@|0.9.0|g' -e "s|@abs_top_srcdir@|`pwd`/xrdp|g" xrdp/pkgconfig/xrdp-uninstalled.pc.in > xrdp/pkgconfig/xrdp-uninstalled.pc
./bootstrap && env PKG_CONFIG_PATH=xrdp/pkgconfig ./configure && make
```

## Usage
As mentioned abobe, xorgxrdp is to be used together with xrdp and X.Org Server. You would need X.Org Server (**xserver-xorg-core** or **xorg-x11-server-Xorg**).  xorgxrdp will be ignited automatically by xrdp (xrdp-sesman, speaking more accurately) so usually you don't need to run xorgxrdp manually except in case of debugging.

In case of debugging, this is a typical example to run Xorg with xorgxrdp. Additional options may be required depending on your distro.
```
Xorg :10 -config xrdp/xorg.conf
```
## Contributing

First off, thanks for taking your time for improve xorgxrdp.

If you contribute to xorgxrdp, checkout **devel** branch and make changes to the branch. Please make pull requests also versus **devel** branch.

#!/bin/sh

# Test that Xorg can load the compiled modules

set -e

XORG=Xorg
moduledir=`$XORG -showDefaultModulePath 2>&1`
builddir=`git rev-parse --show-toplevel`

$XORG \
  -modulepath $builddir/module/.libs,$builddir/xrdpdev/.libs,$builddir/xrdpkeyb/.libs,$builddir/xrdpmouse/.libs,$moduledir \
  -config $builddir/xrdpdev/xorg.conf \
  -logfile xorgxrdp.log \
  -novtswitch -sharevts -noreset -ac :20

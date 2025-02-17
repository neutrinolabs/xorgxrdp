#!/bin/sh
################################################################################
# B U I L D   W I T H   L A T E S T   X O R G   A P T . S H
#
# Builds the latest version of the xserver from gitlab.freedesktop.org
#
# (c) Matt Burt  2025
#
# Usage ./build_with_latest_xorg_apt.sh [-r] <build_dir>
#
# Add [-r] to use an autoresume file. This can be useful when tracking down
# problems with the build script. It can also make things very confusing!
#
# sudo is used to install build dependencies, plus a few package
# dependencies.
#
# Most dependencies are pulled down and installed as the build user.
################################################################################

# ------------------------------------------------------------------------------
# G L O B A L S
# ------------------------------------------------------------------------------
# Build tools
APT_PKG_LIST="git pip pkg-config cmake automake autoconf libtool"
APT_PKG_LIST="$APT_PKG_LIST python3-venv" ; # meson/ninja
# Other OS-provided dependencies
#
# We pull in most of Mesa rather than building it from scratch as
# Mesa dependencies (notably the rust compiler version) can be
# problematic
APT_PKG_LIST="$APT_PKG_LIST mesa-common-dev" ; # See above
APT_PKG_LIST="$APT_PKG_LIST libfreetype-dev" ; # Needed by libXfont
APT_PKG_LIST="$APT_PKG_LIST libudev-dev" ; # Needed by xserver
APT_PKG_LIST="$APT_PKG_LIST libssl-dev" ; # Needed by xserver
APT_PKG_LIST="$APT_PKG_LIST libepoxy-dev" ; # Needed by xserver

# We use 'pip' to get the latest meson and ninja build tools down. These
# are installed for the build user.
PIP_PKG_LIST="meson ninja"

# The modular packages supported by the xserver build tool which we need to
# build. This includes the xserver package
#
# This list of packages is sorted into dependency order before use.
MODULAR_PKG_LIST="util/macros"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST font/util"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST proto/xorgproto proto/xcbproto"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST lib/libxcvt lib/libxtrans lib/libXau"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST lib/libXdmcp lib/libxcb lib/libX11"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST lib/libfontenc lib/libXfont lib/libxkbfile"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST lib/libxshmfence"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST mesa/drm"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST pixman/pixman"
MODULAR_PKG_LIST="$MODULAR_PKG_LIST xserver"

# Check the XDG_RUNTIME_DIR is specified, and use it for the autoresume file
if [ -z "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR=/run/user/$(id -u)
fi
AUTORESUME_FILE="$XDG_RUNTIME_DIR/xserver-autores.txt"

# Our source dir. This is currently hard-coded
SOURCE_DIR="$HOME/xserver-src"

# Dependencies in the source dir
BUILDER="$SOURCE_DIR/util/modular/build.sh" ; # xorg modular build script
MODFILE="$SOURCE_DIR/modfile.txt"           ; # Used by above
PYTHON_VENV="$SOURCE_DIR/python"            ; # Used for meson/ninja

# The build directory (set later)
BUILD_DIR=

# ------------------------------------------------------------------------------
# C R E A T E   B U I L D   M O D F I L E
#
# Creates the modfile used by utils/modular/build.sh on stdout
# Parameters:-
# 1..) List of modules to add to the file
#
# Return : != 0 for error
# The modules are sorted into dependency order before being added.
# ------------------------------------------------------------------------------
create_build_modfile()
{
    tmp=$(mktemp -t "modfile-XXXXXXXX")
    $BUILDER -L >$tmp
    while read mod; do
        newlist=
        for m in "$@"; do
            if [ "$m" = "$mod" ]; then
                echo $mod
            else
                newlist="$newlist $m"
            fi
        done
        set -- $newlist
        if [ $# -eq 0 ]; then
            break
        fi
    done <$tmp
    rm $tmp
    if [ $# -gt 0 ]; then
        echo "** The following modules are not supported by build.sh: $*" >&2
    fi
    return $#
}

# ------------------------------------------------------------------------------
# I N S T A L L   S O U R C E S
#
# Installs the sources used by the modular build utility
# Parameters:-
# 1..) List of modules to get source for
#
# Return : != 0 for error
# ------------------------------------------------------------------------------
install_sources()
{
    rv=0
    tmp=$(mktemp -t "modfile-XXXXXXXX")
    for mod in "$@"; do
        if ! [ -d "$mod" ]; then
            echo "$mod"
        fi
    done >$tmp
    if [ -s "$tmp" ]; then
        $BUILDER --modfile "$tmp" -a -m --clone "$BUILD_DIR"
        rv=$?

        # Most autoconf-enabled modules required an 'm4' directory. If
        # we fetch from git, these directories are only created if there
        # are not empty. If the directory isn't present, the module
        # builds can fail.
        #
        # Add m4 directories for autoconf-enabled modules
        for mod in "$@"; do
            if [ -f "$mod/configure.ac" ]; then
                mkdir -p "$mod/m4"
            fi
        done
    fi
    rm -f "$tmp"
    return $rv
}

# ------------------------------------------------------------------------------
# T I T L E
#
# Outputs a log title
# ------------------------------------------------------------------------------
title()
{
    echo
    echo '==============================================================================='
    echo $(date +%T)": $*"
    echo '==============================================================================='
}

# ------------------------------------------------------------------------------
# M A I N
# ------------------------------------------------------------------------------

# Check parameters
if [ "$1" = "-r" ]; then
    shift
else
    rm -f "$AUTORESUME_FILE"
fi

if [ $# != 1 ]; then
    echo "Usage : $0 [-r] <build-dir>" >&2
    exit 1
fi

# Set up the BUILD_DIR, after resolving it to an absolute path
mkdir -p "$1"
if ! cd "$1" || ! [ -w . ]; then
    echo "** Build directory $BUILD_DIR is invalid or unwriteable" >&2
    exit 1
fi
BUILD_DIR=$(pwd)

# Debian has an extra pkgconfig directory to consider in
# lib/$(uname -p)-linux-gnu/, which meson from pip doesn't pick up on.
# Rather than messing with PKG_CONFIG_PATH, we'll use a soft-link to make
# this work, so the resulting output directory can be more easily managed
debian_extra_pkgconf="$BUILD_DIR/lib/$(uname -p)-linux-gnu/pkgconfig"
if ! [ -L "$debian_extra_pkgconf" ]; then
    mkdir -p "${debian_extra_pkgconf%/*}"
    ln -sf ../pkgconfig "$debian_extra_pkgconf"
fi

# Set up the SOURCE_DIR, and work in it.
mkdir -p "$SOURCE_DIR"
cd "$SOURCE_DIR" || exit $?

# Install all dependencies
title "Installing dependencies"
sudo apt-get install -y $APT_PKG_LIST || exit $?
if ! [ -d "$PYTHON_VENV" ]; then
    python3 -m venv "$PYTHON_VENV" || exit $?
fi
PATH="$PYTHON_VENV/bin:$PATH"
pip3 install $PIP_PKG_LIST || exit $?

title "Installing modular build script"
if ! [ -x "$BUILDER" ]; then
    git clone https://gitlab.freedesktop.org/xorg/util/modular.git util/modular || exit $?
fi

title "Creating module file"
create_build_modfile $MODULAR_PKG_LIST >$MODFILE || exit $?

title "Installing sources"
install_sources $MODULAR_PKG_LIST || exit $?

# If there are meson builddir files without build.ninja files, delete them.
# This can happen if a previous build failed in the configure stage with
# missing dependencies.
title "Removing failed meson build directories"
for dir in $(find . -name builddir); do
    if [ ! -e $dir/build.ninja ]; then
        rm -rf $dir
    fi
done

title "Building sources"
$BUILDER \
    --modfile "$MODFILE" \
    --autoresume "$AUTORESUME_FILE" \
    "$BUILD_DIR"

#!/bin/sh

# Test that Xorg can load the compiled modules

# X server to run
if [ -z "$XORG" ]; then
    if [ -x /usr/lib/xorg/Xorg ]; then
        # Don't use the Ubuntu wrapped server
        XORG=/usr/lib/xorg/Xorg
    else
        XORG=Xorg
    fi
fi

# Client to connect to Xorg
: ${XCLIENT=xdpyinfo}

# Build directory where modules are located
: ${top_builddir=..}
top_builddir=`cd $top_builddir >/dev/null; pwd`

# Source directory where xorg.conf is located
: ${srcdir=`dirname $0`}
top_srcdir=`cd $srcdir/.. >/dev/null; pwd`

# Test name
: ${TESTNAME=test}

# Output files
: ${XORG_LOG=$TESTNAME-xorg.log}
: ${XORG_OUT=$TESTNAME-xorg-out.log}
: ${XORG_ERR=$TESTNAME-xorg-err.log}
: ${XCLIENT_OUT=$TESTNAME-xclient-out.log}

# Display number
: ${TEST_DISPLAY=:20}

# Timeout in seconds
: ${XORG_START_TIMEOUT=3}

# Command line arguments are passed to Xorg
XORG_ARGS="$@"


# If the X server has setuid bit, make a local copy
XORG_FULL=`command -v $XORG`
if test -u $XORG_FULL; then
  XORG=`pwd`/Xorg.no-setuid
  echo "$XORG_FULL has setuid bit set, will use $XORG"
  if ! test -e $XORG; then
    echo "Copying $XORG_FULL to $XORG"
    cp $XORG_FULL $XORG
    chmod 755 $XORG
  fi
fi

# Find Xorg module path
moduledir=`$XORG -showDefaultModulePath 2>&1`
echo "Module directory: $moduledir"

# Building the module path to include compiled modules
moduledir="$top_builddir/module/.libs,$moduledir"
moduledir="$top_builddir/xrdpdev/.libs,$moduledir"
moduledir="$top_builddir/xrdpkeyb/.libs,$moduledir"
moduledir="$top_builddir/xrdpmouse/.libs,$moduledir"

# Run Xorg with compiled modules as a background task
#
# [Linux]   Set LD_BIND_NOW (see dlopen(3)) to disable lazy symbol
#           resolution so we check the modules for undefined symbols.
# [FreeBSD] Setting LD_BIND_NOW does not affect the operation of
#           dlopen()
LD_BIND_NOW=1 \
$XORG \
  -modulepath $moduledir \
  -config $top_srcdir/xrdpdev/xorg.conf \
  -logfile $XORG_LOG \
  -novtswitch -sharevts -once -terminate -ac \
  $TEST_DISPLAY $XORG_ARGS >$XORG_OUT 2>$XORG_ERR </dev/null &

# Record Xorg PID so it can be killed
XORG_PID=$!

# Wait for Xorg to start
echo "Waiting ${XORG_START_TIMEOUT} seconds for Xorg to start"
sleep $XORG_START_TIMEOUT

# Test with an X client
echo "Running X client"
$XCLIENT -display $TEST_DISPLAY >$XCLIENT_OUT
CLIENT_RET=$?
echo "Client error code: $CLIENT_RET"

# Xorg would not exit if the client did not connect to it
if test $CLIENT_RET != 0; then
  echo "Killing X server (PID $XORG_PID)"
  kill $XORG_PID
  echo "Test failed"
  exit 1
fi

# Wait for Xorg to stop
wait $XORG_PID
XORG_RET=$?
echo "Xorg error code: $XORG_RET"

# Succeed if Xorg returned code 0
if test $XORG_RET = 0; then
  echo "Test successful"
  exit 0
else
  echo "Test failed"
  exit 1
fi

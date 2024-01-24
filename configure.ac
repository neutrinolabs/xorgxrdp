# Process this file with autoconf to produce a configure script

AC_PREREQ(2.65)
# package version must be x.y.z
AC_INIT([xorgxrdp], [0.9.80], [xrdp-devel@googlegroups.com])
package_version_major=$(echo ${PACKAGE_VERSION}|cut -d. -f1)
package_version_minor=$(echo ${PACKAGE_VERSION}|cut -d. -f2)
package_version_patchlevel=$(echo ${PACKAGE_VERSION}|cut -d. -f3)
AC_SUBST([package_version_major], [${package_version_major}])
AC_SUBST([package_version_minor], [${package_version_minor}])
AC_SUBST([package_version_patchlevel], [${package_version_patchlevel}])
AC_CONFIG_HEADERS(config_ac.h:config_ac-h.in)
AM_INIT_AUTOMAKE([1.11 foreign parallel-tests])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_PROG_CC_C99
AC_C_CONST
AC_PROG_LIBTOOL

case $host_os in
        *linux*)
                linux=yes
                ;;
        *kfreebsd*)
                linux=yes # only used in instfiles/ so that’s ok for us for now
                ;;
        *freebsd*)
                freebsd=yes
                ;;
        *netbsd*)
                netbsd=yes
                ;;
        *openbsd*)
                openbsd=yes
                ;;
        *darwin*)
                macos=yes
                ;;
        *solaris*)
                solaris=yes
                ;;
esac

AM_CONDITIONAL(LINUX, [test "x$linux" = xyes])
AM_CONDITIONAL(FREEBSD, [test "x$freebsd" = xyes])
AM_CONDITIONAL(OPENBSD, [test "x$openbsd" = xyes])
AM_CONDITIONAL(NETBSD, [test "x$netbsd" = xyes])
AM_CONDITIONAL(MACOS, [test "x$macos" = xyes])
AM_CONDITIONAL(SOLARIS, [test "x$solaris" = xyes])

AC_CONFIG_MACRO_DIR([m4])
AX_CFLAGS_WARN_ALL
AX_APPEND_COMPILE_FLAGS([-Wwrite-strings])
AX_GCC_FUNC_ATTRIBUTE([format])

PKG_CHECK_MODULES([XORG_SERVER], [xorg-server >= 0], [],
  [AC_MSG_ERROR([please install xserver-xorg-dev, xorg-x11-server-sdk or xorg-x11-server-devel])])
if test "x${enable_glamor}" = "xyes"; then
  PKG_CHECK_MODULES([XORG_SERVER_GLAMOR], [xorg-server >= 1.19.0])
  PKG_CHECK_MODULES([XORG_SERVER_GBM], [gbm >= 0], [], [AC_MSG_ERROR([please install libgbm-dev or mesa-libgbm-devel])])
  PKG_CHECK_MODULES([XORG_SERVER_EPOXY], [epoxy >= 0], [], [AC_MSG_ERROR([please install libepoxy-dev or libepoxy-devel])])
  PKG_CHECK_MODULES([XORG_SERVER_EGL], [egl >= 0], [], [AC_MSG_ERROR([please install libegl1-mesa-dev or mesa-libEGL-devel])])
fi

if test x$solaris = xyes; then
    CFLAGS="${CFLAGS} -D_XOPEN_SOURCE=600"
    AC_SUBST(CFLAGS)
fi 

if test "x$XRDP_CFLAGS" = "x"; then
  PKG_CHECK_MODULES([XRDP], [xrdp >= 0.9.80])
  XRDP_CFLAGS=`pkg-config xrdp --cflags`
fi

AC_ARG_ENABLE([strict-locations],
  [AS_HELP_STRING([--enable-strict-locations], [Use standard installation directories])])

if test "x$enable_strict_locations" = "xyes"; then
  moduledir='${libdir}/xorg/modules'
else
  moduledir=`pkg-config xorg-server --variable=moduledir`
  sysconfdir="/etc"
fi

AC_SUBST([moduledir])

# SIMD is optional
AC_ARG_WITH([simd],
    AC_HELP_STRING([--without-simd],[Omit SIMD extensions.]))
if test "x${with_simd}" != "xno"; then
  # Check if we're on a supported CPU
  AC_MSG_CHECKING([if we have SIMD optimisations for cpu type])
  case "$host_cpu" in
    x86_64 | amd64)
      AC_MSG_RESULT([yes (x86_64)])
      AC_PROG_NASM
      simd_arch=x86_64
    ;;
    i*86 | x86 | ia32)
      AC_MSG_RESULT([yes (i386)])
      AC_PROG_NASM
      simd_arch=i386
    ;;
    *)
      AC_MSG_RESULT([no ("$host_cpu")])
      AC_MSG_WARN([SIMD support not available for this CPU.  Performance will suffer.])
      with_simd=no;
    ;;
  esac
  if test "x${with_simd}" != "xno"; then
    AC_DEFINE([WITH_SIMD], [1], [Use accelerated SIMD routines.])
  fi
fi

# shm_open may not be in the C library
AC_SEARCH_LIBS([shm_open], [rt])

AC_ARG_ENABLE(glamor, AS_HELP_STRING([--enable-glamor],
              [Use glamor(requires xorg server 1.19+) (default: no)]),
              [], [enable_glamor=no])
AM_CONDITIONAL(WITH_GLAMOR, [test x$enable_glamor = xyes])

AM_CONDITIONAL(WITH_SIMD_AMD64, [test x$simd_arch = xx86_64])
AM_CONDITIONAL(WITH_SIMD_X86, [test x$simd_arch = xi386])

AC_CONFIG_FILES([Makefile
                 module/Makefile
                 module/amd64/Makefile
                 module/x86/Makefile
                 tests/Makefile
                 tests/yuv2rgb/Makefile
                 xrdpdev/Makefile
                 xrdpkeyb/Makefile
                 xrdpmouse/Makefile
])

AC_OUTPUT


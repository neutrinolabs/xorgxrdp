EXTRA_DIST =
EXTRA_FLAGS =

if WITH_GLAMOR
EXTRA_FLAGS += -DXORGXRDP_GLAMOR -DGLAMOR_FOR_XORG
endif

AM_CFLAGS = \
  $(XORG_SERVER_CFLAGS) \
  $(XRDP_CFLAGS) \
  -DPACKAGE_VERSION_MAJOR=@package_version_major@ \
  -DPACKAGE_VERSION_MINOR=@package_version_minor@ \
  -DPACKAGE_VERSION_PATCHLEVEL=@package_version_patchlevel@ \
  -I$(top_srcdir)/module \
  $(EXTRA_FLAGS)

xrdpdev_drv_la_LTLIBRARIES = xrdpdev_drv.la

xrdpdev_drv_la_LDFLAGS = -module -avoid-version

xrdpdev_drv_ladir = $(moduledir)/drivers

xrdpdev_drv_la_SOURCES = xrdpdev.c

xrdpdev_drv_la_LIBADD =

xrdpdevsysconfdir=$(sysconfdir)/X11/xrdp

dist_xrdpdevsysconf_DATA = \
  xorg.conf


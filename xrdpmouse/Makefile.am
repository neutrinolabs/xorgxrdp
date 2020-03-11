EXTRA_DIST =

AM_CFLAGS = \
  $(XRDP_CFLAGS) \
  $(XORG_SERVER_CFLAGS) \
  -DPACKAGE_VERSION_MAJOR=@package_version_major@ \
  -DPACKAGE_VERSION_MINOR=@package_version_minor@ \
  -DPACKAGE_VERSION_PATCHLEVEL=@package_version_patchlevel@ \
  -I$(top_srcdir)/module

xrdpmouse_drv_la_LTLIBRARIES = xrdpmouse_drv.la

xrdpmouse_drv_la_LDFLAGS = -module -avoid-version

xrdpmouse_drv_ladir = $(moduledir)/input

xrdpmouse_drv_la_SOURCES = rdpMouse.c

xrdpmouse_drv_la_LIBADD =

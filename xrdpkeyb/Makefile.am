EXTRA_DIST =

AM_CFLAGS = \
  $(XORG_SERVER_CFLAGS) \
  $(XRDP_CFLAGS) \
  -DPACKAGE_VERSION_MAJOR=@package_version_major@ \
  -DPACKAGE_VERSION_MINOR=@package_version_minor@ \
  -DPACKAGE_VERSION_PATCHLEVEL=@package_version_patchlevel@ \
  -I$(top_srcdir)/module

xrdpkeyb_drv_la_LTLIBRARIES = xrdpkeyb_drv.la

xrdpkeyb_drv_la_LDFLAGS = -module -avoid-version

xrdpkeyb_drv_ladir = $(moduledir)/input

xrdpkeyb_drv_la_SOURCES = rdpKeyboard.c

xrdpkeyb_drv_la_LIBADD =

EXTRA_DIST = common.asm nasm_lt.sh
EXTRA_FLAGS =
SUBDIRS =
ASMLIB =

if WITH_SIMD_AMD64
EXTRA_FLAGS += -DSIMD_USE_ACCEL=1
SUBDIRS += amd64
ASMLIB += amd64/libxorgxrdp-asm.la
endif

if WITH_SIMD_X86
EXTRA_FLAGS += -DSIMD_USE_ACCEL=1
SUBDIRS += x86
ASMLIB += x86/libxorgxrdp-asm.la
endif

AM_CFLAGS = \
  $(XORG_SERVER_CFLAGS) \
  $(XRDP_CFLAGS) \
  -I$(top_srcdir)/module \
  $(EXTRA_FLAGS)

noinst_HEADERS = \
  rdpCapture.h \
  rdpClientCon.h \
  rdpComposite.h \
  rdpCopyArea.h \
  rdpCopyPlane.h \
  rdpCursor.h \
  rdpDraw.h \
  rdpFillPolygon.h \
  rdpFillSpans.h \
  rdpGC.h \
  rdpGlyphs.h \
  rdp.h \
  rdpImageGlyphBlt.h \
  rdpImageText16.h \
  rdpImageText8.h \
  rdpInput.h \
  rdpMain.h \
  rdpMisc.h \
  rdpPixmap.h \
  rdpPolyArc.h \
  rdpPolyFillArc.h \
  rdpPolyFillRect.h \
  rdpPolyGlyphBlt.h \
  rdpPolylines.h \
  rdpPolyPoint.h \
  rdpPolyRectangle.h \
  rdpPolySegment.h \
  rdpPolyText16.h \
  rdpPolyText8.h \
  rdpPri.h \
  rdpPushPixels.h \
  rdpPutImage.h \
  rdpRandR.h \
  rdpReg.h \
  rdpSetSpans.h \
  rdpSimd.h \
  rdpTrapezoids.h \
  rdpXv.h \
  amd64/funcs_amd64.h \
  x86/funcs_x86.h

libxorgxrdp_la_LTLIBRARIES = libxorgxrdp.la

libxorgxrdp_la_LDFLAGS = -module -avoid-version

libxorgxrdp_ladir = $(moduledir)

libxorgxrdp_la_SOURCES = rdpDraw.c rdpPri.c rdpGC.c rdpFillSpans.c \
rdpSetSpans.c rdpPutImage.c rdpCopyArea.c rdpCopyPlane.c rdpPolyPoint.c \
rdpPolylines.c rdpPolySegment.c rdpPolyRectangle.c rdpPolyArc.c \
rdpFillPolygon.c rdpPolyFillRect.c rdpPolyFillArc.c rdpPolyText8.c \
rdpPolyText16.c rdpImageText8.c rdpImageText16.c rdpImageGlyphBlt.c \
rdpPolyGlyphBlt.c rdpPushPixels.c rdpCursor.c rdpMain.c rdpRandR.c \
rdpMisc.c rdpReg.c rdpComposite.c rdpGlyphs.c rdpPixmap.c rdpInput.c \
rdpClientCon.c rdpCapture.c rdpTrapezoids.c rdpXv.c rdpSimd.c

libxorgxrdp_la_LIBADD = $(ASMLIB)

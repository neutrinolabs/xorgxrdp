EXTRA_DIST = nasm_lt.sh

EXTRA_SOURCES =
EXTRA_FLAGS =

if WITH_SIMD_AMD64
EXTRA_SOURCES += amd64/cpuid_amd64.asm \
  amd64/yv12_to_rgb32_amd64_sse2.asm \
  amd64/i420_to_rgb32_amd64_sse2.asm \
  amd64/yuy2_to_rgb32_amd64_sse2.asm \
  amd64/uyvy_to_rgb32_amd64_sse2.asm \
  amd64/a8r8g8b8_to_a8b8g8r8_box_amd64_sse2.asm \
  amd64/a8r8g8b8_to_nv12_box_amd64_sse2.asm
EXTRA_FLAGS += -DSIMD_USE_ACCEL=1
endif

if WITH_SIMD_X86
EXTRA_SOURCES += x86/cpuid_x86.asm \
  x86/yv12_to_rgb32_x86_sse2.asm \
  x86/i420_to_rgb32_x86_sse2.asm \
  x86/yuy2_to_rgb32_x86_sse2.asm \
  x86/uyvy_to_rgb32_x86_sse2.asm \
  x86/a8r8g8b8_to_a8b8g8r8_box_x86_sse2.asm \
  x86/a8r8g8b8_to_nv12_box_x86_sse2.asm
EXTRA_FLAGS += -DSIMD_USE_ACCEL=1
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
rdpClientCon.c rdpCapture.c rdpTrapezoids.c rdpXv.c rdpSimd.c \
$(EXTRA_SOURCES)

nasm_verbose = $(nasm_verbose_@AM_V@)
nasm_verbose_ = $(nasm_verbose_@AM_DEFAULT_V@)
nasm_verbose_0 = @echo "  NASM     $@";

.asm.lo:
	$(nasm_verbose)$(LIBTOOL) $(AM_V_lt) --mode=compile \
	  $(srcdir)/nasm_lt.sh $(NASM) $(NAFLAGS) -I$(srcdir) -I. $< -o $@

libxorgxrdp_la_LIBADD =

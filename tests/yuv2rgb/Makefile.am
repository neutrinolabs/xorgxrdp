AM_CFLAGS =

if WITH_SIMD_AMD64
AM_CFLAGS += -DUSE_SIMD_AMD64
ASMLIB = $(top_builddir)/module/amd64/libxorgxrdp-asm.la
endif

if WITH_SIMD_X86
AM_CFLAGS += -DUSE_SIMD_X86
ASMLIB = $(top_builddir)/module/x86/libxorgxrdp-asm.la
endif

check_PROGRAMS = yuv2rgb_speed

yuv2rgb_speed_SOURCES = yuv2rgb_speed.c

yuv2rgb_speed_LDADD = $(ASMLIB)

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

TESTS = yuv2rgb_speed.sh

dist_check_SCRIPTS = $(TESTS)

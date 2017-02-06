AM_CFLAGS =

if WITH_SIMD_AMD64
AM_CFLAGS += -DUSE_SIMD_AMD64
ASMOBJ = $(top_builddir)/module/amd64/a8r8g8b8_to_nv12_box_amd64_sse2.lo
endif

if WITH_SIMD_X86
AM_CFLAGS += -DUSE_SIMD_X86
ASMOBJ = $(top_builddir)/module/x86/a8r8g8b8_to_nv12_box_x86_sse2.lo
endif

check_PROGRAMS = yuv2rgb_speed

yuv2rgb_speed_SOURCES = yuv2rgb_speed.c

yuv2rgb_speed_LDADD = $(ASMOBJ)

TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

TESTS = yuv2rgb_speed.sh

dist_check_SCRIPTS = $(TESTS)

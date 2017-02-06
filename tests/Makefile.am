TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

TESTS = xorg-test-run.sh

dist_check_SCRIPTS = $(TESTS)

CLEANFILES = *.log *.log.old Xorg.no-setuid

SUBDIRS =

if WITH_SIMD_AMD64
  SUBDIRS += yuv2rgb
endif

if WITH_SIMD_X86
  SUBDIRS += yuv2rgb
endif

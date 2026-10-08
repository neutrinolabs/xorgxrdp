TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)

AM_CPPFLAGS = -I$(top_srcdir)/module $(PIXMAN_CFLAGS)

check_PROGRAMS = transport_test region-coalesce-test
transport_test_SOURCES = transport_test.c $(top_srcdir)/module/rdpTransport.c
region_coalesce_test_SOURCES = region-coalesce-test.c $(top_srcdir)/module/rdpCoalesce.c
region_coalesce_test_LDADD = $(PIXMAN_LIBS)

TESTS = xorg-test-run.sh $(check_PROGRAMS)

dist_check_SCRIPTS = xorg-test-run.sh

EXTRA_DIST = \
  performance/README.md \
  performance/RESULTS.md \
  performance/results.json \
  performance/run.py \
  performance/report.py \
  performance/convert_bench.c \
  performance/capture_extract.py \
  performance/capture_bench.c \
  performance/client_info.c \
  performance/live_bench.py

EXTRA_DIST += \
  performance/RDP_E2E.md \
  performance/RDP_RESULTS.md \
  performance/rdp-e2e-results.json \
  performance/desktop_workload.md \
  performance/desktop_workload.c \
  performance/rdp_client_bench.c \
  performance/prepare_rdp_server.py \
  performance/rdp_e2e.py \
  performance/rdp_e2e_report.py

CLEANFILES = *.log *.log.old Xorg.no-setuid

SUBDIRS =

if WITH_SIMD_AMD64
  SUBDIRS += yuv2rgb
endif

if WITH_SIMD_X86
  SUBDIRS += yuv2rgb
endif

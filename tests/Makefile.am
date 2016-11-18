EXTRA_DIST = yuv2rgb

TESTS_ENVIRONMENT = top_builddir=$(top_builddir) top_srcdir=$(top_srcdir) $(SHELL)

TESTS = xorg-test-run.sh

dist_check_SCRIPTS = $(TESTS)

CLEANFILES = *.log *.log.old Xorg.no-setuid

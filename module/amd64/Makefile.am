NAFLAGS += -DASM_ARCH_AMD64

ASMSOURCES = \
  a8r8g8b8_to_a8b8g8r8_box_amd64_sse2.asm \
  a8r8g8b8_to_nv12_box_amd64_sse2.asm \
  cpuid_amd64.asm \
  i420_to_rgb32_amd64_sse2.asm \
  uyvy_to_rgb32_amd64_sse2.asm \
  yuy2_to_rgb32_amd64_sse2.asm \
  yv12_to_rgb32_amd64_sse2.asm

noinst_LTLIBRARIES = libxorgxrdp-asm.la

libxorgxrdp_asm_la_SOURCES = \
  funcs_amd64.h \
  $(ASMSOURCES)

nasm_verbose = $(nasm_verbose_@AM_V@)
nasm_verbose_ = $(nasm_verbose_@AM_DEFAULT_V@)
nasm_verbose_0 = @echo "  NASM     $@";

.asm.lo:
	$(nasm_verbose)$(LIBTOOL) $(AM_V_lt) --mode=compile \
	  $(top_srcdir)/module/nasm_lt.sh $(NASM) $(NAFLAGS) \
	  -I$(top_srcdir)/module $< -o $@

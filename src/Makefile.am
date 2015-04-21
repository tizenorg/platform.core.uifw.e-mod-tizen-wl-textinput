MAINTAINERCLEANFILES = Makefile.in
MODULE = e-mod-tizen-wl-textinput

# data files for the module
filesdir = $(libdir)/enlightenment/modules/$(MODULE)

pkgdir                 = $(libdir)/enlightenment/modules/$(MODULE)/$(MODULE_ARCH)
pkg_LTLIBRARIES        = module.la
module_la_SOURCES      = e_mod_main.c \
                         e_mod_input_panel.c \
                         input-method-protocol.c \
                         text-protocol.c
module_la_LIBADD       =
module_la_CFLAGS       = @WAYLAND_CFLAGS@ @ENLIGHTENMENT_CFLAGS@ -DHAVE_WAYLAND_ONLY
module_la_LDFLAGS      = -module -avoid-version @WAYLAND_LIBS@ @ENLIGHTENMENT_LIBS@
module_la_DEPENDENCIES = $(top_builddir)/config.h

BUILT_SOURCES = input-method-protocol.c \
                input-method-protocol.h \
                text-protocol.c \
                text-protocol.h

CLEANFILES = $(BUILT_SOURCES)

%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

%-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) server-header < $< > $@

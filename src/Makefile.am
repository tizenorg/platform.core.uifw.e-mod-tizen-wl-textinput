MAINTAINERCLEANFILES = Makefile.in
MODULE = e-mod-tizen-wl-textinput

# data files for the module
filesdir = $(libdir)/enlightenment/modules/$(MODULE)

pkgdir                 = $(libdir)/enlightenment/modules/$(MODULE)/$(MODULE_ARCH)
pkg_LTLIBRARIES        = module.la
module_la_SOURCES      = e_mod_main.c \
                         e_mod_input_panel.c \
                         wti_log.c
module_la_LIBADD       =
module_la_CFLAGS       = @WAYLAND_CFLAGS@ @ENLIGHTENMENT_CFLAGS@ @EEZE_CFLAGS@ @VCONF_CFLAGS@ -DHAVE_WAYLAND_ONLY -DHAVE_WAYLAND
module_la_LDFLAGS      = -module -avoid-version @WAYLAND_LIBS@ @ENLIGHTENMENT_LIBS@ @EEZE_LIBS@ @VCONF_LIBS@
module_la_DEPENDENCIES = $(top_builddir)/config.h

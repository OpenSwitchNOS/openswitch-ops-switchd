# Copyright (C) 2009, 2010, 2011, 2012, 2014 Nicira, Inc.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without warranty of any kind.
if OPS
ovspluginslibincludedir = $(includedir)/ovs
ovspluginslibinclude_HEADERS = \
	plugins/plugins.h \
	plugins/plugin-extensions.h \
	plugins/reconfigure-blocks.h
endif

lib_LTLIBRARIES += plugins/libplugins.la
plugins_libplugins_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
        -Wl,--version-script=$(top_builddir)/plugins/libplugins.sym \
        $(AM_LDFLAGS)

plugins_libplugins_la_LIBADD = $(YAML_LIBS)

plugins_libplugins_la_SOURCES = \
	plugins/plugins.c \
	plugins/plugins.h \
	plugins/plugins_yaml.c \
	plugins/plugins_yaml.h \
	plugins/plugin-extensions.c \
	plugins/plugin-extensions.h \
	plugins/reconfigure-blocks.c \
	plugins/reconfigure-blocks.h

plugins_libplugins_la_CFLAGS = -DYAML_PATH=$(sysconfdir)/openswitch/platform

plugins_libplugins_la_CPPFLAGS = $(AM_CPPFLAGS)
plugins_libplugins_la_CFLAGS += $(AM_CFLAGS)

pkgconfig_DATA += \
	$(srcdir)/plugins/libplugins.pc

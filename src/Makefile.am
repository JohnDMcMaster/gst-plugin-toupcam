# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgsttoupcamsrc.la


# XXX: SDK_BRANDING

TOUPCAM_CFLAGS = -I/opt/toupcamsdk/inc -Werror
TOUPCAM_LIBS = -ltoupcam

# TOUPCAM_CFLAGS = -I/opt/amcamsdk/inc -Werror
# TOUPCAM_LIBS = -lamcam

# TOUPCAM_CFLAGS = -I/opt/nncamsdk/inc -Werror
# TOUPCAM_LIBS = -lnncam

# TOUPCAM_CFLAGS = -I/opt/swiftcamsdk/inc -Werror
# TOUPCAM_LIBS = -lswiftcam


# sources used to compile this plug-in
libgsttoupcamsrc_la_SOURCES = gsttoupcamsrc.c gsttoupcamsrc.h gstplugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgsttoupcamsrc_la_CFLAGS = $(GST_CFLAGS) $(TOUPCAM_CFLAGS)
libgsttoupcamsrc_la_LIBADD = $(GST_LIBS) $(TOUPCAM_LIBS) -lgstvideo-1.0
libgsttoupcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttoupcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gsttoupcamsrc.h

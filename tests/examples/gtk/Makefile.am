
SUBDIRS = . gtkvideooverlay filternovideooverlay filtervideooverlay fxtest switchvideooverlay


noinst_LTLIBRARIES = libgstgtkhelper.la

libgstgtkhelper_la_SOURCES = gstgtk.c gstgtk.h
libgstgtkhelper_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK3_CFLAGS)

if HAVE_WINDOW_COCOA
libgstgtkhelper_la_CFLAGS += -x objective-c
endif


sbin_PROGRAMS = gooroom-greeter

BUILT_SOURCES = \
	greeter-resources.c \
	greeter-resources.h

gooroom_greeter_SOURCES = \
	$(BUILT_SOURCES) \
	indicator-button.h \
	indicator-button.c \
	gooroom-greeter.c \
	greeterconfiguration.c \
	greeterconfiguration.h \
	greeterbackground.c \
	greeterbackground.h \
	greeter-window.h \
	greeter-window.c \
	splash-window.h \
	splash-window.c \
	greeter-password-settings-dialog.h \
	greeter-password-settings-dialog.c \
	greeter-message-dialog.h \
	greeter-message-dialog.c

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DHAVE_CONFIG_H \
	-DLOCALEDIR=\"$(localedir)\" \
	-DPKGDATA_DIR=\"$(pkgdatadir)\" \
	-DCONFIG_FILE=\"$(sysconfdir)/lightdm/gooroom-greeter.conf\" \
	-DINDICATOR_DIR=\"$(INDICATORDIR)\" \
	-DGOOROOM_SPLASH=\"$(libdir)/gooroom-splash/gooroom-splash\" \
	-DGOOROOM_NOTIFYD=\"$(libdir)/gooroom-notifyd/gooroom-notifyd\" \
	$(WARN_CFLAGS)

gooroom_greeter_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GMODULE_CFLAGS) \
	$(UPOWER_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(LIGHTDMGOBJECT_CFLAGS) \
	$(LIBX11_CFLAGS) \
    $(AYATANA_INDICATOR_NG_CFLAGS)

gooroom_greeter_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GMODULE_LIBS) \
	$(UPOWER_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIGHTDMGOBJECT_LIBS) \
	$(LIBX11_LIBS) \
	$(AYATANA_INDICATOR_NG_LIBS) \
	-lm

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)
greeter-resources.c: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name greeter $<
greeter-resources.h: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name greeter $<

DISTCLEANFILES = \
	$(BUILT_SOURCES)

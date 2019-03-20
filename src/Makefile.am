sbin_PROGRAMS = gooroom-greeter

gooroom_greeter_built_sources = \
	gooroom-greeter-ui.h

gooroom_greeter_SOURCES = \
	$(gooroom_greeter_built_sources) \
	indicator-button.h	\
	indicator-button.c	\
	gooroom-greeter.c \
	greeterbackground.c \
	greeterbackground.h \
	greeterconfiguration.c \
	greeterconfiguration.h

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DHAVE_CONFIG_H \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DPKGDATA_DIR=\""$(datadir)/gooroom-greeter"\" \
	-DCONFIG_FILE=\""$(sysconfdir)/lightdm/gooroom-greeter.conf"\" \
	-DINDICATOR_DIR=\""$(INDICATORDIR)"\"	\
	$(WARN_CFLAGS)

gooroom_greeter_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GMODULE_CFLAGS) \
	$(UPOWER_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(LIGHTDMGOBJECT_CFLAGS) \
	$(INDICATOR_CFLAGS) \
	$(LIBX11_CFLAGS)

gooroom_greeter_LDADD = \
	$(GTK_LIBS) \
	$(GMODULE_LIBS) \
	$(UPOWER_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIGHTDMGOBJECT_LIBS) \
	$(INDICATOR_LIBS) \
	$(LIBX11_LIBS) \
	-lm


gooroom-greeter-ui.h: $(srcdir)/gooroom-greeter.glade Makefile
	$(AM_V_GEN) exo-csource --static --strip-comments --strip-content --name=gooroom_greeter_ui $< >$@

DISTCLEANFILES = \
	$(gooroom_greeter_built_sources)

BUILT_SOURCES = \
	$(gooroom_greeter_built_sources)



EXTRA_DIST = \
	gooroom-greeter.glade

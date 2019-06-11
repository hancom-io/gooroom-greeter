pkglibexec_SCRIPTS = chromium-pkexec chromium-killall

sbin_PROGRAMS = gooroom-greeter

BUILT_SOURCES = \
	greeter-resources.c \
	greeter-resources.h


#gooroom_greeter_built_sources = \
#	gooroom-greeter-ui.h

gooroom_greeter_SOURCES = \
	$(BUILT_SOURCES) \
	indicator-button.h	\
	indicator-button.c	\
	gooroom-greeter.c \
	greeterbackground.c \
	greeterbackground.h \
	greeterconfiguration.c \
	greeterconfiguration.h

#	$(gooroom_greeter_built_sources)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DHAVE_CONFIG_H \
	-DLOCALEDIR=\"$(localedir)\" \
	-DPKGDATA_DIR=\"$(pkgdatadir)\" \
	-DCONFIG_FILE=\"$(sysconfdir)/lightdm/gooroom-greeter.conf\" \
	-DINDICATOR_DIR=\"$(INDICATORDIR)\"	\
	-DCHROMIUM_PKEXEC=\"$(pkglibexecdir)/chromium-pkexec\" \
	-DCHROMIUM_KILLALL=\"$(pkglibexecdir)/chromium-killall\" \
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
	$(INDICATOR_CFLAGS) \
	$(LIBX11_CFLAGS) \
    $(AYATANA_INDICATOR_NG_CFLAGS) \
	$(JSON_GLIB_CFLAGS)

gooroom_greeter_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GMODULE_LIBS) \
	$(UPOWER_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIGHTDMGOBJECT_LIBS) \
	$(INDICATOR_LIBS) \
	$(LIBX11_LIBS) \
	$(JSON_GLIB_LIBS) \
	$(AYATANA_INDICATOR_NG_LIBS) \
	-lm

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)
greeter-resources.c: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name greeter $<
greeter-resources.h: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name greeter $<

#gooroom-greeter-ui.h: $(srcdir)/gooroom-greeter.glade Makefile
#	$(AM_V_GEN) exo-csource --static --strip-comments --strip-content --name=gooroom_greeter_ui $< >$@

@INTLTOOL_POLICY_RULE@
polkitdir = $(datadir)/polkit-1/actions
polkit_in_files = kr.gooroom.greeter.policy.in
polkit_DATA = $(polkit_in_files:.policy.in=.policy)

CLEANFILES = \
	kr.gooroom.greeter.policy

DISTCLEANFILES = \
	$(BUILT_SOURCES)
#	$(gooroom_greeter_built_sources)

#BUILT_SOURCES = \
#	$(gooroom_greeter_built_sources)

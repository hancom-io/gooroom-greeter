AC_INIT(gooroom-greeter, 0.1.0)
AC_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR(m4)
AM_INIT_AUTOMAKE([no-dist-gzip dist-xz foreign])
LT_INIT
AM_PROG_CC_C_O
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

GOBJECT_INTROSPECTION_CHECK(0.9.5)

GNOME_COMPILE_WARNINGS(maximum)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

PKG_CHECK_MODULES([GTK], [gtk+-3.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-export-2.0])
PKG_CHECK_MODULES([UPOWER], [upower-glib >= 0.99.4])
PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.19.2],
  [AC_DEFINE([HAVE_LIBLIGHTDMGOBJECT_1_19_2], [1], [Building with liblightdmgobject 1.19.2])],
  [PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.3.5])]
)
PKG_CHECK_MODULES([LIBX11], [x11])

INDICATOR_PKGNAME=indicator3-0.4
PKG_CHECK_MODULES([INDICATOR], [${INDICATOR_PKGNAME}], [12.10.1])

AC_CHECK_PROG([HAVE_EXO_CSOURCE], [exo-csource], [yes])
if test x"$HAVE_EXO_CSOURCE" != xyes; then
  AC_MSG_FAILURE([exo-csource not installed])
fi

dnl ***********************
dnl *** Indicator info ***
dnl ***********************
INDICATORDIR=`$PKG_CONFIG --variable=indicatordir ${INDICATOR_PKGNAME}`
AC_SUBST(INDICATORDIR)


dnl ###########################################################################
AC_ARG_ENABLE([kill-on-sigterm],
    AC_HELP_STRING([--enable-kill-on-sigterm], [Kill greeter instance on SIGTERM, see LP1445461])
    AC_HELP_STRING([--disable-kill-on-sigterm], [Don't kill greeter on SIGTERM, see LP1445461']),
            [], [enable_kill_on_sigterm=no])

AS_IF([test "x$enable_kill_on_sigterm" = "xyes"],
[
    AC_DEFINE([KILL_ON_SIGTERM], [1], [Kill greeter in SIGTERM handler, see LP1445461])
],
[])

dnl ###########################################################################
dnl Internationalization
dnl ###########################################################################

IT_PROG_INTLTOOL(0.35.0)
AC_SUBST(GETTEXT_PACKAGE, gooroom-greeter)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", Gettext package)

AC_CONFIG_FILES([
Makefile
data/Makefile
data/icons/Makefile
data/images/Makefile
data/themes/Makefile
po/Makefile.in
src/Makefile
])
AC_OUTPUT

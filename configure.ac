AC_INIT([spiceglue], [2.2], [devel@flexvdi.com], [spiceglue], [http://flexvdi.com])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_MACRO_DIRS([m4])

LT_INIT([win32-dll])

AC_PROG_CC

PKG_CHECK_MODULES([GLIB], [glib-2.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES([SPICEGLIB], [spice-client-glib-2.0])
AC_SUBST(SPICEGLIB_CFLAGS)
AC_SUBST(SPICEGLIB_LIBS)

AC_ARG_ENABLE([printing],
    AS_HELP_STRING([--disable-printing], [Disable flexVDI follow-me printing support]))
 
AS_IF([test "x$enable_printing" != "xno"], [
    AC_DEFINE(PRINTING)
	enable_printing="yes"
	PKG_CHECK_MODULES([FLEXVDI_SPICE_CLIENT], [flexvdi-spice-client])
	AC_SUBST(FLEXVDI_SPICE_CLIENT_CFLAGS)
	AC_SUBST(FLEXVDI_SPICE_CLIENT_LIBS)
	], )
AM_CONDITIONAL([WITH_PRINTING], [test "x$enable_printing" != "xno"])

AC_OUTPUT

AC_MSG_NOTICE([

        Spiceglue $VERSION
        ===================

        prefix:                   ${prefix}
        c compiler:               ${CC}
        Target:                   ${target}

        Follow-me printing:       ${enable_printing}

        Now type 'make' to build $PACKAGE

])

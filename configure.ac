
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([speech-recognition],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]),
        [krisztian.litkey at intel.com])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([src/config.h])
AM_INIT_AUTOMAKE

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

m4_define(version_major, `echo $VERSION | cut -d. -f1 | cut -d- -f1`)
m4_define(version_minor, `echo $VERSION | cut -d. -f2 | cut -d- -f1`)
m4_define(version_patch, `echo $VERSION | cut -d. -f3 | cut -d- -f1`)

AC_SUBST(VERSION)
AC_SUBST(VERSION_MAJOR, version_major)
AC_SUBST(VERSION_MINOR, version_minor)
AC_SUBST(VERSION_PATCH, version_patch)
AC_SUBST(VERSION_FULL, version_major.version_minor.version_patch)

SRS_VERSION_INFO="0:0:0"
AC_SUBST(SRS_VERSION_INFO)

# Disable static libraries.
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
# We need AC_PROG_CXX if Qt support is enabled but (at least some
# versions of autotools) cannot handle conditional use of this.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

# Checks for libraries.
AC_CHECK_LIB([dl], [dlopen dlclose dlsym dlerror])

# Checks for header files.
AC_PATH_X
AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h sys/statvfs.h sys/vfs.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_HEADER_MAJOR
AC_FUNC_MALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([memmove memset])

# Check and enable extra compiler warnings if they are supported.
AC_ARG_ENABLE(extra-warnings,
              [  --enable-extra-warnings enable extra compiler warnings],
	      [extra_warnings=$enableval], [extra_warnings=auto])

WARNING_CFLAGS=""
warncflags="-Wall -Wextra"
if test "$extra_warnings" != "no"; then
    save_CPPFLAGS="$CPPFLAGS"
    for opt in $warncflags; do
        AC_PREPROC_IFELSE([AC_LANG_PROGRAM([])],
	                  [WARNING_CFLAGS="$WARNING_CFLAGS $opt"])
    done
    CPPFLAGS="$save_CPPFLAGS"
fi

AC_SUBST(WARNING_CFLAGS)

# Check for the necessary Murphy components.
PKG_CHECK_MODULES(MURPHY_COMMON,    murphy-common)
PKG_CHECK_MODULES(MURPHY_PULSE,     murphy-pulse)
PKG_CHECK_MODULES(MURPHY_GLIB,      murphy-glib)
PKG_CHECK_MODULES(MURPHY_RESOURCE,  murphy-resource)
PKG_CHECK_MODULES(MURPHY_BREEDLINE, breedline-murphy)
AC_SUBST(MURPHY_COMMON_CFLAGS)
AC_SUBST(MURPHY_COMMON_LIBS)
AC_SUBST(MURPHY_PULSE_CFLAGS)
AC_SUBST(MURPHY_PULSE_LIBS)
AC_SUBST(MURPHY_GLIB_CFLAGS)
AC_SUBST(MURPHY_GLIB_LIBS)
AC_SUBST(MURPHY_RESOURCE_CFLAGS)
AC_SUBST(MURPHY_RESOURCE_LIBS)
AC_SUBST(MURPHY_BREEDLINE_CFLAGS)
AC_SUBST(MURPHY_BREEDLINE_LIBS)

# Check for PulseAudio.
PKG_CHECK_MODULES(PULSE, libpulse >= 0.9.22)
PKG_CHECK_MODULES(PULSE_GLIB, libpulse-mainloop-glib >= 0.922)
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)
AC_SUBST(PULSE_GLIB_CFLAGS)
AC_SUBST(PULSE_GLIB_LIBS)

# Check if potentially GPL bits are allowed to be enabled.
AC_ARG_ENABLE(gpl,
              [  --enable-gpl            enable linking against GPL code],
	      [enable_gpl=$enableval], [enable_gpl=no])

# Check if DBUS was enabled.
AC_ARG_ENABLE(dbus,
              [  --enable-dbus           enable D-BUS support],
	      [enable_dbus=$enableval], [enable_dbus=no])

if test "$enable_dbus" = "yes"; then
    if test "$enable_gpl" = "no"; then
        AC_MSG_ERROR([D-Bus support requires the --enable-gpl option.])
    fi
    PKG_CHECK_MODULES(DBUS, dbus-1 >= 0.70)
    DBUS_SESSION_DIR="`pkg-config --variable session_bus_services_dir dbus-1`"
    AC_SUBST(DBUS_SESSION_DIR)

    PKG_CHECK_MODULES(MURPHY_DBUS, murphy-dbus-libdbus)
else
    AC_MSG_NOTICE([D-Bus support is disabled.])
fi

if test "$enable_dbus" = "yes"; then
    AC_DEFINE([DBUS_ENABLED], 1, [Enable D-BUS support ?])
fi

AM_CONDITIONAL(DBUS_ENABLED, [test "$enable_dbus" = "yes"])
AC_SUBST(DBUS_ENABLED)
AC_SUBST(MURPHY_DBUS_CFLAGS)
AC_SUBST(MURPHY_DBUS_LIBS)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

# XXX TODO temporarily disable mpris2 and bluetooth plugins
AM_CONDITIONAL(MPRIS2_ENABLED, [test "xno" = "xyes"])
AM_CONDITIONAL(BLUETOOTH_ENABLED, [test "xno" = "xyes"])
AC_SUBST(MPRIS2_ENABLED)
AC_SUBST(BLUETOOTH_ENABLED)

# Set up common SRS compilation and linking flags.
SRS_CFLAGS=""
SRS_LIBS=""
AC_SUBST(SRS_CFLAGS)
AC_SUBST(SRS_LIBS)

# Add LIBDIR to config.h.
AC_MSG_CHECKING([libdir])
AC_MSG_RESULT([$libdir])
AC_SUBST(LIBDIR, [$libdir])

# Check if sphinx was enabled.
AC_ARG_ENABLE(sphinx,
              [  --enable-sphinx         enable pocketsphinx support in SRS],
              [enable_sphinx=$enableval], [enable_sphinx=auto])

if test "$enable_sphinx" != "no"; then
    PKG_CHECK_MODULES(SPHINX, [sphinxbase pocketsphinx],
                      [have_sphinx=yes], [have_sphinx=no])
    if test "$have_sphinx" = "no" -a "$enable_sphinx" = "yes"; then
        AC_MSG_ERROR([Sphinx development libraries not found.])
    fi

    # sphinxbase is packetised in wrong way in Fedora
    # and pkg-config reports libraries what we really
    # do not need; so as a temporary hack here we get
    # rid of them
    SPHINX_LIBS=`echo $SPHINX_LIBS			| \
                      sed -e "s/ -lpulse-simple//g"	| \
                      sed -e "s/ -lblas//g"		| \
                      sed -e "s/ -llapack//g"		| \
                      sed -e "s/ -lsamplerate//g"	| \
                      sed -e "s/ -lsndfile//g"		| \
                      sed -e "s/ -lpthread//g"`

    enable_sphinx="$have_sphinx"
else
    AC_MSG_NOTICE([Sphinx support disabled])
fi

AM_CONDITIONAL(SPHINX_ENABLED, [test "$enable_sphinx" = "yes"])
AC_SUBST(SPHINX_ENABLED)
AC_SUBST(SPHINX_CFLAGS)
AC_SUBST(SPHINX_LIBS)

# Check for libudev.
PKG_CHECK_MODULES(UDEV, libudev)

# Check for glib et al that we need temporarily to allow plugins use gdbus.
PKG_CHECK_MODULES(GLIB, glib-2.0 gobject-2.0)
PKG_CHECK_MODULES(MURPHY_GLIB, murphy-glib)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(MURPHY_GLIB_CFLAGS)
AC_SUBST(MURPHY_GLIB_LIBS)

# Check if WRT media client should be enabled.
AC_ARG_ENABLE(wrt-client,
              [  --enable-wrt-client     enable WRT media client plugin],
	      [enable_wrtc=$enableval], [enable_wrtc=yes])

if test "$enable_wrtc" = "yes"; then
    PKG_CHECK_MODULES(GIO, gio-2.0)
else
    AC_MSG_NOTICE([WRT media client plugin is disabled.])
fi

AM_CONDITIONAL(WRTC_ENABLED, [test "$enable_wrtc" = "yes"])
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

# Check if festival support should be enabled.
AC_ARG_ENABLE(festival,
              [  --enable-festival          enable festival support],
	      [enable_festival=$enableval], [enable_festival=auto])

if test "$enable_festival" != "no"; then
    enable_festival=yes
    AC_MSG_NOTICE([festival support is enabled.])
else
    AC_MSG_NOTICE([festival support is disabled.])
fi

if test "$enable_festival" = "yes"; then
    AC_DEFINE([FESTIVAL_ENABLED], 1, [Enable festival support ?])
fi

if test -f $prefix/include/festival/festival.h; then
    FESTIVAL_INCLUDES="-I$prefix/include \
                       -I$prefix/include/festival \
                       -I$prefix/include/speech_tools"
else
    FESTIVAL_INCLUDES="-I$prefix/include"
fi

FESTIVAL_CXXFLAGS="$FESTIVAL_INCLUDES"
FESTIVAL_LIBS=""

AM_CONDITIONAL(FESTIVAL_ENABLED, [test "$enable_festival" = "yes"])
AC_SUBST(FESTIVAL_ENABLED)
AC_SUBST(FESTIVAL_CXXFLAGS)
AC_SUBST(FESTIVAL_LIBS)

# Shave by default.
SHAVE_INIT([build-aux], [enable])

# Create murphy symlink to src.
if test ! -L srs; then
    AC_MSG_NOTICE([Symlinking src to srs...])
    ln -s src srs
fi

# Generate output.
AC_CONFIG_FILES([build-aux/shave
		 build-aux/shave-libtool
		 Makefile
                 utils/Makefile
                 src/Makefile
                 src/plugins/client-api/native/srs-native-client.pc
		 ])
AC_OUTPUT


# Display the configuration.
echo "----- configuration -----"
echo "Extra C warnings flags: $WARNING_CFLAGS"
echo "D-Bus support: $enable_dbus"
echo "Sphinx support: $enable_sphinx"
echo "Festival support: $enable_festival"

if test "$DUMP_LIB_FLAGS" = "yes"; then
    echo "MURPHY_COMMON:"
    echo "  CFLAGS: $MURPHY_COMMON_CFLAGS"
    echo "  LIBS  : $MURPHY_COMMON_LIBS"

    echo "MURPHY_DBUS:"
    echo "  CFLAGS: $MURPHY_DBUS_CFLAGS"
    echo "  LIBS  : $MURPHY_DBUS_LIBS"

    echo "MURPHY_PULSE:"
    echo "  CFLAGS: $MURPHY_PULSE_CFLAGS"
    echo "  LIBS  : $MURPHY_PULSE_LIBS"

    echo "MURPHY_RESOURCE:"
    echo "  CFLAGS: $MURPHY_RESOURCE_CFLAGS"
    echo "  LIBS  : $MURPHY_RESOURCE_LIBS"

    echo "MURPHY_BREEDLINE:"
    echo "  CFLAGS: $MURPHY_BREEDLINE_CFLAGS"
    echo "  LIBS  : $MURPHY_BREEDLINE_LIBS"
fi

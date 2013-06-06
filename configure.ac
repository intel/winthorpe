
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([speech-recognition],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]),
        [krisztian.litkey at intel.com])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([src/config.h])
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

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

# Check for PulseAudio.
PKG_CHECK_MODULES(PULSE, libpulse >= 0.9.22)
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)

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
else
    AC_MSG_NOTICE([D-Bus support is disabled.])
fi

if test "$enable_dbus" = "yes"; then
    AC_DEFINE([DBUS_ENABLED], 1, [Enable D-BUS support ?])
fi

AM_CONDITIONAL(DBUS_ENABLED, [test "$enable_dbus" = "yes"])
AC_SUBST(DBUS_ENABLED)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

# Set up common SRS compilation and linking flags.
SRS_CFLAGS=""
SRS_LIBS=""
AC_SUBST(SRS_CFLAGS)
AC_SUBST(SRS_LIBS)

# Add LIBDIR to config.h.
AC_MSG_CHECKING([libdir])
AC_MSG_RESULT([$libdir])
AC_SUBST(LIBDIR, [$libdir])

# Check for Check (unit test framework).
PKG_CHECK_MODULES(CHECK, 
                  check >= 0.9.4,
                  [has_check="yes"], [has_check="no"])
AM_CONDITIONAL(HAVE_CHECK, test "x$has_check" = "xyes")

AC_SUBST(CHECK_CFLAGS)
AC_SUBST(CHECK_LIBS)

if test "x$has_check" = "xno"; then
    AC_MSG_WARN([Check framework not found, unit tests are DISABLED.])
fi

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
		 ])
AC_OUTPUT


# Display the configuration.
echo "----- configuration -----"
echo "Extra C warnings flags: $WARNING_CFLAGS"
echo "D-Bus support: $enable_dbus"
echo "Sphinx support: $enable_sphinx"

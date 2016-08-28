# SVAR - configure.ac
# Copyright (c) 2014-2016 Arkadiusz Bokowy

AC_INIT([svar], [1.0.0], [arkadiusz.bokowy@gmail.com])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# support for debugging
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug], [enable debugging support])])
AM_CONDITIONAL([ENABLE_DEBUG], [test "x$enable_debug" = "xyes"])
AM_COND_IF([ENABLE_DEBUG], [
	AC_DEFINE([DEBUG], [1], [Define to 1 if the debugging is enabled])
])

AC_CHECK_LIB([m], [abs],
	[], [AC_MSG_ERROR([math library not found])])
AC_CHECK_LIB([pthread], [pthread_create],
	[], [AC_MSG_ERROR([pthread library not found])])

PKG_CHECK_MODULES([ALSA], [alsa])

# support for the libsndfile (WAV) encoder
AC_ARG_ENABLE([sndfile],
	[AS_HELP_STRING([--enable-sndfile], [enable WAV support])])
AM_CONDITIONAL([ENABLE_SNDFILE], [test "x$enable_sndfile" = "xyes"])
AM_COND_IF([ENABLE_SNDFILE], [
	PKG_CHECK_MODULES([SNDFILE], [sndfile])
	AC_DEFINE([ENABLE_SNDFILE], [1], [Define to 1 if sndfile is enabled])
])

# support for the Ogg Vorbis encoder
AC_ARG_ENABLE([vorbis],
	[AS_HELP_STRING([--enable-vorbis], [enable Ogg Vorbis support])])
AM_CONDITIONAL([ENABLE_VORBIS], [test "x$enable_vorbis" = "xyes"])
AM_COND_IF([ENABLE_VORBIS], [
	PKG_CHECK_MODULES([VORBIS], [vorbis vorbisenc ogg])
	AC_DEFINE([ENABLE_VORBIS], [1], [Define to 1 if vorbis is enabled])
])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

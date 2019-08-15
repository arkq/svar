# SVAR - configure.ac
# Copyright (c) 2014-2019 Arkadiusz Bokowy

AC_PREREQ([2.60])
AC_INIT([svar], [1.1.0], [arkadiusz.bokowy@gmail.com])
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

AC_SEARCH_LIBS([sqrt], [m],
	[], [AC_MSG_ERROR([unable to find sqrt() function])])
AC_SEARCH_LIBS([pthread_create], [pthread],
	[], [AC_MSG_ERROR([pthread library not found])])

PKG_CHECK_MODULES([ALSA], [alsa])

AC_ARG_ENABLE([mp3lame],
	[AS_HELP_STRING([--enable-mp3lame], [enable MP3 support])])
AM_CONDITIONAL([ENABLE_MP3LAME], [test "x$enable_mp3lame" = "xyes"])
AM_COND_IF([ENABLE_MP3LAME], [
	AC_CHECK_LIB([mp3lame], [lame_init],
		[AC_SUBST([MP3LAME_LIBS], [-lmp3lame])], [AC_MSG_ERROR([mp3lame library not found])])
	AC_DEFINE([ENABLE_MP3LAME], [1], [Define to 1 if MP3LAME is enabled.])
])

AC_ARG_ENABLE([sndfile],
	[AS_HELP_STRING([--enable-sndfile], [enable WAV support])])
AM_CONDITIONAL([ENABLE_SNDFILE], [test "x$enable_sndfile" = "xyes"])
AM_COND_IF([ENABLE_SNDFILE], [
	PKG_CHECK_MODULES([SNDFILE], [sndfile])
	AC_DEFINE([ENABLE_SNDFILE], [1], [Define to 1 if SNDFILE is enabled])
])

AC_ARG_ENABLE([vorbis],
	[AS_HELP_STRING([--enable-vorbis], [enable OGG support])])
AM_CONDITIONAL([ENABLE_VORBIS], [test "x$enable_vorbis" = "xyes"])
AM_COND_IF([ENABLE_VORBIS], [
	PKG_CHECK_MODULES([VORBIS], [vorbis vorbisenc ogg])
	AC_DEFINE([ENABLE_VORBIS], [1], [Define to 1 if OGG is enabled])
])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

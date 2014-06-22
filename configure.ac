# SVAR - configure.ac
# Copyright (c) 2014 Arkadiusz Bokowy

AC_INIT([svar], [0.1.0], [arkadiusz.bokowy@gmail.com])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_PROG_CC
AM_PROG_CC_C_O


# support for debugging
AC_ARG_ENABLE(
	[debug],
	[AS_HELP_STRING([--enable-debug], [enable debugging support])],
)
AM_CONDITIONAL([ENABLE_DEBUG], [test "x$enable_debug" = "xyes"])
AM_COND_IF(
	[ENABLE_DEBUG],
	[AC_DEFINE([DEBUG], [1], [Define to 1 if the debugging is enabled])],
)


AC_CHECK_LIB(
	[m], [abs],
	[], [AC_MSG_ERROR([math library not found])]
)
AC_CHECK_LIB(
	[pthread], [pthread_create],
	[], [AC_MSG_ERROR([pthread library not found])]
)

AC_CHECK_HEADERS(
	[alsa/asoundlib.h],
	[], [AC_MSG_ERROR([asoundlib.h header not found])]
)
AC_CHECK_LIB(
	[asound], [snd_pcm_open],
	[], [AC_MSG_ERROR([asound library not found])]
)


# support for the libsndfile (WAV) encoder
AC_ARG_ENABLE(
	[sndfile],
	[AS_HELP_STRING([--enable-sndfile], [enable WAV support])],
)
AM_CONDITIONAL([ENABLE_SNDFILE], [test "x$enable_sndfile" = "xyes"])
AM_COND_IF(
	[ENABLE_SNDFILE],
	[PKG_CHECK_MODULES(
		[SNDFILE], [sndfile],
		[AC_DEFINE([ENABLE_SNDFILE], [1], [Define to 1 if libsndfile is enabled])],
		[AC_MSG_ERROR([module libsndfile not found])],
	)],
)

# support for the OggVorbis encoder
AC_ARG_ENABLE(
	[vorbisenc],
	[AS_HELP_STRING([--enable-vorbisenc], [enable OggVorbis support])],
)
AM_CONDITIONAL([ENABLE_VORBISENC], [test "x$enable_vorbisenc" = "xyes"])
AM_COND_IF(
	[ENABLE_VORBISENC],
	[PKG_CHECK_MODULES(
		[VORBISENC], [vorbis vorbisenc ogg],
		[AC_DEFINE([ENABLE_VORBISENC], [1], [Define to 1 if OggVorbis is enabled])],
		[AC_MSG_ERROR([module vorbisenc not found])],
	)],
)

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

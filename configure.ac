# SVAR - configure.ac
# Copyright (c) 2014 Arkadiusz Bokowy

AC_INIT([svar], [0.0.1], [arkadiusz.bokowy@gmail.com])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_PROG_CC
AM_PROG_CC_C_O

# support for debugging
AC_ARG_ENABLE(
	[debug],
	AS_HELP_STRING([--enable-debug], [enable debugging support])
)
AS_IF([test "x$enable_debug" = "xyes"],
	[AC_DEFINE([DEBUG], [1], [Define to 1 if the debugging is enabled])]
)

AC_CHECK_LIB(
	[m], [abs],
	[], [AC_MSG_ERROR([math library not found])]
)

AC_CHECK_HEADERS(
	[alsa/asoundlib.h],
	[], [AC_MSG_ERROR([asoundlib.h header not found])]
)
AC_CHECK_LIB(
	[asound], [snd_pcm_open],
	[], [AC_MSG_ERROR([asound library not found])]
)

# support for OGG encoder
AC_CHECK_HEADERS([vorbis/vorbisenc.h])
AC_CHECK_LIB([vorbis], [vorbis_analysis_init])
AC_CHECK_LIB([vorbisenc], [vorbis_encode_init])
AC_CHECK_LIB([ogg], [ogg_stream_init])
# support for WAV encoder
AC_CHECK_HEADERS([sndfile.h])
AC_CHECK_LIB([sndfile], [sf_open])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT

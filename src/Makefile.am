# SVAR - Makefile.am
# Copyright (c) 2014-2019 Arkadiusz Bokowy

bin_PROGRAMS = svar
svar_SOURCES = main.c

AM_CFLAGS = \
	@ALSA_CFLAGS@ \
	@SNDFILE_CFLAGS@ \
	@VORBIS_CFLAGS@

LDADD = \
	@ALSA_LIBS@ \
	@MP3LAME_LIBS@ \
	@SNDFILE_LIBS@ \
	@VORBIS_LIBS@

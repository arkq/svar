# SVAR - Makefile.am
# Copyright (c) 2014 Arkadiusz Bokowy

bin_PROGRAMS = svar
svar_SOURCES = main.c

svar_CFLAGS = \
	@SNDFILE_CFLAGS@ \
	@VORBISENC_CFLAGS@

svar_LDADD = \
	@SNDFILE_LIBS@ \
	@VORBISENC_LIBS@

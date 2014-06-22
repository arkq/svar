# SVAR - Makefile.am
# Copyright (c) 2014 Arkadiusz Bokowy

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = svar
svar_SOURCES = main.c


svar_CFLAGS = \
	@SNDFILE_CFLAGS@ \
	@VORBISENC_CFLAGS@

svar_LDFLAGS = \
	@SNDFILE_LIBS@ \
	@VORBISENC_LIBS@

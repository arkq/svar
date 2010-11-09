#
#   svar - makefile
#   Copyright (c) 2010 Arkadiusz Bokowy
#

CC      = gcc

CFLAGS  = -pipe -Wall
LDFLAGS = -lm -lasound -logg -lvorbis -lvorbisenc -lsndfile

OBJS    = svar.o
PROG    = svar

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LDFLAGS)

%.o: src/%.c
	$(CC) $(CFLAGS) -c $<

all: $(PROG)

clean:
	rm -f *.o

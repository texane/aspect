#!/usr/bin/env sh

CFLAGS=`sdl2-config --cflags`
LFLAGS=`sdl2-config --static-libs`
LFLAGS="$LFLAGS -lasound"
LFLAGS="$LFLAGS -lfftw3"

gcc -Wall -O2 $CFLAGS main.c $LFLAGS

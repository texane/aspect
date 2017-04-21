#!/usr/bin/env sh

CFLAGS=`sdl2-config --cflags`
LFLAGS=`sdl2-config --static-libs`

gcc -Wall $CFLAGS -O2 main.c $LFLAGS -lm

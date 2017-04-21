#!/usr/bin/env sh
gcc -Wall -O2 -I../wav main.c ../wav/wav.c -lm -lfftw3

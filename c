#!/bin/sh
CC="cc -O2 -s"
$CC -c -o bstrlib.o bstrlib.c && $CC -c -o sepia.o sepia.c && $CC -o server server.c sepia.o bstrlib.o -lgc && rm *.o

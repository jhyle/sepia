#!/bin/sh
CC="cc -O2 -s"
$CC -DBSTRLIB_MEMORY_DEBUG -c -o bstrlib.o bstrlib.c && $CC -c -o sepia.o sepia.c && $CC -o server server.c sepia.o bstrlib.o -lgc -lbson-1.0 && rm *.o

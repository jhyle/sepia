#!/bin/sh
CC="cc -O2 -s"
$CC -c -o jsonsl.o jsonsl.c && $CC -DBSTRLIB_MEMORY_DEBUG -c -o bstrlib.o bstrlib.c && $CC -c -o sepia.o sepia.c && $CC -o server server.c sepia.o bstrlib.o jsonsl.o -lgc -lbson-1.0 #&& rm *.o

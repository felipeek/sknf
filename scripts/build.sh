#!/bin/bash

mkdir -p bin

# dynamic linking
gcc -O0 -g -Wall -I include -o bin/cni-plugin src/main.c lib/libjson-c.a $(pkg-config --cflags --libs libnl-3.0 libnl-route-3.0)

# static linking
#gcc -O0 -g -Wall -I include -o bin/cni-plugin src/main.c lib/libjson-c.a -static $(pkg-config --cflags --libs --static libnl-3.0 libnl-route-3.0)

# dynamic linking; use compiled 3.11.0
#gcc -O0 -g -Wall -Iinclude -I/home/felipeek/Development/libnl/bin/include/libnl3 -o bin/cni-plugin src/main.c lib/libjson-c.a -L/opt/libnl-3.11.0/lib -Wl,-rpath,/opt/libnl-3.11.0/lib -l:libnl-3.so.200.26.0 -l:libnl-route-3.so.200.26.0

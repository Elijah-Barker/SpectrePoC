#!/usr/bin/env bash
emcc -s FORCE_FILESYSTEM=1 -s NO_EXIT_RUNTIME=0 -pthread -sPTHREAD_POOL_SIZE=3 "$*" clock.c 
wasm2wat --enable-threads a.out.wasm -o a.out.wat
./run.sh

#!/usr/bin/env bash
emcc -O0 -g -s FORCE_FILESYSTEM=1 -s NO_EXIT_RUNTIME=0 -pthread -sPTHREAD_POOL_SIZE=3 -s TOTAL_MEMORY=1GB -DWASM spectre.c 
wasm2wat --enable-threads a.out.wasm -o a.out.wat
time ./run.sh

#!/bin/bash
# usage: build.sh <name> <board> [sysclk] [fine]
set -e
. /workspace/pico-wspr/env.sh
name=$1; board=$2; clk=${3:-}; fine=${4:-0}
cd /workspace/pico-wspr/repo; B=/workspace/pico-wspr/builds/$name; mkdir -p /workspace/pico-wspr/builds
rm -rf $B
cmake -S . -B $B -G Ninja -DPICO_BOARD=$board -DWSPR_SYSCLK_MHZ=$clk -DWSPR_DCO_FINE=$fine -DCMAKE_BUILD_TYPE=Release > $B.cmake.log 2>&1
ninja -C $B > $B.log 2>&1 || { tail -40 $B.log; exit 1; }
grep -i "warning" $B.log | grep -v "pico-sdk" | head -20 || true
arm-none-eabi-size $B/pico-wspr-tx-enhanced.elf | tail -1

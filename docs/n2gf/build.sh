#!/bin/bash
# usage: build.sh <name> <board> [sysclk] [fine] [extra cmake args...]
#   sysclk/fine default to AUTO: pico2 (RP2350) -> 288 MHz + fine dco3 engine,
#   pico (RP2040) -> original 200 MHz + classic dco2. Pass a value to override,
#   e.g. build.sh p2-276 pico2 276 1   or   build.sh p2-200 pico2 "" 0
set -e
. /workspace/pico-wspr/env.sh
name=$1; board=$2; clk=${3-AUTO}; fine=${4:-AUTO}; shift $(( $# < 4 ? $# : 4 ))
cd /workspace/pico-wspr/repo; B=/workspace/pico-wspr/builds/$name; mkdir -p /workspace/pico-wspr/builds
rm -rf $B
cmake -S . -B $B -G Ninja -DPICO_BOARD=$board "-DWSPR_SYSCLK_MHZ=$clk" "-DWSPR_DCO_FINE=$fine" -DCMAKE_BUILD_TYPE=Release "$@" > $B.cmake.log 2>&1
ninja -C $B > $B.log 2>&1 || { tail -40 $B.log; exit 1; }
grep -i "warning" $B.log | grep -v "pico-sdk" | head -20 || true
arm-none-eabi-size $B/pico-wspr-tx-enhanced.elf | tail -1

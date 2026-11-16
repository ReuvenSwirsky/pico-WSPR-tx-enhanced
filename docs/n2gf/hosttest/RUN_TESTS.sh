#!/bin/bash
# Run from a checkout: R=path/to/repo ./RUN_TESTS.sh
set -e; R=${R:-../repo}; G=$R/pico-hf-oscillator; P=$G/gpstime
gcc -O2 -fwrapv -Wall -o upstream_pps_test upstream_pps_test.c -lm && ./upstream_pps_test | tail -2
gcc -O1 -g -fsanitize=undefined,address -fno-sanitize-recover=all -I$P -o ppscal_test ppscal_test.c $P/ppscal.c -lm && ./ppscal_test | tail -1
gcc -O2 -fgnu89-inline -Wno-old-style-declaration -Wno-format '-D__FBSDID(x)=' -Istubs -I$P -I$G -o gps_test gps_test.c $G/lib/thirdparty/strnstr.c $P/ppscal.c -lm && ./gps_test | tail -1
gcc -O2 -I$P -o fine_engine_test fine_engine_test.c $P/ppscal.c -lm && ./fine_engine_test | tail -1
gcc -O2 -o dco_math_test dco_math_test.c -lm && ./dco_math_test | tail -1

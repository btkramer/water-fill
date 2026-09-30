#!/bin/sh
# Host-side unit tests for the pure logic (controller, settings, history).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/wf_control_test"
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
  control_test.cpp ../WaterFill_v2/control.cpp ../WaterFill_v2/settings.cpp ../WaterFill_v2/history.cpp \
  -o "$OUT"
"$OUT"

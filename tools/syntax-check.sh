#!/data/data/com.termux/files/usr/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# Host syntax check (no NDK required).
#
# Compiles every engine translation unit with -fsyntax-only against the stub
# headers in tools/host-stubs (jni.h / android/log.h). Catches type errors,
# missing includes and signature mismatches before shipping to a real build.
#
# Requires: any clang (zig c++ works:  zig c++ -std=c++17 ...)
#   usage: ./tools/syntax-check.sh [c++-compiler]
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${1:-clang++}"

cd "$REPO/src"
FLAGS="-std=c++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
  -I include -I . -I ../tools/host-stubs"

fail=0
for f in common/strings.cpp common/config.cpp common/gpu.cpp \
         spoof/props.cpp spoof/cpu.cpp spoof/gpu.cpp \
         zygisk_module.cpp controller.cpp; do
  if $CXX $FLAGS "$f" 2>&1 | sed "s|^|[$f] |"; then
    echo "✓ $f"
  else
    echo "✗ $f FAILED"; fail=1
  fi
done
exit $fail

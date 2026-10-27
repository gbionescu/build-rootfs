#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -Wall -Wextra -O2 -fPIC}"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config is required (install pkg-config)"
    exit 1
fi

if ! pkg-config --exists Qt6Widgets; then
    echo "Qt6 Widgets development files not found (install qt6-base-dev)"
    exit 1
fi

# shellcheck disable=SC2086
"$CXX" $CXXFLAGS $(pkg-config --cflags Qt6Widgets) \
    -o "$SCRIPT_DIR/stopwatch" "$SCRIPT_DIR/main.cpp" \
    $(pkg-config --libs Qt6Widgets)

echo "Built $SCRIPT_DIR/stopwatch"

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/.build/presubmit}"

mkdir -p "$BUILD_DIR"

echo "[presubmit] Configuring CMake in $BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR"

echo "[presubmit] Building"
cmake --build "$BUILD_DIR"

echo "[presubmit] Running tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "[presubmit] OK"

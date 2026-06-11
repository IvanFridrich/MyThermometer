#!/usr/bin/env bash
# scripts/coverage.sh — Generate LLVM coverage report for the native test build.
#
# Prerequisites (Windows via Git Bash or WSL):
#   - LLVM installed and llvm-profdata / llvm-cov on PATH
#     (e.g. C:\Program Files\LLVM\bin on Windows, or apt install llvm on Linux)
#   - Run `pio test -e native` first with LLVM_PROFILE_FILE set:
#       export LLVM_PROFILE_FILE=default.profraw
#       pio test -e native
#
# Usage:
#   bash scripts/coverage.sh
#   # Then open coverage/index.html in a browser.
#
# On Windows, if llvm-profdata is not on PATH, use the versioned variant:
#   alias llvm-profdata='llvm-profdata-18'
#   alias llvm-cov='llvm-cov-18'

set -euo pipefail

PROFRAW="${LLVM_PROFILE_FILE:-default.profraw}"
PROFDATA="coverage.profdata"
TEST_BINARY=".pio/build/native/program"
SOURCE_DIR="src/core"
OUTPUT_DIR="coverage"

if [[ ! -f "${PROFRAW}" ]]; then
    echo "ERROR: ${PROFRAW} not found."
    echo "Run: LLVM_PROFILE_FILE=${PROFRAW} pio test -e native"
    exit 1
fi

if [[ ! -f "${TEST_BINARY}" ]]; then
    echo "ERROR: ${TEST_BINARY} not found. Run: pio test -e native"
    exit 1
fi

echo "Merging profile data..."
llvm-profdata merge -sparse "${PROFRAW}" -o "${PROFDATA}"

echo ""
echo "Coverage summary (${SOURCE_DIR}/):"
llvm-cov report "${TEST_BINARY}" \
    -instr-profile="${PROFDATA}" \
    "${SOURCE_DIR}/"

echo ""
echo "Generating HTML report..."
llvm-cov show "${TEST_BINARY}" \
    -instr-profile="${PROFDATA}" \
    "${SOURCE_DIR}/" \
    -format=html \
    -output-dir="${OUTPUT_DIR}/"

echo ""
echo "Coverage report written to: ${OUTPUT_DIR}/index.html"

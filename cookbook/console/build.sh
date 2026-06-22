#!/bin/sh
# ===================================================================
#  Generic builder for the OpenADS cookbook -- console (pure Harbour)
#  POSIX counterpart of build.cmd. No machine-specific paths.
#
#  Usage:
#     ./build.sh <openads-lib-dir> [example.hbp]
#
#     <openads-lib-dir>  folder holding the OpenADS shared lib (libace.so)
#     [example.hbp]      which example to build (default: 01_hello_table.hbp)
#
#  Prerequisites:
#     * a C toolchain hbmk2 can drive (gcc/clang)
#     * hbmk2 on PATH, or set HB_BIN
#     * OPENADS_ACELIB = import-lib base name (default: ace64; plus: openace64)
# ===================================================================
set -e

if [ -z "$1" ]; then
   echo "Usage: ./build.sh <openads-lib-dir> [example.hbp]"
   exit 2
fi

OPENADS_LIB="$1"
export OPENADS_LIB
: "${OPENADS_ACELIB:=ace64}"
export OPENADS_ACELIB

HBP="${2:-01_hello_table.hbp}"
HBMK="${HB_BIN:-hbmk2}"

cd "$(dirname "$0")"

echo "=== Building $HBP ==="
"$HBMK" "$HBP"

echo "=== Build OK ==="
echo "Run with:  LD_LIBRARY_PATH=\"$OPENADS_LIB:\$LD_LIBRARY_PATH\" ./${HBP%.hbp}"

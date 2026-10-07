#!/usr/bin/env sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=config/build-defaults.sh
. "$SCRIPT_DIR/config/build-defaults.sh"
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/examples/simple/build/$MICROKIT_BOARD/$MICROKIT_CONFIG"}

rm -rf "$BUILD_DIR"
exec nix develop "$SCRIPT_DIR" --command make \
    -C "$SCRIPT_DIR/examples/simple" \
    -j"$(nproc)" \
    "BUILD_DIR=$BUILD_DIR" \
    "MICROKIT_SDK=$MICROKIT_SDK" \
    "MICROKIT_BOARD=$MICROKIT_BOARD" \
    "MICROKIT_CONFIG=$MICROKIT_CONFIG" \
    all

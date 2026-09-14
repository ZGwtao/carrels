#!/usr/bin/env sh

# Recreate and populate qemu_disk from the currently built application images.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXAMPLE_DIR="$SCRIPT_DIR/examples/simple"
BUILD_DIR=${BUILD_DIR:-"$EXAMPLE_DIR/build"}
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}
MICROKIT_BOARD=${MICROKIT_BOARD:-x86_64_generic}
MICROKIT_CONFIG=${MICROKIT_CONFIG:-smp-debug}

if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "build-ramdisk.sh: missing $BUILD_DIR/Makefile" >&2
    echo "Run ./build-infra.sh first." >&2
    exit 1
fi

if [ ! -d "$MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" ]; then
    echo "build-ramdisk.sh: invalid Microkit SDK/configuration:" >&2
    echo "  $MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" >&2
    exit 1
fi

nix develop "$SCRIPT_DIR" --command make \
    -C "$EXAMPLE_DIR" \
    "BUILD_DIR=$BUILD_DIR" \
    "MICROKIT_SDK=$MICROKIT_SDK" \
    "MICROKIT_BOARD=$MICROKIT_BOARD" \
    "MICROKIT_CONFIG=$MICROKIT_CONFIG" \
    ramdisk

DISK_FILE="$BUILD_DIR/qemu_disk"
if [ ! -f "$DISK_FILE" ]; then
    echo "build-ramdisk.sh: build completed without producing $DISK_FILE" >&2
    exit 1
fi

echo "QEMU disk built successfully:"
ls -lh "$DISK_FILE"

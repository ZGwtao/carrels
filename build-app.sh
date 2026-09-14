#!/usr/bin/env sh

# Incrementally build one application image, for example unikraft-redis.
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <application-name>" >&2
    echo "Example: $0 unikraft-redis" >&2
    exit 2
fi

APP_NAME=$1
case "$APP_NAME" in
    *[!A-Za-z0-9_-]*|'')
        echo "build-app.sh: invalid application name: $APP_NAME" >&2
        exit 2
        ;;
esac

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/examples/simple/build"}
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}
IMAGE_FILE="$BUILD_DIR/$APP_NAME.img"

if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "build-app.sh: missing $BUILD_DIR/Makefile" >&2
    echo "Run ./run.sh once to build the infrastructure first." >&2
    exit 1
fi

if [ ! -d "$MICROKIT_SDK/board/x86_64_generic/smp-debug" ]; then
    echo "build-app.sh: invalid Microkit SDK: $MICROKIT_SDK" >&2
    exit 1
fi

nix develop "$SCRIPT_DIR" --command make \
    -C "$BUILD_DIR" \
    "MICROKIT_SDK=$MICROKIT_SDK" \
    "$APP_NAME.img"

if [ ! -f "$IMAGE_FILE" ]; then
    echo "build-app.sh: make completed without producing $IMAGE_FILE" >&2
    exit 1
fi

echo "Application image built successfully:"
ls -lh "$IMAGE_FILE"
echo "To update the existing QEMU disk (with QEMU stopped), run:"
echo "  nix develop --command examples/simple/copy2ramdisk.sh examples/simple/build/$APP_NAME.img 1"

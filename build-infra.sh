#!/usr/bin/env sh

# Incrementally build the x86_64 Carrels infrastructure and container image.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXAMPLE_DIR="$SCRIPT_DIR/examples/simple"
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}
MICROKIT_BOARD=${MICROKIT_BOARD:-x86_64_generic}
MICROKIT_CONFIG=${MICROKIT_CONFIG:-smp-debug}

if [ ! -d "$MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" ]; then
    echo "build-infra.sh: invalid Microkit SDK/configuration:" >&2
    echo "  $MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" >&2
    exit 1
fi

nix develop "$SCRIPT_DIR" --command make \
    -C "$EXAMPLE_DIR" \
    "MICROKIT_SDK=$MICROKIT_SDK" \
    "MICROKIT_BOARD=$MICROKIT_BOARD" \
    "MICROKIT_CONFIG=$MICROKIT_CONFIG" \
    infra

IMAGE_FILE="$EXAMPLE_DIR/build/container.img"
if [ ! -f "$IMAGE_FILE" ]; then
    echo "build-infra.sh: build completed without producing $IMAGE_FILE" >&2
    exit 1
fi

echo "Infrastructure image built successfully:"
ls -lh "$IMAGE_FILE"

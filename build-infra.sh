#!/usr/bin/env sh

# Incrementally build the Carrels infrastructure and container image.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=config/build-defaults.sh
. "$SCRIPT_DIR/config/build-defaults.sh"
EXAMPLE_DIR="$SCRIPT_DIR/examples/simple"
BUILD_DIR=${BUILD_DIR:-"$EXAMPLE_DIR/build/$MICROKIT_BOARD/$MICROKIT_CONFIG"}
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}

if [ ! -d "$MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" ]; then
    echo "build-infra.sh: invalid Microkit SDK/configuration:" >&2
    echo "  $MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG" >&2
    exit 1
fi

# Filesystem sources and public headers moved first out of the repository and
# then into components/fat. Remove only stale dependency metadata and its
# corresponding object so existing build directories migrate incrementally.
if [ -d "$BUILD_DIR" ]; then
    find "$BUILD_DIR" -type f -name '*.d' -exec \
        grep -l -E "$SCRIPT_DIR/(components/(fs/fat|lionsos-fs-sddf)|include/lions/fs|lib/fs/server)/" {} + 2>/dev/null |
    while IFS= read -r dependency_file; do
        rm -f "$dependency_file" "${dependency_file%.d}.o"
    done
fi

export SCRIPT_DIR EXAMPLE_DIR BUILD_DIR MICROKIT_SDK MICROKIT_BOARD MICROKIT_CONFIG

nix develop "$SCRIPT_DIR" --command bash -lc '
    make -C "$SCRIPT_DIR/guest/k8s" kubelet-credentials &&
    make -C "$SCRIPT_DIR/guest/k8s" rootfs &&
    make -C "$EXAMPLE_DIR" \
        "BUILD_DIR=$BUILD_DIR" \
        "MICROKIT_SDK=$MICROKIT_SDK" \
        "MICROKIT_BOARD=$MICROKIT_BOARD" \
        "MICROKIT_CONFIG=$MICROKIT_CONFIG" \
        infra
'

IMAGE_FILE="$BUILD_DIR/container.img"
if [ ! -f "$IMAGE_FILE" ]; then
    echo "build-infra.sh: build completed without producing $IMAGE_FILE" >&2
    exit 1
fi

echo "Infrastructure image built successfully:"
ls -lh "$IMAGE_FILE"

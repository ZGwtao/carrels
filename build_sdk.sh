#!/usr/bin/env sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=config/build-defaults.sh
. "$SCRIPT_DIR/config/build-defaults.sh"

git -C "$SCRIPT_DIR" submodule update --init --recursive
test ! -e "$SCRIPT_DIR/dep/uk-on-mk/dep/sddf" || test -L "$SCRIPT_DIR/dep/uk-on-mk/dep/sddf" || { echo "refusing to replace non-symlink $SCRIPT_DIR/dep/uk-on-mk/dep/sddf" >&2; exit 1; }
ln -sfn ../../sddf "$SCRIPT_DIR/dep/uk-on-mk/dep/sddf"

mkdir -p "$SCRIPT_DIR/dep/uk-on-mk/dep/catalog-core/repos"
(cd "$SCRIPT_DIR/dep/uk-on-mk/dep/catalog-core" && ./setup.sh)

(cd "$SCRIPT_DIR/dep/microkit" && \
    nix develop "$SCRIPT_DIR" --command python build_sdk.py \
        --skip-tar \
        "--boards=$MICROKIT_BOARD" \
        --sel4=../sel4 \
        "--configs=$MICROKIT_CONFIG")

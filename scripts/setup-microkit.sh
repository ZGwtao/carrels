#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 UNSW
# SPDX-License-Identifier: BSD-2-Clause

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    echo "run: source scripts/setup-microkit.sh" >&2
    exit 1
fi

setup_microkit() {
    local carrels wsp microkit sdfgen sddf uk_on_mk uk_sddf

    carrels=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
    wsp=${WSP:-"$HOME/wsp"}
    microkit=${MICROKIT:-"$wsp/microkit"}
    sdfgen=${SDFGEN:-"$wsp/microkit_sdf_gen"}
    uk_on_mk=${UK_ON_MK_DIR:-"$carrels/dep/uk-on-mk"}
    sddf="$carrels/dep/sddf"
    export MICROKIT_SDK=${MICROKIT_SDK:-"$microkit/release/microkit-sdk-2.3.0-dev"}
    export MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_CONFIG=debug

    for path in "$microkit" "$microkit/seL4" "$sdfgen"; do
        test -d "$path" || { echo "missing directory: $path" >&2; return 1; }
    done

    git -C "$carrels" submodule update --init --recursive || return
    test -d "$uk_on_mk" || { echo "missing directory: $uk_on_mk" >&2; return 1; }
    test -d "$sddf" || { echo "missing directory: $sddf" >&2; return 1; }
    uk_sddf="$uk_on_mk/dep/sddf"

    if [[ -e "$uk_sddf" && ! -L "$uk_sddf" ]]; then
        echo "refusing to replace non-symlink sDDF path: $uk_sddf" >&2
        return 1
    fi
    ln -sfn "../../sddf" "$uk_sddf" || return
    export SDDF=$sddf
    export UK_ON_MK_DIR=$uk_on_mk

    test "$(git -C "$sdfgen" branch --show-current)" = vswitch-orchestrator-dev || {
        echo "sdfgen must be on the vswitch-orchestrator-dev branch" >&2
        return 1
    }

    # shellcheck disable=SC1091
    . "$microkit/pyenv/bin/activate" || return
    export PYTHON="$(command -v python)"
    python -c 'from importlib.metadata import version; version("sel4-deps")' \
        >/dev/null 2>&1 || python -m pip install --upgrade sel4-deps || return
    python -c 'import sdfgen' >/dev/null 2>&1 || python -m pip install "$sdfgen" || return

    if ! test -x "$MICROKIT_SDK/bin/microkit" || \
       ! test -f "$MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG/lib/libmicrokit.a"; then
        (
            cd "$microkit" || exit
            python build_sdk.py --sel4 seL4 --boards "$MICROKIT_BOARD" \
                --configs "$MICROKIT_CONFIG" --skip-docs --skip-tar
        ) || return
    fi

    printf 'Microkit environment ready; run: make -C examples/simple qemu\n'
}

setup_microkit
status=$?
unset -f setup_microkit
return "$status"

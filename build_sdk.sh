#!/usr/bin/env sh

git submodule update --init --recursive
test ! -e ./dep/uk-on-mk/dep/sddf || test -L ./dep/uk-on-mk/dep/sddf || { echo "refusing to replace non-symlink ./dep/uk-on-mk/dep/sddf" >&2; exit 1; }
ln -sfn ../../sddf ./dep/uk-on-mk/dep/sddf

cd ./dep/microkit && \
nix develop --command bash -c "python build_sdk.py --skip-tar --boards=x86_64_generic,x86_64_generic_vtx --sel4=../sel4 --configs=smp-debug"


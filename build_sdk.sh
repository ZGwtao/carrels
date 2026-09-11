#!/usr/bin/env sh

cd ./dep/microkit && \
nix develop --command bash -c "python build_sdk.py --skip-tar --boards=x86_64_generic,x86_64_generic_vtx --sel4=../sel4 --configs=smp-debug"


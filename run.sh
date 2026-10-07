#!/usr/bin/env sh

# Check if target machine is provided

rm -rf examples/simple/build && \
nix develop --command bash -c "\
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev infra && \
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev apps && \
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev qemu"

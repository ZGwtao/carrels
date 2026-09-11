#!/usr/bin/env sh

# Check if target machine is provided
if [ -z "$1" ]; then
    echo "Error: Target machine not provided."
    echo "Usage: $0 <target_machine_name>"
    exit 1
fi

rm -rf examples/simple/build && \
nix develop --command bash -c "\
pip install dep/microkit_sdf_gen && \
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev infra && \
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev apps && \
make -j$(nproc) -C examples/simple MICROKIT_CONFIG=smp-debug MICROKIT_BOARD=x86_64_generic MICROKIT_SDK=../../dep/microkit/release/microkit-sdk-2.3.0-dev qemu"


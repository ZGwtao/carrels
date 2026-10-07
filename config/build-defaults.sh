#!/usr/bin/env sh

# Shared defaults for the shell and Make build entry points. Callers may set
# either variable before loading this file to override the corresponding value.
: "${MICROKIT_BOARD:=qemu_virt_aarch64}"
: "${MICROKIT_CONFIG:=smp-debug}"


#!/usr/bin/env sh

# Start the image produced by run.sh without invoking make or rebuilding it.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/examples/simple/build"}
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}
MICROKIT_CONFIG=${MICROKIT_CONFIG:-smp-debug}

IMAGE_FILE="$BUILD_DIR/container.img"
DISK_FILE="$BUILD_DIR/qemu_disk"
SEL4_FILE="$MICROKIT_SDK/board/x86_64_generic/$MICROKIT_CONFIG/elf/sel4_32.elf"

for file in "$IMAGE_FILE" "$DISK_FILE" "$SEL4_FILE"; do
    if [ ! -f "$file" ]; then
        echo "qemu.sh: missing build artifact: $file" >&2
        echo "Run ./run.sh successfully first." >&2
        exit 1
    fi
done

# Entering the existing Nix development environment only supplies QEMU; it
# does not invoke make or modify container.img/qemu_disk.
exec nix develop "$SCRIPT_DIR" --command qemu-system-x86_64 \
    -machine q35 \
    -kernel "$SEL4_FILE" \
    -m size=2G \
    -serial mon:stdio \
    -cpu qemu64,+fsgsbase,+pdpe1gb,+pcid,+invpcid,+xsave,+xsaves,+xsaveopt \
    -initrd "$IMAGE_FILE" \
    -device nvme,drive=hd,serial=carrels,addr=0x4.0 \
    -device virtio-net-pci,netdev=netdev0,addr=0x2.0 \
    -nographic \
    -drive "file=$DISK_FILE,if=none,format=raw,id=hd" \
    -netdev user,id=netdev0,hostfwd=tcp::8080-10.0.2.15:80,hostfwd=tcp::8081-10.0.2.16:80 \
    -global virtio-mmio.force-legacy=false \
    -d guest_errors \
    -smp 4 \
    "$@"

#!/usr/bin/env sh

# Start the image produced by run.sh without invoking make or rebuilding it.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=config/build-defaults.sh
. "$SCRIPT_DIR/config/build-defaults.sh"
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/examples/simple/build/$MICROKIT_BOARD/$MICROKIT_CONFIG"}
MICROKIT_SDK=${MICROKIT_SDK:-"$SCRIPT_DIR/dep/microkit/release/microkit-sdk-2.3.0-dev"}

IMAGE_FILE="$BUILD_DIR/container.img"
DISK_FILE="$BUILD_DIR/qemu_disk"

for file in "$IMAGE_FILE" "$DISK_FILE"; do
    if [ ! -f "$file" ]; then
        echo "qemu.sh: missing build artifact: $file" >&2
        echo "Run ./run.sh successfully first." >&2
        exit 1
    fi
done

# Entering the Nix development environment only supplies QEMU; it does not
# invoke make or modify container.img/qemu_disk.
case "$MICROKIT_BOARD" in
    qemu_virt_aarch64)
        exec nix develop "$SCRIPT_DIR" --command qemu-system-aarch64 \
            -machine virt,virtualization=on \
            -cpu cortex-a53 \
            -m size=2G \
            -serial mon:stdio \
            -device "loader,file=$IMAGE_FILE,addr=0x70000000,cpu-num=0" \
            -device virtio-blk-device,drive=hd,bus=virtio-mmio-bus.1 \
            -device virtio-net-device,netdev=netdev0,bus=virtio-mmio-bus.0 \
            -nographic \
            -drive "file=$DISK_FILE,if=none,format=raw,id=hd" \
            -netdev user,id=netdev0,hostfwd=tcp::8080-10.0.2.15:80,hostfwd=tcp::8081-10.0.2.16:80 \
            -global virtio-mmio.force-legacy=false \
            -d guest_errors \
            -smp 4 \
            "$@"
        ;;
    x86_64_generic)
        SEL4_FILE="$MICROKIT_SDK/board/$MICROKIT_BOARD/$MICROKIT_CONFIG/elf/sel4_32.elf"
        if [ ! -f "$SEL4_FILE" ]; then
            echo "qemu.sh: missing build artifact: $SEL4_FILE" >&2
            exit 1
        fi
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
        ;;
    *)
        echo "qemu.sh: unsupported QEMU board: $MICROKIT_BOARD" >&2
        echo "Supported boards: qemu_virt_aarch64, x86_64_generic" >&2
        exit 2
        ;;
esac

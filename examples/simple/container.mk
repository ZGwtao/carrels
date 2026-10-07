# Copyright 2025, UNSW
# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

SUPPORTED_BOARDS:= \
	qemu_virt_aarch64 \
	maaxboard \
	odroidc4 \
	x86_64_generic \
	x86_64_generic_vtx

UK_ON_MK_DIR ?= $(CARRELS)/dep/uk-on-mk
TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF ?= $(CARRELS)/dep/sddf
LIBMICROKITCO_PATH := $(CARRELS)/dep/libmicrokitco
LIBVMM := $(CARRELS)/dep/libvmm
LIBVMM_EXAMPLE := $(LIBVMM)/examples/virtio_pci
SYSTEM_FILE := container.system
IMAGE_FILE := container.img
DLG_FILE := container_monitor.dlg
REPORT_FILE := report.txt
PROTOCON_COUNT ?= 4

# qemu or vtx-demo
X86_DEVICE_PROFILE ?= qemu

# Older sDDF checkpoints use a machine-specific board name in their metadata
# while the Microkit SDK uses x86_64_generic. Keep those two namespaces
# separate when invoking the metaprogram.
META_BOARD := $(MICROKIT_BOARD)
ifeq ($(MICROKIT_BOARD),x86_64_generic)
X86_BOARD ?= qemu_virt_x86
META_BOARD := $(X86_BOARD)
endif

# Each FATFS has an exclusive partition: orchestrator, monitor, then one per
# protocon. Keep all partitions at 64 MiB so the monitor ramdisk remains large
# enough for the infrastructure and application configuration files.
QEMU_DISK_PARTITION_COUNT := $(shell expr $(PROTOCON_COUNT) + 3)
QEMU_DISK_PARTITION_BYTES ?= 67108864
QEMU_DISK_SIZE_BYTES := $(shell expr $(QEMU_DISK_PARTITION_COUNT) \* $(QEMU_DISK_PARTITION_BYTES))


.PHONY: all build infra apps app-native app-uk ramdisk qemu refresh-ramdisk

# Keep the complete workflow ordered even when make is invoked with -j.
all:
	$(MAKE) infra
	$(MAKE) apps
	$(MAKE) ramdisk
	$(MAKE) qemu

build:
	$(MAKE) infra
	$(MAKE) apps
	$(MAKE) ramdisk

include $(ROOT)/mk/components.mk

METAPROGRAM := $(CONTAINER_DIR)/meta/meta.py
RAMDISK_INITIALISER := $(CONTAINER_DIR)/refresh-ramdisk.py
include $(ROOT)/mk/k8s-vmm.mk

INFRA_IMAGES := \
	timer_driver.elf \
	eth_driver.elf network_virt_rx.elf network_virt_tx.elf network_vswitch.elf network_copy.elf \
	monitor.elf \
	vsock_backend.elf \
	k8s_vmm.elf \
	fat.elf \
	trampoline.elf \
	protocon.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

ifeq ($(ARCH),x86_64)
INFRA_IMAGES += acpi_driver.elf pci_driver.elf
endif

NATIVE_APPLICATION_IMAGES := $(PC_SERVICE_IMGS)
UNIKRAFT_APPLICATION_IMAGES := $(UNIKERNELS)
K8S_APPLICATION_IMAGES := c-hello.img
APPLICATION_IMAGES := $(NATIVE_APPLICATION_IMAGES) $(UNIKRAFT_APPLICATION_IMAGES) \
	$(K8S_APPLICATION_IMAGES)

c-hello.img: unikraft-c-hello.img
	cp $< $@

$(INFRA_IMAGES) $(APPLICATION_IMAGES): libsddf_util_debug.a

FORCE:

LAYOUT_CMD := \
	--vm-layout $(PROTOCON_VM_LAYOUT) \
	--monitor-vm-layout $(CONTAINER_COMPONENT_DIR)/config/monitor_vm_layout.py


$(SYSTEM_FILE): $(METAPROGRAM) $(INFRA_IMAGES) $(DTB) $(K8S_VM_DTB)
ifneq ($(strip $(DTS)),)
	$(PYTHON) -B \
	    $(METAPROGRAM) --sddf $(SDDF) --board $(META_BOARD) $(LAYOUT_CMD) \
	    --dtb $(DTB) --client-dtb $(K8S_VM_DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) \
	    --protocon-count $(PROTOCON_COUNT) \
	    --x86-device-profile $(X86_DEVICE_PROFILE) $(BLK_META_ARGS)
else
	$(PYTHON) -B \
	    $(METAPROGRAM) --sddf $(SDDF) --board $(META_BOARD) $(LAYOUT_CMD) \
	    --client-dtb $(K8S_VM_DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) \
	    --protocon-count $(PROTOCON_COUNT) \
	    --x86-device-profile $(X86_DEVICE_PROFILE) $(BLK_META_ARGS)
endif
ifdef BLK_NEED_TIMER
	$(OBJCOPY) --update-section .timer_client_config=timer_client_blk_driver.data blk_driver.elf
endif
	$(OBJCOPY) --update-section .device_resources=eth_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_vswitch_config=net_vswitch.data network_vswitch.elf
	$(OBJCOPY) --update-section .net_vswitch_orchestrator_config=net_vswitch_orchestrator.data monitor.elf
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_vsock_backend.data vsock_backend.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_k8s_vmm.data k8s_vmm.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_vsock_backend.data vsock_backend.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_k8s_vmm.data k8s_vmm.elf
	$(OBJCOPY) --update-section .net_client_config=net_client_k8s_vmm.data k8s_vmm.elf
	$(OBJCOPY) --update-section .vmm_config=vmm_k8s_vmm.data k8s_vmm.elf
	$(OBJCOPY) --update-section .virtio_vsock_transport_config=virtio_vsock_transport_k8s_vmm.data k8s_vmm.elf
	$(OBJCOPY) --update-section .virtio_vsock_transport_config=virtio_vsock_transport_vsock_backend.data vsock_backend.elf

SPEC = capdl_spec.json
$(IMAGE_FILE) $(REPORT_FILE) $(DLG_FILE): $(INFRA_IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) \
		--search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) 	\
		--config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE) --capdl-json ${SPEC}
	cp $(BUILD_DIR)/build/delegation/*.dlg $(BUILD_DIR)

infra: $(IMAGE_FILE)

apps: app-native app-uk
app-native: $(NATIVE_APPLICATION_IMAGES)
app-uk: $(UNIKRAFT_APPLICATION_IMAGES)

refresh-ramdisk: $(RAMDISK_INITIALISER) qemu_disk
	cp $(CONTAINER_DIR)/disk-test.txt $(BUILD_DIR)/disk-test.txt
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(RAMDISK_INITIALISER) $(BUILD_DIR) $(PROTOCON_COUNT)

qemu_disk: FORCE
	$(SDDF)/tools/mkvirtdisk $@ $(QEMU_DISK_PARTITION_COUNT) 512 $(QEMU_DISK_SIZE_BYTES) GPT

ramdisk: refresh-ramdisk

qemu: ramdisk
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_BLK_ARGS) $(QEMU_NET_ARGS) \
		-nographic \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-netdev user,id=netdev0,hostfwd=tcp::8080-10.0.2.15:80,hostfwd=tcp::8081-10.0.2.16:80 \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors -smp 4

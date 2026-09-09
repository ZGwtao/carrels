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
SYSTEM_FILE := container.system
IMAGE_FILE := container.img
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
QEMU_DISK_PARTITION_COUNT := $(shell expr $(PROTOCON_COUNT) + 2)
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

include ${SDDF}/tools/make/board/common.mk

VSWITCH:= ${SDDF}/examples/vswitch
METAPROGRAM := $(CONTAINER_DIR)/meta/meta.py
ETHERNET_DRIVER := $(SDDF)/drivers/network/$(NET_DRIV_DIR)
RAMDISK_INITIALISER := $(CONTAINER_DIR)/refresh-ramdisk.py
FAT := $(CARRELS)/components/fs/fat
NETWORK_COMPONENTS := $(SDDF)/network/components

# Use the board's default block device unless NVMe is explicitly requested.
# For x86_64/QEMU: make ... NVME=1
NVME ?= 1
ifeq ($(NVME),1)
ifneq ($(ARCH),x86_64)
$(error NVME=1 is currently supported only on x86_64)
endif
BLK_DRIV_DIR := nvme
QEMU_BLK_ARGS := -device nvme,drive=hd,serial=carrels,addr=0x4.0
BLK_META_ARGS := --nvme
CFLAGS += -DCARRELS_BLK_NVME
else ifeq ($(NVME),0)
BLK_META_ARGS :=
CFLAGS += -DCARRELS_BLK_BOARD_DEFAULT
else
$(error NVME must be either 0 or 1)
endif

vpath %.c ${SDDF} ${VSWITCH}

CFLAGS += \
	-DSDDF_VIRTIO_PCI_TRANSPORT_SKIP_BUS_CHECK \
	-I$(CARRELS)/include \
	-I$(SDDF)/include/sddf/util/custom_libc \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(VSWITCH)/include \
	-I$(LIBMICROKITCO_PATH)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_CUSTOM_LIBC := 1
SDDF_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk
include ${SDDF}/drivers/acpi/acpi_driver.mk
include ${SDDF}/drivers/pci/pci_driver.mk

include ${SDDF}/network/components/network_components.mk
include ${ETHERNET_DRIVER}/eth_driver.mk

%.py: ${CONTAINER_DIR}/%.py
	cp $< $@

LIBTRUSTEDLO_PATH ?= $(CARRELS)/dep/libtrustedlo
PROTOCON_VM_LAYOUT := $(LIBTRUSTEDLO_PATH)/config/vm_layout.py
MONITOR_VM_LAYOUT := $()

FAT_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
include $(FAT)/fat.mk

CONTAINER_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
CONTAINER_COMPONENT_DIR := $(CARRELS)
include $(CONTAINER_COMPONENT_DIR)/pc.mk

LIBMICROKITCO_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk


include $(ROOT)/uk-on-mk.mk

INFRA_IMAGES := \
	timer_driver.elf \
	eth_driver.elf network_virt_rx.elf network_virt_tx.elf network_vswitch.elf network_copy.elf \
	monitor.elf \
	orchestrator.elf \
	fat.elf \
	acpi_driver.elf pci_driver.elf \
	trampoline.elf \
	protocon.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

NATIVE_APPLICATION_IMAGES := $(PC_SERVICE_IMGS)
UNIKRAFT_APPLICATION_IMAGES := $(UNIKERNELS)
APPLICATION_IMAGES := $(NATIVE_APPLICATION_IMAGES) $(UNIKRAFT_APPLICATION_IMAGES)

$(INFRA_IMAGES) $(APPLICATION_IMAGES): libsddf_util_debug.a

FORCE:

LAYOUT_CMD := \
	--vm-layout $(PROTOCON_VM_LAYOUT) \
	--monitor-vm-layout $(CONTAINER_COMPONENT_DIR)/config/monitor_vm_layout.py


$(SYSTEM_FILE): $(METAPROGRAM) $(INFRA_IMAGES) $(DTB)
ifneq ($(strip $(DTS)),)
	$(PYTHON) -B \
	    $(METAPROGRAM) --sddf $(SDDF) --board $(META_BOARD) $(LAYOUT_CMD) \
	    --dtb $(DTB) --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) \
	    --protocon-count $(PROTOCON_COUNT) \
	    --x86-device-profile $(X86_DEVICE_PROFILE) $(BLK_META_ARGS)
else
	$(PYTHON) -B \
	    $(METAPROGRAM) --sddf $(SDDF) --board $(META_BOARD) $(LAYOUT_CMD) \
	    --output . --sdf $(SYSTEM_FILE) --objcopy $(OBJCOPY) \
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
	$(OBJCOPY) --update-section .serial_client_config=serial_client_orchestrator.data orchestrator.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_orchestrator.data orchestrator.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf

$(IMAGE_FILE) $(REPORT_FILE): $(INFRA_IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) \
		--search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) 	\
		--config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

infra: $(IMAGE_FILE)

apps: app-native app-uk
app-native: $(NATIVE_APPLICATION_IMAGES)
app-uk: $(UNIKRAFT_APPLICATION_IMAGES)

refresh-ramdisk: $(RAMDISK_INITIALISER) qemu_disk
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) \
		$(RAMDISK_INITIALISER) $(BUILD_DIR)

qemu_disk: FORCE
	$(SDDF)/tools/mkvirtdisk $@ $(QEMU_DISK_PARTITION_COUNT) 512 $(QEMU_DISK_SIZE_BYTES) GPT

ramdisk: refresh-ramdisk

qemu:
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_BLK_ARGS) $(QEMU_NET_ARGS) \
		-nographic \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-netdev user,id=netdev0,hostfwd=tcp::8080-10.0.2.15:80,hostfwd=tcp::8081-10.0.2.16:80 \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors -smp 4

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${CARRELS}/dep/sddf/include &:
	cd $(CARRELS) && git submodule update --init --recursive
	@test ! -e $(CARRELS)/dep/uk-on-mk/dep/sddf || test -L $(CARRELS)/dep/uk-on-mk/dep/sddf || { echo "refusing to replace non-symlink $(CARRELS)/dep/uk-on-mk/dep/sddf" >&2; exit 1; }
	ln -sfn ../../sddf $(CARRELS)/dep/uk-on-mk/dep/sddf

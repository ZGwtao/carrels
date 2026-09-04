# Copyright 2025, UNSW
# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

SUPPORTED_BOARDS:= \
	qemu_virt_aarch64 \
	maaxboard \
	odroidc4

UK_ON_MK_DIR ?= $(CARRELS)/dep/uk-on-mk
TOOLCHAIN ?= clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF ?= $(CARRELS)/dep/sddf
LIBMICROKITCO_PATH := $(CARRELS)/dep/libmicrokitco
SYSTEM_FILE := container.system
IMAGE_FILE := container.img
REPORT_FILE := report.txt


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

vpath %.c ${SDDF} ${VSWITCH}

CFLAGS += \
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


system: $(METAPROGRAM) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) -B $(METAPROGRAM) \
	--sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --objcopy $(OBJCOPY) \
	--vm-layout $(PROTOCON_VM_LAYOUT) --monitor-vm-layout $(CONTAINER_COMPONENT_DIR)/config/monitor_vm_layout.py \
	--output . --sdf $(SYSTEM_FILE)


$(SYSTEM_FILE): $(METAPROGRAM) $(INFRA_IMAGES) $(DTB)
	cp network_copy.elf network_copy0.elf
	cp network_copy.elf network_copy1.elf
	cp network_copy.elf network_copy2.elf
	cp network_copy.elf network_copy3.elf
	cp network_copy.elf network_copy4.elf
	cp network_copy.elf network_copy5.elf
	cp network_copy.elf network_copy6.elf
	cp network_copy.elf network_copy7.elf
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) -B $(METAPROGRAM) \
	--sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --objcopy $(OBJCOPY) \
	--vm-layout $(PROTOCON_VM_LAYOUT) --monitor-vm-layout $(CONTAINER_COMPONENT_DIR)/config/monitor_vm_layout.py \
	--output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=eth_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client0_net_copier.data network_copy0.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client1_net_copier.data network_copy1.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client2_net_copier.data network_copy2.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client3_net_copier.data network_copy3.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client4_net_copier.data network_copy4.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client5_net_copier.data network_copy5.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client6_net_copier.data network_copy6.elf
	$(OBJCOPY) --update-section .net_copy_config=net_copy_client7_net_copier.data network_copy7.elf
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

qemu_disk:
	$(SDDF)/tools/mkvirtdisk $@ 2 512 134217728 GPT

ramdisk: refresh-ramdisk

qemu:
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-netdev user,id=netdev0,hostfwd=tcp::8080-10.0.2.15:80,hostfwd=tcp::8081-10.0.2.16:80 \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		$(QEMU_BLK_ARGS) \
		$(QEMU_NET_ARGS)

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${CARRELS}/dep/sddf/include &:
	cd $(CARRELS) && git submodule update --init --recursive
	@test ! -e $(CARRELS)/dep/uk-on-mk/dep/sddf || test -L $(CARRELS)/dep/uk-on-mk/dep/sddf || { echo "refusing to replace non-symlink $(CARRELS)/dep/uk-on-mk/dep/sddf" >&2; exit 1; }
	ln -sfn ../../sddf $(CARRELS)/dep/uk-on-mk/dep/sddf

# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

include $(SDDF)/tools/make/board/common.mk

ifneq ($(MICROKIT_BOARD),qemu_virt_aarch64)
$(error Kubernetes VM PoC currently supports MICROKIT_BOARD=qemu_virt_aarch64 only)
endif

ifeq ($(ARCH),aarch64)
CFLAGS += -include $(CARRELS)/include/sddf-arch-compat.h
endif

VSWITCH := $(SDDF)/examples/vswitch
LIONSOS := $(CARRELS)/dep/lionsos
ETHERNET_DRIVER := $(SDDF)/drivers/network/$(NET_DRIV_DIR)
FAT := $(CARRELS)/components/fat
NETWORK_COMPONENTS := $(SDDF)/network/components

# Match the storage device exposed by qemu.sh unless explicitly overridden.
ifeq ($(ARCH),x86_64)
NVME ?= 1
else
NVME ?= 0
endif
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

vpath %.c $(SDDF) $(VSWITCH) $(LIBVMM)

CFLAGS += \
	-DSDDF_VIRTIO_PCI_TRANSPORT_SKIP_BUS_CHECK \
	-DCARRELS_PROTOCON_COUNT=$(PROTOCON_COUNT) \
	-I$(CARRELS)/include \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include/sddf/util/custom_libc \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(VSWITCH)/include \
	-I$(LIBMICROKITCO_PATH) \
	-I$(LIBVMM)/include \
	-I$(LIBVMM_EXAMPLE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

BLK_DRIVER := $(SDDF)/drivers/blk/$(BLK_DRIV_DIR)
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_CUSTOM_LIBC := 1
SDDF_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
include $(SDDF)/util/util.mk
include $(SDDF)/drivers/timer/$(TIMER_DRIV_DIR)/timer_driver.mk
include $(SDDF)/drivers/serial/$(UART_DRIV_DIR)/serial_driver.mk
include $(SDDF)/serial/components/serial_components.mk
include $(SDDF)/libco/libco.mk
include $(BLK_DRIVER)/blk_driver.mk
include $(BLK_COMPONENTS)/blk_components.mk
ifeq ($(ARCH),x86_64)
include $(SDDF)/drivers/acpi/acpi_driver.mk
include $(SDDF)/drivers/pci/pci_driver.mk
endif

include $(SDDF)/network/components/network_components.mk
include $(ETHERNET_DRIVER)/eth_driver.mk

LIBVMM_LIBC_INCLUDE := $(SDDF)/include/sddf/util/custom_libc
include $(LIBVMM)/vmm.mk
include $(LIBVMM)/tools/linux/blk/blk_init.mk
include $(LIBVMM)/tools/linux/net/net_init.mk

%.py: $(CONTAINER_DIR)/%.py
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

include $(ROOT)/mk/unikernels.mk

$(SDDF)/tools/make/board/common.mk $(SDDF_MAKEFILES) \
		$(CARRELS)/dep/sddf/include &:
	cd $(CARRELS) && git submodule update --init --recursive
	@test ! -e $(CARRELS)/dep/uk-on-mk/dep/sddf || test -L $(CARRELS)/dep/uk-on-mk/dep/sddf || { echo "refusing to replace non-symlink $(CARRELS)/dep/uk-on-mk/dep/sddf" >&2; exit 1; }
	ln -sfn ../../sddf $(CARRELS)/dep/uk-on-mk/dep/sddf

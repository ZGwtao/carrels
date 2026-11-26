# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

SUPPORTED_BOARDS := qemu_virt_aarch64 x86_64_generic maaxboard
PROTOCON_COUNT := 16

UK_ON_MK_DIR := $(CARRELS)/dep/uk-on-mk
CONTAINER_DIR := $(BENCH_DIR)
TOOLCHAIN := clang
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF := $(CARRELS)/dep/sddf
LIBMICROKITCO_PATH := $(CARRELS)/dep/libmicrokitco
SYSTEM_FILE := bench_lifecycle.system
IMAGE_FILE := bench_lifecycle.img
REPORT_FILE := report.txt
META_BOARD := $(MICROKIT_BOARD)
ifeq ($(MICROKIT_BOARD),x86_64_generic)
X86_BOARD ?= qemu_virt_x86
META_BOARD := $(X86_BOARD)
endif

# The distributed SDK contains an SMP kernel only.  Supply the CPUs it expects,
# while leaving every benchmark PD on its default affinity (CPU 0), so the
# measured deployment path remains single-core.
QEMU_CPUS := 4

# Monitor objects capture CFLAGS while the shared component rules are included
# below, so profiling must be enabled before that include.
COMPONENTS_VMM := 0
BENCH_COLD_START_COUNT ?= 100
BENCH_SAMPLE_COUNT ?= 100
CFLAGS += \
	-DCARRELS_PROFILE \
	-DCOLD_START_COUNT=$(BENCH_COLD_START_COUNT) \
	-DSAMPLE_COUNT=$(BENCH_SAMPLE_COUNT)
ifeq ($(BENCH_PROGRESS),1)
CFLAGS += -DBENCH_PROGRESS
endif

include $(ROOT)/mk/components.mk

# Benchmark components use the sDDF serial virtualiser for output. Avoid the
# debug library, whose polling Microkit console path perturbs lifecycle timing.
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util.a

METAPROGRAM := $(BENCH_DIR)/meta.py
LAYOUT_ARGS := \
	--vm-layout $(PROTOCON_VM_LAYOUT) \
	--monitor-vm-layout $(CONTAINER_COMPONENT_DIR)/config/monitor_vm_layout.py

INFRA_IMAGES := \
	timer_driver.elf \
	serial_driver.elf serial_virt_rx.elf serial_virt_tx.elf \
	eth_driver.elf network_virt_rx.elf network_virt_tx.elf \
	network_vswitch.elf network_copy.elf \
	monitor.elf protocon.elf trampoline.elf \
	ramfs_server.elf benchmark_issuer.elf

BENCH_IMAGES := nop-native.img nop-unikraft.img
RAMFS_MONITOR_ROOTFS := $(BUILD_DIR)/uk/bench-monitor-rootfs
RAMFS_ISSUER_ROOTFS := $(BUILD_DIR)/uk/bench-issuer-rootfs
RAMFS_PHASE1_STAMP := .ramfs-phase1
RAMFS_PHASE2_STAMP := .ramfs-phase2
PATCH_STAMP := .patched

issuer.o: $(BENCH_DIR)/issuer.c
	$(CC) -c $(PC_CFLAGS) $(CFLAGS) $< -o $@

benchmark_issuer.elf: issuer.o $(PC_FS_HELPERS_OBJ) pc/util/pico_vfs.o \
		$(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a
	$(LD) $(LDFLAGS) -L$(BOARD_DIR)/lib $^ $(LIBS) -o $@

nop-native.o: $(BENCH_DIR)/nop_native.c
	$(CC) -c $(PC_CFLAGS) $(CFLAGS) $< -o $@

nop-native.elf: nop-native.o pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a
	$(LD) $(LDFLAGS) -L$(BOARD_DIR)/lib -Ttext=0x2800000 $^ $(LIBS) -o $@

nop-native.img: nop-native.elf $(BENCH_DIR)/nop.mf $(CARRELS)/tools/service-helper.py
	$(PYTHON) $(CARRELS)/tools/service-helper.py \
		--mf $(BENCH_DIR)/nop.mf --elf $< -o $@

uk-nop: libsddf_util.a
	$(MAKE) -f $(UK_ON_MK_DIR)/Makefile $(UKF_FLAGS) \
		TARGET_APP=c-hello \
		UKF_APP_ENTRY=$(BENCH_DIR)/unikraft \
		uk-catalog-build

nop-unikraft.elf: uk-nop
	cp $(BUILD_DIR)/uk/c-hello/c-hello.elf $@

nop-unikraft.img: nop-unikraft.elf $(BENCH_DIR)/nop.mf $(CARRELS)/tools/service-helper.py
	$(PYTHON) $(CARRELS)/tools/service-helper.py \
		--mf $(BENCH_DIR)/nop.mf --elf $< -o $@

$(INFRA_IMAGES) $(BENCH_IMAGES): libsddf_util.a

$(SYSTEM_FILE): $(METAPROGRAM) $(INFRA_IMAGES) $(DTB)
ifneq ($(strip $(DTS)),)
	PYTHONPATH=$(CARRELS)/examples/simple/meta $(PYTHON) -B $(METAPROGRAM) \
		--sddf $(SDDF) --board $(META_BOARD) --dtb $(DTB) \
		--output . --sdf $@ --objcopy $(OBJCOPY) $(LAYOUT_ARGS) \
		--protocon-count $(PROTOCON_COUNT)
else
	PYTHONPATH=$(CARRELS)/examples/simple/meta $(PYTHON) -B $(METAPROGRAM) \
		--sddf $(SDDF) --board $(META_BOARD) \
		--output . --sdf $@ --objcopy $(OBJCOPY) $(LAYOUT_ARGS) \
		--protocon-count $(PROTOCON_COUNT)
endif

$(RAMFS_PHASE1_STAMP): $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) \
		-o bench_lifecycle.seed.img -r report.seed.txt
	cp build/delegation/container_monitor.dlg container_monitor.dlg
	touch $@

$(RAMFS_PHASE2_STAMP): $(RAMFS_PHASE1_STAMP) $(BENCH_IMAGES)
	$(PYTHON) $(CARRELS)/tools/prepare_ramfs_rootfs.py \
		monitor $(BUILD_DIR) $(RAMFS_MONITOR_ROOTFS)
	$(call ukf_build_svc,ramfs,LIONSOS=$(LIONSOS) \
		UKF_SVC_BUILD=$(BUILD_DIR)/uk/bench-monitor \
		UKF_SVC_ROOTFS=$(RAMFS_MONITOR_ROOTFS))
	cp $(BUILD_DIR)/uk/bench-monitor/ramfs-server.elf monitor_fs.elf
	$(PYTHON) $(CARRELS)/tools/prepare_ramfs_rootfs.py \
		orchestrator $(BUILD_DIR) $(RAMFS_ISSUER_ROOTFS) $(BENCH_IMAGES)
	$(call ukf_build_svc,ramfs,LIONSOS=$(LIONSOS) \
		UKF_SVC_BUILD=$(BUILD_DIR)/uk/bench-issuer \
		UKF_SVC_ROOTFS=$(RAMFS_ISSUER_ROOTFS))
	cp $(BUILD_DIR)/uk/bench-issuer/ramfs-server.elf issuer_fs.elf
	touch $@

$(PATCH_STAMP): $(RAMFS_PHASE2_STAMP) $(INFRA_IMAGES)
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=ethernet_driver_device_resources.data eth_driver.elf
	$(OBJCOPY) --update-section .net_driver_config=net_driver.data eth_driver.elf
	$(OBJCOPY) --update-section .net_virt_rx_config=net_virt_rx.data network_virt_rx.elf
	$(OBJCOPY) --update-section .net_virt_tx_config=net_virt_tx.data network_virt_tx.elf
	$(OBJCOPY) --update-section .net_vswitch_config=net_vswitch.data network_vswitch.elf
	$(OBJCOPY) --update-section .net_vswitch_orchestrator_config=net_vswitch_orchestrator.data monitor.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_benchmark_issuer.data benchmark_issuer.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_benchmark_issuer.data benchmark_issuer.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_benchmark_issuer.data benchmark_issuer.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_monitor_fs.data monitor_fs.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_monitor_fs.data monitor_fs.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_monitor_fs.data monitor_fs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_issuer_fs.data issuer_fs.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_issuer_fs.data issuer_fs.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_issuer_fs.data issuer_fs.elf
	@for i in $$(seq 0 $$(($(PROTOCON_COUNT) - 1))); do \
		cp network_copy.elf network_copy$$i.elf; \
		$(OBJCOPY) --update-section \
			.net_copy_config=net_copy_protocon$${i}_net_copier.data \
			network_copy$$i.elf; \
	done
	touch $@

$(IMAGE_FILE) $(REPORT_FILE): $(SYSTEM_FILE) $(PATCH_STAMP)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) \
		-o $(IMAGE_FILE) -r $(REPORT_FILE)

.PHONY: all qemu clean clobber
all: $(IMAGE_FILE)

qemu: $(IMAGE_FILE)
	$(QEMU) $(QEMU_ARCH_ARGS) $(QEMU_NET_ARGS) \
		-nographic -netdev user,id=netdev0 \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors -m size=4G -smp $(QEMU_CPUS)

clean::
	rm -f issuer.o nop-native.o *.elf *.img *.data *.json *.system report*.txt \
		$(RAMFS_PHASE1_STAMP) $(RAMFS_PHASE2_STAMP) $(PATCH_STAMP)

clobber:: clean
	rm -rf build symbols $(BUILD_DIR)/uk/bench-*

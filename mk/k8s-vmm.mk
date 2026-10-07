# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

K8S_VM_DIR := k8s_vm
K8S_VM_CLIENT := $(LIBVMM_EXAMPLE)/client_vm/aarch64
K8S_GUEST_OUT ?= $(CARRELS)/guest/k8s/build
K8S_VM_LINUX ?= $(K8S_GUEST_OUT)/linux/arch/arm64/boot/Image
K8S_VM_INITRD ?= $(K8S_GUEST_OUT)/rootfs.cpio.gz
K8S_VM_NET_INIT := $(CARRELS)/guest/k8s/net_client_init
K8S_VM_PACKED_INITRD := $(K8S_VM_DIR)/rootfs.cpio.gz
K8S_VM_DTS := $(K8S_VM_DIR)/vm.dts
K8S_VM_DTB := $(K8S_VM_DIR)/vm.dtb

$(K8S_VM_DIR):
	mkdir -p $@

$(K8S_VM_PACKED_INITRD): $(K8S_VM_INITRD) blk_client_init \
		$(K8S_VM_NET_INIT) | $(K8S_VM_DIR)
	$(LIBVMM)/tools/packrootfs $(K8S_VM_INITRD) \
		$(K8S_VM_DIR)/rootfs_staging -o $@ \
		--startup blk_client_init $(K8S_VM_NET_INIT)

$(K8S_VM_DTS): $(K8S_VM_CLIENT)/linux.dts \
		$(K8S_VM_CLIENT)/gic_v2_overlay.dts \
		$(ROOT)/mk/k8s-vmm.mk $(K8S_VM_PACKED_INITRD) | $(K8S_VM_DIR)
	$(LIBVMM)/tools/dtscat $(word 1,$^) $(word 2,$^) > $@
	@initrd_size=$$(stat -c %s $(K8S_VM_PACKED_INITRD)); \
	initrd_end=$$((0x47000000 + initrd_size)); \
	test $$initrd_end -lt $$((0x50000000)) || { echo "k8s initramfs does not fit in guest RAM"; exit 1; }; \
	initrd_end_hex=$$(printf '0x%x' $$initrd_end); \
	sed -i "s/linux,initrd-end = <0x48000000>/linux,initrd-end = <$$initrd_end_hex>/" $@; \
	grep -q "linux,initrd-end = <$$initrd_end_hex>" $@ || { echo "failed to update k8s initramfs end in $@"; exit 1; }

$(K8S_VM_DTB): $(K8S_VM_DTS)
	$(DTC) -q -I dts -O dtb $< > $@

$(K8S_VM_DIR)/vmm.o: $(LIBVMM_EXAMPLE)/client_vmm.c | $(K8S_VM_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(K8S_VM_DIR)/guest_arch_init.o: \
		$(K8S_VM_CLIENT)/guest_arch_init.c | $(K8S_VM_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(K8S_VM_DIR)/images.o: $(LIBVMM)/tools/package_guest_images.S \
		$(K8S_VM_LINUX) $(K8S_VM_DTB) $(K8S_VM_PACKED_INITRD) | $(K8S_VM_DIR)
	$(CC) -c -g3 -x assembler-with-cpp \
		-DGUEST_KERNEL_IMAGE_PATH=\"$(K8S_VM_LINUX)\" \
		-DGUEST_DTB_IMAGE_PATH=\"$(K8S_VM_DTB)\" \
		-DGUEST_INITRD_IMAGE_PATH=\"$(K8S_VM_PACKED_INITRD)\" \
		-target $(TARGET) $< -o $@

k8s_vmm.elf: $(K8S_VM_DIR)/vmm.o $(K8S_VM_DIR)/guest_arch_init.o \
		$(K8S_VM_DIR)/images.o libvmm.a libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

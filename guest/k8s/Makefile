# SPDX-License-Identifier: BSD-2-Clause

CARRELS := $(abspath ../..)
EXTERNAL_DIR := $(CARRELS)/external
include $(EXTERNAL_DIR)/versions.mk

ARCH := arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
JOBS ?= $(shell nproc)
OUT ?= $(CURDIR)/build
LINUX_OUT := $(OUT)/linux
BUSYBOX_OUT := $(OUT)/busybox
ROOTFS := $(OUT)/rootfs
IMAGE := $(LINUX_OUT)/arch/arm64/boot/Image
INITRAMFS := $(OUT)/rootfs.cpio.gz

KIND_CONTROL_PLANE ?= libvmm-poc-control-plane
KUBELET_NODE_NAME ?= sel4-worker
KUBELET_API_SERVER ?= https://10.0.2.2:6443
KUBELET_KUBECONFIG := $(CURDIR)/credentials/kubelet.conf
KUBELET_CREDENTIAL_DEP := $(wildcard $(KUBELET_KUBECONFIG))

.PHONY: all clean external kernel rootfs kubelet-credentials check-sources

all: external kernel rootfs

kernel rootfs: | external

external:
	$(MAKE) -C $(EXTERNAL_DIR) all

check-sources:
	@test -f "$(LINUX_SRC)/Makefile" || { echo "LINUX_SRC is not a Linux source tree: $(LINUX_SRC)"; exit 1; }
	@test -f "$(BUSYBOX_SRC)/Makefile" || { echo "BUSYBOX_SRC is not a BusyBox source tree: $(BUSYBOX_SRC)"; exit 1; }

$(LINUX_OUT)/.config: kernel.fragment | check-sources
	mkdir -p $(LINUX_OUT)
	$(MAKE) -C $(LINUX_SRC) O=$(LINUX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) tinyconfig
	$(LINUX_SRC)/scripts/kconfig/merge_config.sh -m -O $(LINUX_OUT) $@ $(abspath kernel.fragment)
	$(MAKE) -C $(LINUX_SRC) O=$(LINUX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) olddefconfig

kernel: $(IMAGE)

$(IMAGE): $(LINUX_OUT)/.config
	$(MAKE) -C $(LINUX_SRC) O=$(LINUX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -j$(JOBS) Image

$(BUSYBOX_OUT)/.config: busybox.fragment | check-sources
	mkdir -p $(BUSYBOX_OUT)
	$(MAKE) -C $(BUSYBOX_SRC) O=$(BUSYBOX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) allnoconfig
	$(LINUX_SRC)/scripts/kconfig/merge_config.sh -m -O $(BUSYBOX_OUT) $@ $(abspath busybox.fragment)
	$(MAKE) -C $(BUSYBOX_SRC) O=$(BUSYBOX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) oldconfig </dev/null

$(BUSYBOX_OUT)/busybox: $(BUSYBOX_OUT)/.config
	$(MAKE) -C $(BUSYBOX_SRC) O=$(BUSYBOX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -j$(JOBS)

rootfs: $(INITRAMFS)

$(INITRAMFS): $(BUSYBOX_OUT)/busybox $(KUBELET) $(CRI_STUB) $(KUBELET_CREDENTIAL_DEP) init Makefile busybox.fragment kubelet-config.yaml passwd group
	mkdir -p $(ROOTFS)/bin $(ROOTFS)/dev $(ROOTFS)/etc/init.d $(ROOTFS)/etc/kubernetes/manifests $(ROOTFS)/mnt $(ROOTFS)/proc $(ROOTFS)/run $(ROOTFS)/sys $(ROOTFS)/tmp $(ROOTFS)/usr/share/udhcpc $(ROOTFS)/var/lib/kubelet $(ROOTFS)/var/log/pods
	$(MAKE) -C $(BUSYBOX_SRC) O=$(BUSYBOX_OUT) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CONFIG_PREFIX=$(ROOTFS) install
	install -m 0755 init $(ROOTFS)/init
	install -m 0755 $(KUBELET) $(ROOTFS)/bin/kubelet
	install -m 0755 $(CRI_STUB) $(ROOTFS)/bin/cri-stub
	install -m 0644 kubelet-config.yaml $(ROOTFS)/etc/kubelet-config.yaml
	install -m 0644 passwd $(ROOTFS)/etc/passwd
	install -m 0644 group $(ROOTFS)/etc/group
	@if test -f "$(KUBELET_KUBECONFIG)"; then \
		install -m 0600 "$(KUBELET_KUBECONFIG)" $(ROOTFS)/etc/kubernetes/kubelet.conf; \
	else \
		rm -f $(ROOTFS)/etc/kubernetes/kubelet.conf; \
		echo "note: building without kubelet credentials"; \
	fi
	install -m 0755 $(BUSYBOX_SRC)/examples/udhcp/simple.script $(ROOTFS)/usr/share/udhcpc/default.script
	cd $(ROOTFS) && find . -print0 | cpio --null -o --format=newc | gzip -9 > $(INITRAMFS)

kubelet-credentials:
	mkdir -p $(dir $(KUBELET_KUBECONFIG))
	docker exec $(KIND_CONTROL_PLANE) kubeadm kubeconfig user \
		--client-name=system:node:$(KUBELET_NODE_NAME) \
		--org=system:nodes --validity-period=8760h > $(KUBELET_KUBECONFIG).tmp
	sed -i 's#server: .*#server: $(KUBELET_API_SERVER)#' $(KUBELET_KUBECONFIG).tmp
	# The current guest has no reliable clock during early boot.
	sed -i '/certificate-authority-data:/c\    insecure-skip-tls-verify: true' $(KUBELET_KUBECONFIG).tmp
	chmod 0600 $(KUBELET_KUBECONFIG).tmp
	mv $(KUBELET_KUBECONFIG).tmp $(KUBELET_KUBECONFIG)

clean:
	@test ! -d "$(LINUX_SRC)" || $(MAKE) -C $(LINUX_SRC) O=$(LINUX_OUT) ARCH=$(ARCH) clean
	@test ! -d "$(BUSYBOX_SRC)" || $(MAKE) -C $(BUSYBOX_SRC) O=$(BUSYBOX_OUT) clean
	rm -rf $(ROOTFS) $(INITRAMFS)

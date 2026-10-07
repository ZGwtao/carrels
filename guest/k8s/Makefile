# SPDX-License-Identifier: BSD-2-Clause

CARRELS := $(abspath ../..)
LIBVMM_GUEST := $(CARRELS)/dep/libvmm/examples/virtio_pci/guest
OUT ?= $(CURDIR)/build
DOWNLOADS := $(OUT)/downloads
SOURCES := $(OUT)/src

LINUX_VERSION := 6.12
LINUX_ARCHIVE := $(DOWNLOADS)/linux-$(LINUX_VERSION).tar.xz
LINUX_URL := https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$(LINUX_VERSION).tar.xz
LINUX_SHA256 := b1a2562be56e42afb3f8489d4c2a7ac472ac23098f1ef1c1e40da601f54625eb
LINUX_SRC := $(SOURCES)/linux-$(LINUX_VERSION)

BUSYBOX_VERSION := 1.37.0
BUSYBOX_TARBALL := $(DOWNLOADS)/busybox-$(BUSYBOX_VERSION).tar.bz2
BUSYBOX_URL := https://busybox.net/downloads/busybox-$(BUSYBOX_VERSION).tar.bz2
BUSYBOX_SHA256 := 3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4

KIND_CONTROL_PLANE ?= libvmm-poc-control-plane
KUBELET_NODE_NAME ?= sel4-worker
KUBELET_API_SERVER ?= https://10.0.2.2:6443
KUBELET_KUBECONFIG := $(CURDIR)/credentials/kubelet.conf

.PHONY: all clean kubelet-credentials sources
all: sources
	$(MAKE) -C $(LIBVMM_GUEST) OUT=$(OUT) \
		LINUX_SRC=$(LINUX_SRC) BUSYBOX_TARBALL=$(BUSYBOX_TARBALL) \
		KUBELET_KUBECONFIG=$(KUBELET_KUBECONFIG) all

sources: $(LINUX_SRC)/Makefile $(BUSYBOX_TARBALL)

$(LINUX_ARCHIVE):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(LINUX_URL) -o $@.tmp
	echo "$(LINUX_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

$(LINUX_SRC)/Makefile: $(LINUX_ARCHIVE)
	mkdir -p $(SOURCES)
	tar -C $(SOURCES) -xf $<
	test -f $@

$(BUSYBOX_TARBALL):
	mkdir -p $(dir $@)
	curl --fail --location --retry 3 $(BUSYBOX_URL) -o $@.tmp
	echo "$(BUSYBOX_SHA256)  $@.tmp" | sha256sum --check -
	mv $@.tmp $@

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
	$(MAKE) -C $(LIBVMM_GUEST) OUT=$(OUT) clean

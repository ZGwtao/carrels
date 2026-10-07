# SPDX-License-Identifier: BSD-2-Clause

CARRELS := $(abspath ../..)
LIBVMM_GUEST := $(CARRELS)/dep/libvmm/examples/virtio_pci/guest
EXTERNAL_DIR := $(CARRELS)/external
include $(EXTERNAL_DIR)/versions.mk
OUT ?= $(CURDIR)/build

KIND_CONTROL_PLANE ?= libvmm-poc-control-plane
KUBELET_NODE_NAME ?= sel4-worker
KUBELET_API_SERVER ?= https://10.0.2.2:6443
KUBELET_KUBECONFIG := $(CURDIR)/credentials/kubelet.conf

.PHONY: all clean kubelet-credentials external
all: external
	$(MAKE) -C $(LIBVMM_GUEST) OUT=$(OUT) \
		LINUX_SRC=$(LINUX_SRC) BUSYBOX_SRC=$(BUSYBOX_SRC) \
		PREBUILT_KUBELET=$(KUBELET) PREBUILT_CRI_STUB=$(CRI_STUB) \
		KUBELET_KUBECONFIG=$(KUBELET_KUBECONFIG) all

external:
	$(MAKE) -C $(EXTERNAL_DIR) all

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

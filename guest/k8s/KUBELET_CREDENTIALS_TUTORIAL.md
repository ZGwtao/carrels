# Kubelet credentials

The VM kubelet uses a dedicated Kubernetes node identity. Do not give it the
kind administrator kubeconfig.

## Issue the credential

Create the kind cluster first and make sure its control-plane container is
running. The default container name is `libvmm-poc-control-plane`.

```sh
make -C guest/k8s kubelet-credentials
```

This asks `kubeadm` inside the control-plane container to sign a client
certificate with:

- common name: `system:node:sel4-worker`
- organization: `system:nodes`
- validity: one year

The node name must match the kubelet's `--hostname-override`. Override the
defaults when necessary:

```sh
make -C guest/k8s kubelet-credentials \
    KIND_CONTROL_PLANE=my-cluster-control-plane \
    KUBELET_NODE_NAME=sel4-worker \
    KUBELET_API_SERVER=https://10.0.2.2:6443
```

The result is written to `guest/k8s/credentials/kubelet.conf` with mode 0600.
It is ignored by Git because it contains the node's private key.

## Verify and use it

```sh
test -s guest/k8s/credentials/kubelet.conf
kubectl --kubeconfig guest/k8s/credentials/kubelet.conf \
    auth can-i get node/sel4-worker
make -C guest/k8s
```

The current PoC disables server certificate verification because the minimal
guest has no reliable clock during early boot. Once reliable time is available,
remove that substitution from the Makefile and retain the generated CA data.

Run the credential target again to rotate the certificate, then rebuild the
guest initramfs.

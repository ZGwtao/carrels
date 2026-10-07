# Kubernetes guest

This directory builds the arm64 Linux/initramfs used by the `k8s_vmm` PD.  It
reuses the guest sources in `dep/libvmm/examples/virtio_pci/guest`.

Generate the kubelet's least-privilege kubeconfig as described in
[`KUBELET_CREDENTIALS_TUTORIAL.md`](KUBELET_CREDENTIALS_TUTORIAL.md), then run:

```sh
make -C guest/k8s
```

The infrastructure build consumes `build/linux/arch/arm64/boot/Image` and
`build/rootfs.cpio.gz`. The first build downloads pinned Linux 6.12 and BusyBox
1.37.0 source archives, verifies their SHA-256 checksums, and caches them under
`build/downloads`. Later builds reuse the cached sources.

For the current PoC, `carrels.local/c-hello:latest` maps explicitly to
`c-hello.img` in the first qemu disk partition. `PullImage` succeeds only when
that file was present while the vsock backend mounted and scanned the disk.

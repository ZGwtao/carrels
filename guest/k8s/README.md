# Kubernetes guest

This directory owns and builds the arm64 Linux/initramfs used by the `k8s_vmm`
PD. Kubernetes guest policy and configuration live here rather than in libvmm.

Generate the kubelet's least-privilege kubeconfig as described in
[`KUBELET_CREDENTIALS_TUTORIAL.md`](KUBELET_CREDENTIALS_TUTORIAL.md), then run:

```sh
make -C guest/k8s
```

The infrastructure build consumes `build/linux/arch/arm64/boot/Image` and
`build/rootfs.cpio.gz`. Third-party versions, downloads, sources, the Go
toolchain, and the kubelet build are managed by [`../../external`](../../external/README.md).
The first build downloads and verifies the pinned inputs; later builds reuse
the ignored caches under `external/`.

For the current PoC, `carrels.local/c-hello:latest` maps explicitly to
`c-hello.img` in the first qemu disk partition. `PullImage` succeeds only when
that file was present while the vsock backend mounted and scanned the disk.

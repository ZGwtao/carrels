# Copyright 2026, UNSW
# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

import sys
import argparse
import importlib
from pathlib import Path
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from typing import Optional

from elf import Elftools
from vspace import VSpace
from infra import CarrelsContainerInfra as Infra


SDF = SystemDescription
PD = SDF.ProtectionDomain
MR = SDF.MemoryRegion
MAP = SDF.Map
IOPORT = SDF.IoPort
IRQIOAPIC = SDF.IrqIoapic

def generate(
    sdf_path: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    nvme: bool,
    protocon_count: int,
):
    # sDDF blk_virt supports at most 1024 concurrent requests across all
    # block clients. Each FATFS instance contributes one such client.
    fatfs_client_count = protocon_count + 2
    fatfs_capacity_limit = 1024 // fatfs_client_count
    fatfs_blk_queue_capacity = min(
        128, 1 << (fatfs_capacity_limit.bit_length() - 1)
    )
    serial_node = None
    blk_node = None
    timer_node = None
    net_node = None

    if dtb is not None:
        serial_node = dtb.node(board.serial)
        assert serial_node is not None
        blk_node = dtb.node(board.blk)
        assert blk_node is not None
        timer_node = dtb.node(board.timer)
        assert timer_node is not None
        net_node = dtb.node(board.ethernet)
        assert net_node is not None

    timer_driver = PD("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    if board.arch == SystemDescription.Arch.X86_64:
        hpet_irq = IRQIOAPIC(
            ioapic_id=0,
            pin=2,
            vector=107,
            id=0,
            trigger=IRQIOAPIC.Trigger.EDGE,
        )
        timer_driver.add_irq(hpet_irq)
        hpet_regs = MR(sdf, "hpet_regs", 0x1000, paddr=0xFED00000)
        hpet_regs_map = MAP(hpet_regs, 0x5000_0000, "rw", cached=False)
        timer_driver.add_map(hpet_regs_map)
        sdf.add_mr(hpet_regs)

    serial_driver = PD("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = PD("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = PD("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver,
                                serial_virt_tx, virt_rx=serial_virt_rx)
    if board.arch == SystemDescription.Arch.X86_64:
        serial_port = IOPORT(0x3F8, 8, 0)
        serial_driver.add_ioport(serial_port)
        serial_driver.add_irq(
            IRQIOAPIC(ioapic_id=0, pin=4, vector=0, id=1)
        )

    blk_driver = PD("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = PD("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    if nvme:
        assert board.arch == SystemDescription.Arch.X86_64

        # These addresses are part of the current sDDF NVMe driver's ABI.
        dma_regions = [
            ("nvme_admin_sq", 0x5EDF0000, 0x20100000, 0x1000),
            ("nvme_admin_cq", 0x5EDF1000, 0x20101000, 0x1000),
            ("nvme_io_sq", 0x5EDF2000, 0x20102000, 0x1000),
            ("nvme_io_cq", 0x5EDF3000, 0x20103000, 0x1000),
            ("nvme_identify", 0x5EDF4000, 0x20104000, 0x2000),
            ("nvme_prp_list", 0x5F800000, 0x20200000, 0x80000),
        ]
        for name, paddr, vaddr, size in dma_regions:
            mr = MR(sdf, name, size, paddr=paddr)
            sdf.add_mr(mr)
            blk_driver.add_map(MAP(mr, vaddr, "rw", cached=False))

        # QEMU q35 assigns this BAR to the NVMe controller at 00:04.0.
        nvme_bar0 = MR(sdf, "nvme_bar0", 0x4000, paddr=0xFEB90000)
        sdf.add_mr(nvme_bar0)
        blk_driver.add_map(MAP(nvme_bar0, 0x20000000, "rw", cached=False))
        blk_driver.add_irq(
            IRQIOAPIC(
                ioapic_id=0, pin=10, vector=2, id=17,
                trigger=IRQIOAPIC.Trigger.LEVEL,
                polarity=IRQIOAPIC.Polarity.ACTIVELOW,
            )
        )

        # The x86 NVMe driver uses PCI configuration mechanism #1.
        blk_driver.add_ioport(IOPORT(0xCF8, 4, 1))
        blk_driver.add_ioport(IOPORT(0xCFC, 4, 2))

    elif board.arch == SystemDescription.Arch.X86_64:
        blk_requests_mr = MR(sdf, "virtio_requests", 65536, paddr=0x5FDF0000)
        sdf.add_mr(blk_requests_mr)
        blk_driver.add_map(MAP(blk_requests_mr, 0x20200000, "rw"))

        blk_virtio_metadata_mr = MR(
            sdf, "virtio_metadata", 65536, paddr=0x5FFF0000
        )
        sdf.add_mr(blk_virtio_metadata_mr)
        blk_driver.add_map(MAP(blk_virtio_metadata_mr, 0x20210000, "rw"))

        virtio_blk_regs = MR(
            sdf, "virtio_blk_regs", 0x4000, paddr=0xFE004000
        )
        sdf.add_mr(virtio_blk_regs)
        blk_driver.add_map(
            MAP(virtio_blk_regs, 0x60000000, "rw", cached=False)
        )
        blk_driver.add_irq(
            IRQIOAPIC(
                ioapic_id=0, pin=11, vector=2, id=17,
                trigger=IRQIOAPIC.Trigger.LEVEL,
                polarity=IRQIOAPIC.Polarity.ACTIVELOW,
            )
        )

    timer_system.add_client(blk_driver)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        timer_driver,
        blk_driver,
        blk_virt,
    ]
    for pd in pds:
        sdf.add_pd(pd)


    container_infra = Infra(
        sdf=sdf,
        layout_txlo=layout_txlo,
        layout_monitor=layout_monitor,
        client_limit=16,
    )
    container_infra.connect_orchestrator()
    protocons = container_infra.add_clients(protocon_count)
    pd_orchestrator = container_infra.pd_orchestrator
    pd_engine = container_infra.pd_engine

    pds = [
        pd_engine,
        pd_orchestrator,
        # template pds are not included...
    ]
    for pd in pds:
        sdf.add_pd(pd)


    serial_system.add_client(pd_orchestrator)
    serial_system.add_client(pd_engine)

    for pc in protocons:
        serial_system.add_client(pc, optional=True)
        timer_system.add_client(pc, optional=True)

    pd_fs_engine = PD("engine_fs", "engine_fs.elf", priority=96)
    pd_fs_orchestrator = PD("orchestrator_fs", "orchestrator_fs.elf", priority=96)
    engine_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_engine, pd_engine, blk=blk_system, partition=1,
        blk_queue_capacity=fatfs_blk_queue_capacity,
    )
    orchestrator_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_orchestrator, pd_orchestrator, blk=blk_system, partition=0,
        blk_queue_capacity=fatfs_blk_queue_capacity,
    )
    protocon_fs_pds = [
        PD(f"protocon{i}_fs", f"protocon{i}_fs.elf", priority=96)
        for i in range(protocon_count)
    ]
    protocon_filesystems = [
        LionsOs.FileSystem.Fat(
            sdf,
            fs_pd,
            protocon,
            blk=blk_system,
            # Partitions 0 and 1 belong to orchestrator and monitor.
            partition=i + 2,
            blk_queue_capacity=fatfs_blk_queue_capacity,
            optional=True,
        )
        for i, (protocon, fs_pd) in enumerate(zip(protocons, protocon_fs_pds))
    ]

    pds = [
        pd_fs_engine,
        pd_fs_orchestrator,
        *protocon_fs_pds,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    eth_driver = PD("eth_driver", "eth_driver.elf",
                    priority=101, budget=100, period=400)

    if board.arch == SystemDescription.Arch.X86_64:
        hw_net_rings = MR(
            sdf, "hw_net_rings", 65536, paddr=0x7A000000
        )
        sdf.add_mr(hw_net_rings)
        hw_net_rings_map = MAP(
            hw_net_rings, 0x7000_0000, "rw", cached=False
        )
        eth_driver.add_map(hw_net_rings_map)

        virtio_net_regs = MR(
            sdf, "virtio_net_regs", 0x4000, paddr=0xFE000000
        )
        sdf.add_mr(virtio_net_regs)
        virtio_net_regs_map = MAP(
            virtio_net_regs, 0x6000_0000, "rw", cached=False
        )
        eth_driver.add_map(virtio_net_regs_map)

        virtio_net_irq = IRQIOAPIC(
            ioapic_id=0,
            pin=10,
            vector=1,
            id=16,
            trigger=IRQIOAPIC.Trigger.LEVEL,
            polarity=IRQIOAPIC.Polarity.ACTIVELOW,
        )
        eth_driver.add_irq(virtio_net_irq)

    net_virt_tx = PD("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = PD("net_virt_rx", "network_virt_rx.elf", priority=99)
    net_vswitch = PD("net_vswitch", "network_vswitch.elf", priority=98)
    net_system = Sddf.Net(
        sdf,
        net_node,
        eth_driver,
        net_virt_tx,
        net_virt_rx,
        vswitch=net_vswitch,
        vswitch_orchestrator=pd_engine,
    )
    net_copiers = [
        PD(
            f"client{i}_net_copier",
            f"network_copy{i}.elf",
            priority=97,
            budget=20000,
        )
        for i in range(protocon_count)
    ]

    for protocon, net_copier in zip(protocons, net_copiers):
        net_system.add_client_with_copier(
            protocon, net_copier, vswitch=True, optional=True
        )

    pds = [
        eth_driver,
        net_virt_rx,
        net_virt_tx,
        net_vswitch,
        *net_copiers,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert orchestrator_fs.connect()
    assert orchestrator_fs.serialise_config(output_dir)
    assert engine_fs.connect()
    assert engine_fs.serialise_config(output_dir)
    for protocon_fs in protocon_filesystems:
        assert protocon_fs.connect()
        assert protocon_fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    assert net_system.connect()

    # Inter-protocon ACLs start closed. The monitor opens a pair after both
    # endpoints' targeted deployment policies allow that connection.
    for src in protocons:
        net_system.add_acl_rule(src, net_virt_tx, True, True)

    assert net_system.serialise_config(output_dir)

    # Each client gets an independent copier ELF and its matching config.
    # Keep this together with the dynamically-sized client list rather than
    # duplicating a fixed copier count in container.mk.
    for i, net_copier in enumerate(net_copiers):
        elf.copy_elf("network_copy", f"network_copy{i}")
        elf.update_elf_section(
            f"network_copy{i}.elf",
            "net_copy_config",
            f"net_copy_{net_copier.name}",
        )

    # generate all LionsOS services descriptors for engines.
    assert sdf.gensvc(output_dir)

    elf.copy_elf("fat", "orchestrator_fs", None)
    elf.copy_elf("fat", "engine_fs", None)
    for fs_pd in protocon_fs_pds:
        elf.copy_elf("fat", fs_pd.name, None)

    elf.update_elf_section("orchestrator_fs.elf", "blk_client_config", "blk_client_orchestrator_fs")
    elf.update_elf_section("orchestrator_fs.elf", "fs_server_config", "fs_server_orchestrator_fs")

    elf.update_elf_section("engine_fs.elf", "blk_client_config", "blk_client_engine_fs")
    elf.update_elf_section("engine_fs.elf", "fs_server_config", "fs_server_engine_fs")
    for fs_pd in protocon_fs_pds:
        elf.update_elf_section(
            f"{fs_pd.name}.elf", "blk_client_config", f"blk_client_{fs_pd.name}"
        )
        elf.update_elf_section(
            f"{fs_pd.name}.elf", "fs_server_config", f"fs_server_{fs_pd.name}"
        )

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


def load_boards(sddf_root: str):
    meta_dir = Path(sddf_root).resolve() / "tools" / "meta"
    sys.path.insert(0, str(meta_dir))
    board_mod = importlib.import_module("board")
    BOARDS = getattr(board_mod, "BOARDS")
    return BOARDS


if __name__ == "__main__":
    board_parser = argparse.ArgumentParser(add_help=False)
    board_parser.add_argument("--sddf", required=True)
    board_args, _ = board_parser.parse_known_args()
    sddf = Sddf(board_args.sddf)
    BOARDS = load_boards(board_args.sddf)
    parser = argparse.ArgumentParser(parents=[board_parser])
    parser.add_argument("--dtb", required=False)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--vm-layout", required=True,
                        help="path to libtrustedlo config/vm_layout.py")
    parser.add_argument("--monitor-vm-layout", required=True,
                        help="path to monitor config/vm_layout.py")
    parser.add_argument("--nvme", action="store_true", default=False)
    parser.add_argument("--protocon-count", type=int, default=8)

    args = parser.parse_args()

    layout_txlo = VSpace.load(args.vm_layout, "libtrustedlo_vm_layout")
    layout_monitor = VSpace.load(args.monitor_vm_layout, "monitor_vm_layout")

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SDF(board.arch, board.paddr_top)

    elf = Elftools(args.objcopy)

    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    if args.protocon_count <= 0 or args.protocon_count > 16:
        parser.error("--protocon-count must be in the range 1..16")

    generate(args.sdf, args.output, dtb, args.nvme, args.protocon_count)

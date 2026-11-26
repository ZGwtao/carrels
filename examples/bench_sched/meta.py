# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import importlib
import shutil
import sys
from pathlib import Path
from typing import Optional

from sdfgen import DeviceTree, LionsOs, Sddf, SystemDescription

from engine import CarrelsContainerEngine
from vspace import VSpace

PROTOCON_HEAP_SIZE = 0x1400000
MONITOR_PRIORITY = 95
WORKLOAD_PRIORITY = 60
SCHED_CONTROL_VADDR = 0x0C000000
SCHED_VICTIM_CHANNEL = 31


SDF = SystemDescription
PD = SDF.ProtectionDomain
MR = SDF.MemoryRegion
MAP = SDF.Map
Channel = SDF.Channel
IoPort = SDF.IoPort
IrqIoapic = SDF.IrqIoapic


def add_pds(sdf: SDF, *pds: PD) -> None:
    for pd in pds:
        sdf.add_pd(pd)


def device_nodes(dtb: Optional[DeviceTree]):
    if dtb is None:
        return None, None, None

    serial = dtb.node(board.serial)
    timer = dtb.node(board.timer)
    ethernet = dtb.node(board.ethernet)
    assert serial is not None
    assert timer is not None
    assert ethernet is not None
    return serial, timer, ethernet


def create_timer(sdf: SDF, node):
    driver = PD("timer_driver", "timer_driver.elf", priority=254)
    system = Sddf.Timer(sdf, node, driver)

    if board.arch == SDF.Arch.X86_64:
        registers = MR(sdf, "hpet_regs", 0x1000, paddr=0xFED00000)
        sdf.add_mr(registers)
        driver.add_map(MAP(registers, 0x50000000, "rw", cached=False))
        driver.add_irq(IrqIoapic(
            ioapic_id=0,
            pin=2,
            vector=107,
            id=0,
            trigger=IrqIoapic.Trigger.EDGE,
        ))

    sdf.add_pd(driver)
    return system


def create_serial(sdf: SDF, node):
    driver = PD("serial_driver", "serial_driver.elf", priority=100)
    virt_tx = PD("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    virt_rx = PD("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    system = Sddf.Serial(sdf, node, driver, virt_tx, virt_rx=virt_rx)

    if board.arch == SDF.Arch.X86_64:
        driver.add_ioport(IoPort(0x3F8, 8, 0))
        driver.add_irq(IrqIoapic(ioapic_id=0, pin=4, vector=0, id=1))

    add_pds(sdf, driver, virt_tx, virt_rx)
    return system


def create_network(sdf: SDF, node, monitor: PD):
    driver = PD(
        "ethernet_driver",
        "eth_driver.elf",
        priority=101,
        budget=100,
        period=400,
    )

    if board.arch == SDF.Arch.X86_64:
        rings = MR(sdf, "hw_net_rings", 0x10000, paddr=0x7A000000)
        registers = MR(sdf, "virtio_net_regs", 0x4000, paddr=0xFE000000)
        sdf.add_mr(rings)
        sdf.add_mr(registers)
        driver.add_map(MAP(rings, 0x70000000, "rw"))
        driver.add_map(MAP(registers, 0x60000000, "rw", cached=False))
        driver.add_irq(IrqIoapic(ioapic_id=0, pin=11, vector=1, id=16))
        driver.add_ioport(IoPort(0xCF8, 4, 1))
        driver.add_ioport(IoPort(0xCFC, 4, 2))

    virt_tx = PD("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    virt_rx = PD("net_virt_rx", "network_virt_rx.elf", priority=99)
    vswitch = PD("net_vswitch", "network_vswitch.elf", priority=98)
    system = Sddf.Net(
        sdf,
        node,
        driver,
        virt_tx,
        virt_rx,
        vswitch=vswitch,
        vswitch_orchestrator=monitor,
    )
    add_pds(sdf, driver, virt_tx, virt_rx, vswitch)
    return system, virt_tx


def create_container_infra(sdf: SDF, count: int):
    monitor = PD(
        "container_monitor",
        "monitor.elf",
        priority=MONITOR_PRIORITY,
        stack_size=0x10000,
        delegatee=True,
    )
    issuer = PD(
        "benchmark_issuer",
        "benchmark_issuer.elf",
        priority=WORKLOAD_PRIORITY,
        stack_size=0x10000,
    )
    engine = CarrelsContainerEngine(
        sdf=sdf,
        engine=monitor,
        orchestrator=issuer,
        layout_txlo=layout_txlo,
        layout_monitor=layout_monitor,
        cid_limit=count,
        uk_heap_size=PROTOCON_HEAP_SIZE,
    )
    engine.setup_boot_regions()
    engine.connect_orchestrator()
    protocons = [
        PD(
            f"protocon{i}",
            priority=WORKLOAD_PRIORITY,
            stack_size=0x10000,
            template=True,
            sym_emit=True,
            allow_delegation=True,
        )
        for i in range(count)
    ]
    for protocon in protocons:
        engine.add_client(protocon)

    add_pds(sdf, monitor, issuer)
    return engine, monitor, issuer, protocons


def create_ramfs(sdf: SDF, monitor: PD, issuer: PD):
    monitor_server = PD("monitor_fs", "monitor_fs.elf", priority=96, stack_size=0x20000)
    issuer_server = PD("issuer_fs", "issuer_fs.elf", priority=96, stack_size=0x20000)
    monitor_fs = LionsOs.FileSystem.Ram(sdf, monitor_server, monitor)
    issuer_fs = LionsOs.FileSystem.Ram(sdf, issuer_server, issuer)
    add_pds(sdf, monitor_server, issuer_server)
    return monitor_fs, issuer_fs


def connect_network_clients(sdf: SDF, network, protocons):
    copiers = []
    for index, protocon in enumerate(protocons):
        copier = PD(
            f"protocon{index}_net_copier",
            f"network_copy{index}.elf",
            priority=97,
            budget=20000,
        )
        network.add_client_with_copier(
            protocon,
            copier,
            vswitch=True,
            optional=True,
        )
        sdf.add_pd(copier)
        copiers.append(copier)
    return copiers


def create_compute_victim(sdf: SDF, issuer: PD):
    control = MR(sdf, "sched_bench_control", 0x1000)
    victim = PD(
        "coremark_victim",
        "coremark_victim.elf",
        priority=WORKLOAD_PRIORITY,
        stack_size=0x10000,
    )

    sdf.add_mr(control)
    issuer.add_map(MAP(control, SCHED_CONTROL_VADDR, "rw"))
    victim.add_map(MAP(control, SCHED_CONTROL_VADDR, "rw"))
    sdf.add_pd(victim)
    sdf.add_channel(Channel(
        issuer,
        victim,
        a_id=SCHED_VICTIM_CHANNEL,
        b_id=SCHED_VICTIM_CHANNEL,
    ))


def generate(sdf_path: str, output_dir: str, dtb: Optional[DeviceTree], count: int):
    serial_node, timer_node, ethernet_node = device_nodes(dtb)
    timer = create_timer(sdf, timer_node)
    serial = create_serial(sdf, serial_node)
    engine, monitor, issuer, protocons = create_container_infra(sdf, count)
    create_compute_victim(sdf, issuer)
    network, network_tx = create_network(sdf, ethernet_node, monitor)
    copiers = connect_network_clients(sdf, network, protocons)
    monitor_fs, issuer_fs = create_ramfs(sdf, monitor, issuer)

    serial.add_client(monitor)
    serial.add_client(issuer)
    timer.add_client(issuer)
    for protocon in protocons:
        serial.add_client(protocon, optional=True)
        timer.add_client(protocon, optional=True)

    for ramfs in (monitor_fs, issuer_fs):
        serial.add_client(ramfs.fs_pd)
        timer.add_client(ramfs.fs_pd)
        assert ramfs.connect()
        assert ramfs.serialise_config(output_dir)

    assert serial.connect()
    assert serial.serialise_config(output_dir)
    assert timer.connect()
    assert timer.serialise_config(output_dir)
    assert network.connect()
    for protocon in protocons:
        network.add_acl_rule(protocon, network_tx, True, True)
    assert network.serialise_config(output_dir)
    assert sdf.gensvc(output_dir)

    shutil.copyfile("ramfs_server.elf", "monitor_fs.elf")
    shutil.copyfile("ramfs_server.elf", "issuer_fs.elf")
    for index, _ in enumerate(copiers):
        shutil.copyfile("network_copy.elf", f"network_copy{index}.elf")

    Path(output_dir, sdf_path).write_text(sdf.render())


def load_boards(sddf_root: str):
    sys.path.insert(0, str(Path(sddf_root).resolve() / "tools" / "meta"))
    return getattr(importlib.import_module("board"), "BOARDS")


if __name__ == "__main__":
    base_parser = argparse.ArgumentParser(add_help=False)
    base_parser.add_argument("--sddf", required=True)
    base_args, _ = base_parser.parse_known_args()
    sddf = Sddf(base_args.sddf)
    boards = load_boards(base_args.sddf)

    parser = argparse.ArgumentParser(parents=[base_parser])
    parser.add_argument("--board", required=True, choices=[item.name for item in boards])
    parser.add_argument("--dtb")
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--vm-layout", required=True)
    parser.add_argument("--monitor-vm-layout", required=True)
    parser.add_argument("--protocon-count", type=int, default=16)
    args = parser.parse_args()

    if not 1 <= args.protocon_count <= 16:
        parser.error("--protocon-count must be in the range 1..16")

    board = next(item for item in boards if item.name == args.board)
    sdf = SDF(board.arch, board.paddr_top)
    layout_txlo = VSpace.load(args.vm_layout, "libtrustedlo_vm_layout")
    layout_monitor = VSpace.load(args.monitor_vm_layout, "monitor_vm_layout")

    dtb = None
    if board.arch != SDF.Arch.X86_64:
        if args.dtb is None:
            parser.error("--dtb is required for non-x86 boards")
        dtb = DeviceTree(Path(args.dtb).read_bytes())

    generate(args.sdf, args.output, dtb, args.protocon_count)

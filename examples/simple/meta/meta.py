# Copyright 2026, UNSW
# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

import sys
import struct
import argparse
import importlib
from pathlib import Path
from dataclasses import dataclass
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from typing import Optional

from elf import Elftools
from vspace import VSpace
from infra import CarrelsContainerInfra as Infra


SDF = SystemDescription
PD = SDF.ProtectionDomain
MR = SDF.MemoryRegion
MAP = SDF.Map
Map = SDF.Map
CapMap = SDF.CapMap
CNode = SDF.CNode
Channel = SDF.Channel
IOPORT = SDF.IoPort
IRQIOAPIC = SDF.IrqIoapic
IrqIoapic = SDF.IrqIoapic


@dataclass(frozen=True)
class X86DeviceProfile:
    hpet_paddr: int
    hpet_pin: int
    hpet_vector: int
    serial_base: int
    serial_irq_pin: int
    serial_irq_vector: int
    net_rings_paddr: int
    net_bar_paddr: int
    net_irq_pin: int
    net_irq_vector: int
    virtio_blk_bar_paddr: Optional[int]
    virtio_blk_irq_pin: Optional[int]
    virtio_blk_irq_vector: Optional[int]
    nvme_bar_paddr: int
    nvme_irq_pin: int
    nvme_irq_vector: int


X86_DEVICE_PROFILES = {
    "qemu": X86DeviceProfile(
        hpet_paddr=0xFED00000, hpet_pin=2, hpet_vector=107,
        serial_base=0x3F8, serial_irq_pin=4, serial_irq_vector=0,
        net_rings_paddr=0x7A000000, net_bar_paddr=0xFE000000,
        net_irq_pin=10, net_irq_vector=1,
        virtio_blk_bar_paddr=0xFE004000, virtio_blk_irq_pin=11,
        virtio_blk_irq_vector=2,
        nvme_bar_paddr=0xFEB90000, nvme_irq_pin=10, nvme_irq_vector=2,
    ),
    "vtx-demo": X86DeviceProfile(
        hpet_paddr=0xFED00000, hpet_pin=2, hpet_vector=107,
        serial_base=0x3F8, serial_irq_pin=4, serial_irq_vector=0,
        net_rings_paddr=0x7A000000, net_bar_paddr=0xFEBFC000,
        net_irq_pin=10, net_irq_vector=1,
        virtio_blk_bar_paddr=None, virtio_blk_irq_pin=None,
        virtio_blk_irq_vector=None,
        nvme_bar_paddr=0xFEBD4000, nvme_irq_pin=10, nvme_irq_vector=2,
    ),
}


def add_pds(sdf: SDF, *pds: PD) -> None:
    for pd in pds:
        sdf.add_pd(pd)


def resolve_device_nodes(dtb: Optional[DeviceTree]):
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

    return serial_node, blk_node, timer_node, net_node


def init_timer_system(sdf: SDF, timer_node, arch, x86_profile: X86DeviceProfile):
    timer_driver = PD("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    if arch == SystemDescription.Arch.X86_64:
        hpet_irq = IRQIOAPIC(
            ioapic_id=0,
            pin=x86_profile.hpet_pin,
            vector=x86_profile.hpet_vector,
            id=0,
            trigger=IRQIOAPIC.Trigger.EDGE,
        )
        timer_driver.add_irq(hpet_irq)
        hpet_regs = MR(sdf, "hpet_regs", 0x1000, paddr=x86_profile.hpet_paddr)
        hpet_regs_map = MAP(hpet_regs, 0x5000_0000, "rw", cached=False)
        timer_driver.add_map(hpet_regs_map)
        sdf.add_mr(hpet_regs)

    add_pds(sdf, timer_driver)
    return timer_system, timer_driver


def init_serial_system(sdf: SDF, serial_node, arch, x86_profile: X86DeviceProfile):
    serial_driver = PD("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = PD("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = PD("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver,
                                serial_virt_tx, virt_rx=serial_virt_rx)
    if arch == SystemDescription.Arch.X86_64:
        serial_port = IOPORT(x86_profile.serial_base, 8, 0)
        serial_driver.add_ioport(serial_port)
        serial_driver.add_irq(
            IRQIOAPIC(
                ioapic_id=0,
                pin=x86_profile.serial_irq_pin,
                vector=x86_profile.serial_irq_vector,
                id=1,
            )
        )

    add_pds(sdf, serial_driver, serial_virt_tx, serial_virt_rx)
    return serial_system


def init_blk_system(sdf: SDF, blk_node, arch, nvme: bool, timer_system,
                    x86_profile: X86DeviceProfile, pci_driver: PD):
    blk_driver = PD("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = PD("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    if nvme:
        assert arch == SystemDescription.Arch.X86_64
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

        # nvme_bar0 = MR(sdf, "nvme_bar0", 0x4000,
        #                paddr=x86_profile.nvme_bar_paddr)
        # sdf.add_mr(nvme_bar0)
        # blk_driver.add_map(MAP(nvme_bar0, 0x20000000, "rw", cached=False))
        # blk_driver.add_irq(
        #     IRQIOAPIC(
        #         ioapic_id=0,
        #         pin=x86_profile.nvme_irq_pin,
        #         vector=x86_profile.nvme_irq_vector,
        #         id=17,
        #         trigger=IRQIOAPIC.Trigger.LEVEL,
        #         polarity=IRQIOAPIC.Polarity.ACTIVELOW,
        #     )
        # )
        # blk_driver.add_ioport(IOPORT(0xCF8, 4, 1))
        # blk_driver.add_ioport(IOPORT(0xCFC, 4, 2))
        blk_driver.add_irq_placeholder(17)

    elif arch == SystemDescription.Arch.X86_64:
        if x86_profile.virtio_blk_bar_paddr is None:
            raise ValueError(
                "x86 device profile 'vtx-demo' supports NVMe only; "
                "use --nvme"
            )
        blk_requests_mr = MR(sdf, "virtio_requests", 65536, paddr=0x5FDF0000)
        sdf.add_mr(blk_requests_mr)
        blk_driver.add_map(MAP(blk_requests_mr, 0x20200000, "rw"))

        blk_virtio_metadata_mr = MR(
            sdf, "virtio_metadata", 65536, paddr=0x5FFF0000
        )
        sdf.add_mr(blk_virtio_metadata_mr)
        blk_driver.add_map(MAP(blk_virtio_metadata_mr, 0x20210000, "rw"))

        virtio_blk_regs = MR(
            sdf, "virtio_blk_regs", 0x4000,
            paddr=x86_profile.virtio_blk_bar_paddr,
        )
        sdf.add_mr(virtio_blk_regs)
        blk_driver.add_map(
            MAP(virtio_blk_regs, 0x60000000, "rw", cached=False)
        )
        blk_driver.add_irq(
            IRQIOAPIC(
                ioapic_id=0,
                pin=x86_profile.virtio_blk_irq_pin,
                vector=x86_profile.virtio_blk_irq_vector,
                id=17,
                trigger=IRQIOAPIC.Trigger.LEVEL,
                polarity=IRQIOAPIC.Polarity.ACTIVELOW,
            )
        )

    timer_system.add_client(blk_driver)
    add_pds(sdf, blk_driver, blk_virt)

    pci_driver.add_cap_map(CapMap(CapMap.CapType.Vspace, blk_driver, None, 4))
    pci_driver.add_cap_map(CapMap(CapMap.CapType.Cspace, blk_driver, None, 5))
    sdf.add_channel(Channel(pci_driver, blk_driver, a_id=2, b_id=10))

    return blk_system


def init_container_infra(sdf: SDF, protocon_count: int):
    container_infra = Infra(
        sdf=sdf,
        layout_txlo=layout_txlo,
        layout_monitor=layout_monitor,
        client_limit=16,
    )
    container_infra.connect_orchestrator()
    protocons = container_infra.add_clients(protocon_count)
    add_pds(sdf, container_infra.pd_engine, container_infra.pd_orchestrator)
    return container_infra, protocons


def connect_container_services(serial_system, timer_system, pd_engine, pd_orchestrator,
                               protocons) -> None:
    serial_system.add_client(pd_orchestrator)
    serial_system.add_client(pd_engine)
    for pc in protocons:
        serial_system.add_client(pc, optional=True)
        timer_system.add_client(pc, optional=True)


def fatfs_blk_queue_capacity(protocon_count: int) -> int:
    # sDDF blk_virt supports at most 1024 concurrent requests across all
    # block clients. Each FATFS instance contributes one such client.
    fatfs_client_count = protocon_count + 2
    capacity_limit = 1024 // fatfs_client_count
    return min(128, 1 << (capacity_limit.bit_length() - 1))


def init_filesystems(sdf: SDF, blk_system, pd_engine, pd_orchestrator, protocons,
                     protocon_count: int):
    blk_queue_capacity = fatfs_blk_queue_capacity(protocon_count)
    pd_fs_engine = PD("engine_fs", "engine_fs.elf", priority=96)
    pd_fs_orchestrator = PD("orchestrator_fs", "orchestrator_fs.elf", priority=96)
    engine_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_engine, pd_engine, blk=blk_system, partition=1,
        blk_queue_capacity=blk_queue_capacity,
    )
    orchestrator_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_orchestrator, pd_orchestrator, blk=blk_system, partition=0,
        blk_queue_capacity=blk_queue_capacity,
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
            blk_queue_capacity=blk_queue_capacity,
            optional=True,
        )
        for i, (protocon, fs_pd) in enumerate(zip(protocons, protocon_fs_pds))
    ]
    add_pds(sdf, pd_fs_engine, pd_fs_orchestrator, *protocon_fs_pds)
    return engine_fs, orchestrator_fs, protocon_filesystems, protocon_fs_pds


def init_net_system(sdf: SDF, net_node, arch, pd_engine, protocons,
                    protocon_count: int, x86_profile: X86DeviceProfile, pci_driver: PD):
    eth_driver = PD("eth_driver", "eth_driver.elf",
                    priority=101, budget=100, period=400)

    if arch == SystemDescription.Arch.X86_64:
        hw_net_rings = MR(
            sdf, "hw_net_rings", 65536, paddr=x86_profile.net_rings_paddr
        )
        sdf.add_mr(hw_net_rings)
        eth_driver.add_map(MAP(hw_net_rings, 0x7000_0000, "rw", cached=False))

        # virtio_net_regs = MR(
        #     sdf, "virtio_net_regs", 0x4000, paddr=x86_profile.net_bar_paddr
        # )
        # sdf.add_mr(virtio_net_regs)
        # eth_driver.add_map(MAP(virtio_net_regs, 0x6000_0000, "rw", cached=False))

        # eth_driver.add_irq(
        #     IRQIOAPIC(
        #         ioapic_id=0,
        #         pin=x86_profile.net_irq_pin,
        #         vector=x86_profile.net_irq_vector,
        #         id=16,
        #         trigger=IRQIOAPIC.Trigger.LEVEL,
        #         polarity=IRQIOAPIC.Polarity.ACTIVELOW,
        #     )
        # )
        eth_driver.add_irq_placeholder(16)

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

    add_pds(sdf, eth_driver, net_virt_rx, net_virt_tx, net_vswitch, *net_copiers)

    pci_driver.add_cap_map(CapMap(CapMap.CapType.Vspace, eth_driver, None, 2))
    pci_driver.add_cap_map(CapMap(CapMap.CapType.Cspace, eth_driver, None, 3))
    sdf.add_channel(Channel(pci_driver, eth_driver, a_id=1, b_id=10))

    return net_system, net_virt_tx, net_copiers


def connect_and_serialise(output_dir: str, engine_fs, orchestrator_fs,
                          protocon_filesystems, serial_system, timer_system,
                          blk_system, net_system, protocons, net_virt_tx) -> None:
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


def update_generated_elfs(protocon_fs_pds, net_copiers) -> None:
    # Each client gets an independent copier ELF and its matching config.
    for i, net_copier in enumerate(net_copiers):
        elf.copy_elf("network_copy", f"network_copy{i}")
        elf.update_elf_section(
            f"network_copy{i}.elf",
            "net_copy_config",
            f"net_copy_{net_copier.name}",
        )

    elf.copy_elf("fat", "orchestrator_fs", None)
    elf.copy_elf("fat", "engine_fs", None)
    for fs_pd in protocon_fs_pds:
        elf.copy_elf("fat", fs_pd.name, None)

    for name in ("orchestrator_fs", "engine_fs"):
        elf.update_elf_section(name + ".elf", "blk_client_config", "blk_client_" + name)
        elf.update_elf_section(name + ".elf", "fs_server_config", "fs_server_" + name)
    for fs_pd in protocon_fs_pds:
        elf.update_elf_section(
            f"{fs_pd.name}.elf", "blk_client_config", f"blk_client_{fs_pd.name}"
        )
        elf.update_elf_section(
            f"{fs_pd.name}.elf", "fs_server_config", f"fs_server_{fs_pd.name}"
        )

class AcpiTablesConfig:
    def __init__(
        self,
        max_total_size: int,
    ):
        self.max_total_size = max_total_size
        self.patched_tables_end = 0
        self.alignment = 0x1000
        self.max_num_acpi_tables = 20 # This needs to be synced with MAX_NUM_ACPI_TABLES in acpi.h
        self.num_tables = 0
        self.acpi_table_bytes = bytearray()
        self.acpi_table_pointers = [0] * self.max_num_acpi_tables

    # TODO: add the checks
    def add_acpi_table(self, acpi_file):
        acpi_file = "/Users/terrybai/tmp/acpi_vb105/vb105_acpi/" + acpi_file + ".dat"
        print(acpi_file)
        assert os.path.isfile(acpi_file)
        with open(acpi_file, "rb") as data_file:
            byte_list = list(data_file.read())

            if len(byte_list) + len(self.acpi_table_bytes) < self.max_total_size:
                self.acpi_table_pointers[self.num_tables] = len(self.acpi_table_bytes)
                self.acpi_table_bytes.extend(byte_list)
                self.patched_tables_end = len(self.acpi_table_bytes)
                self.num_tables += 1

        trailing_len = len(self.acpi_table_bytes) % self.alignment
        if trailing_len != 0:
            padding_len = self.alignment - trailing_len
            if padding_len + len(self.acpi_table_bytes) < self.max_total_size:
                self.acpi_table_bytes.extend(b"\x00" * padding_len)

    def tables_serialise(self):
        pack_str = "<" + "B" * len(self.acpi_table_bytes)

        return struct.pack(
            pack_str,
            *self.acpi_table_bytes
        )

    def summary_serialise(self):
        pack_str = "<" + "Q" * self.max_num_acpi_tables + "QQII"

        return struct.pack(
            pack_str,
            *self.acpi_table_pointers,
            self.patched_tables_end,
            self.max_total_size,
            self.alignment,
            self.num_tables,
        )

def init_acpi_pci():
    acpi_driver = PD("acpi_driver", "acpi_driver.elf", priority=253, stack_size=0x5000)
    pci_driver = PD("pci_driver", "pci_driver.elf", priority=252)

    acpi_bootinfo_post_capdl_untypeds = MR(sdf, "bootinfo_post_capdl_untypeds", 0x1000, prefill_bootinfo="post_capdl_untypeds")
    sdf.add_mr(acpi_bootinfo_post_capdl_untypeds)
    acpi_driver.add_map(Map(acpi_bootinfo_post_capdl_untypeds, 0x2000000, "r", setvar_vaddr="bootinfo_post_capdl_untypeds"))

    acpi_bootinfo_rsdp = MR(sdf, "bootinfo_rsdp", 0x1000, prefill_bootinfo="x86_acpi_rsdp")
    sdf.add_mr(acpi_bootinfo_rsdp)
    acpi_driver.add_map(Map(acpi_bootinfo_rsdp, 0x2001000, "r", setvar_vaddr="bootinfo_rsdp"))

    acpi_tables_config = AcpiTablesConfig(0x500000)

    cnode_remaining_untypeds = CNode("remaining_untypeds", True, 9)
    sdf.add_cnode(cnode_remaining_untypeds)
    acpi_driver.add_cap_map(CapMap(CapMap.CapType.Cnode, None, cnode_remaining_untypeds, 1))
    acpi_driver.add_cap_map(CapMap(CapMap.CapType.Vspace, pci_driver, None, 2))

    cnode_pci_resources = CNode("pci_resources", False, 9)
    sdf.add_cnode(cnode_pci_resources)
    acpi_driver.add_cap_map(CapMap(CapMap.CapType.Cnode, None, cnode_pci_resources, 3))
    pci_driver.add_cap_map(CapMap(CapMap.CapType.Cnode, None, cnode_pci_resources, 1))

    mr_aml_object_pool = MR(sdf, "aml_object_pool", 0x100000)
    sdf.add_mr(mr_aml_object_pool)
    acpi_driver.add_map(Map(mr_aml_object_pool, 0x30000000, "rw"))

    mr_aml_state_stack = MR(sdf, "aml_state_stack", 0x10000)
    sdf.add_mr(mr_aml_state_stack)
    acpi_driver.add_map(Map(mr_aml_state_stack, 0x50000000, "rw"))

    mr_acpi_tables_copy = MR(sdf, "acpi_tables_copy", 0x50000)
    sdf.add_mr(mr_acpi_tables_copy)
    acpi_driver.add_map(Map(mr_acpi_tables_copy, 0x40000000, "rw"))

    mr_pci_resources = MR(sdf, "pci_resources", 0x40000)
    sdf.add_mr(mr_pci_resources)
    acpi_driver.add_map(Map(mr_pci_resources, 0x60000000, "rw", cached=False))
    pci_driver.add_map(Map(mr_pci_resources, 0x60000000, "rw", cached=False))

    sdf.add_channel(Channel(acpi_driver, pci_driver, a_id=0, b_id=0))
    sdf.add_pd(acpi_driver)
    sdf.add_pd(pci_driver)

    return acpi_driver, pci_driver, acpi_tables_config


def generate(
    sdf_path: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
    nvme: bool,
    protocon_count: int,
    x86_device_profile: str,
):
    x86_profile = X86_DEVICE_PROFILES[x86_device_profile]

    acpi_driver, pci_driver, acpi_tables_config = init_acpi_pci()

    serial_node, blk_node, timer_node, net_node = resolve_device_nodes(dtb)
    timer_system, _ = init_timer_system(sdf, timer_node, board.arch, x86_profile)
    serial_system = init_serial_system(sdf, serial_node, board.arch, x86_profile)
    blk_system = init_blk_system(
        sdf, blk_node, board.arch, nvme, timer_system, x86_profile, pci_driver
    )
    container_infra, protocons = init_container_infra(sdf, protocon_count)
    pd_engine = container_infra.pd_engine
    pd_orchestrator = container_infra.pd_orchestrator
    connect_container_services(
        serial_system, timer_system, pd_engine, pd_orchestrator, protocons
    )
    engine_fs, orchestrator_fs, protocon_filesystems, protocon_fs_pds = init_filesystems(
        sdf, blk_system, pd_engine, pd_orchestrator, protocons, protocon_count
    )
    net_system, net_virt_tx, net_copiers = init_net_system(
        sdf, net_node, board.arch, pd_engine, protocons, protocon_count, x86_profile, pci_driver
    )
    connect_and_serialise(
        output_dir, engine_fs, orchestrator_fs, protocon_filesystems,
        serial_system, timer_system, blk_system, net_system, protocons, net_virt_tx,
    )
    # Generate all LionsOS service descriptors after optional services exist.
    assert sdf.gensvc(output_dir)
    update_generated_elfs(protocon_fs_pds, net_copiers)

    with open(f"{output_dir}/acpi_tables_summary.data", "wb+") as f:
        f.write(acpi_tables_config.summary_serialise())
    elf.update_elf_section("acpi_driver.elf", "acpi_tables_summary", "acpi_tables_summary")

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
    parser.add_argument(
        "--x86-device-profile",
        choices=X86_DEVICE_PROFILES.keys(),
        default="qemu",
        help="fixed x86 PCI resource layout (default: qemu)",
    )
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

    generate(
        args.sdf,
        args.output,
        dtb,
        args.nvme,
        args.protocon_count,
        args.x86_device_profile,
    )

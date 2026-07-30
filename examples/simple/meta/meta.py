# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os
import sys
import argparse
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

sys.path.append(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tools/meta")
)
from board import BOARDS

from elf import elftools
from vspace import VSpace
from monitor import CarrelsContainerEngine

assert (
    version("sdfgen").split(".")[1] == "29" or version("sdfgen").split(".")[1] == "33"
), "Unexpected sdfgen version"

SDF = SystemDescription
PD = SDF.ProtectionDomain


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = PD("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = PD("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = PD(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99
    )
    serial_virt_rx = PD(
        "serial_virt_rx", "serial_virt_rx.elf", priority=99
    )
    serial_system = Sddf.Serial(
        sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx
    )

    blk_driver = PD("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = PD(
        "blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000
    )
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    pd_orchestrator = PD(
        "orchestrator", "orchestrator.elf", priority=60, stack_size=0x10000
    )
    # noted that the 'is_monitor' feature is enabled for container monitor, which needs sdfgen support
    pd_monitor = PD(
        "container_monitor",
        "monitor.elf",
        priority=64,
        stack_size=0x10000,
        is_monitor=True,
    )

    m = CarrelsContainerEngine(
        sdf=sdf,
        engine=pd_monitor,
        orchestrator=pd_orchestrator,
        layout_txlo=layout_txlo,
        layout_monitor=layout_monitor,
        cid_limit=6
    )
    m.connect_orchestrator()

    serial_system.add_client(pd_orchestrator)
    serial_system.add_client(pd_monitor)

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    protocon0 = PD("protocon0", priority=53)
    protocon1 = PD("protocon1", priority=53)
    protocon2 = PD("protocon2", priority=53)
    protocon3 = PD("protocon3", priority=53)
    protocon4 = PD("protocon4", priority=53)
    protocon5 = PD("protocon5", priority=53)

    cid0 = m.add_client(protocon0)
    cid1 = m.add_client(protocon1)
    cid2 = m.add_client(protocon2)
    cid3 = m.add_client(protocon3)
    cid4 = m.add_client(protocon4)
    cid5 = m.add_client(protocon5)

    pd_fs_orchestrator = PD(
        "orchestrator_fs", "orchestrator_fs.elf", priority=96
    )
    pd_fs_monitor = PD("monitor_fs", "monitor_fs.elf", priority=96)
    pd_fs_sp0 = PD("protocon0_fs", "protocon0_fs.elf", priority=96)
    pd_fs_sp1 = PD("protocon1_fs", "protocon1_fs.elf", priority=96)

    orchestrator_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_orchestrator, pd_orchestrator, blk=blk_system, partition=0
    )
    monitor_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_monitor, pd_monitor, blk=blk_system, partition=1
    )
    protocon0_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_sp0, protocon0, blk=blk_system, partition=2
    )
    protocon1_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_sp1, protocon1, blk=blk_system, partition=3
    )

    serial_system.add_client(protocon0, optional=True)
    serial_system.add_client(protocon1, optional=True)
    serial_system.add_client(protocon2, optional=True)
    serial_system.add_client(protocon3, optional=True)

    timer_system.add_client(protocon0, optional=True)
    timer_system.add_client(protocon1, optional=True)
    timer_system.add_client(protocon2, optional=True)
    timer_system.add_client(protocon3, optional=True)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        pd_orchestrator,
        pd_fs_orchestrator,
        timer_driver,
        blk_driver,
        blk_virt,
        pd_monitor,
        pd_fs_monitor,
        pd_fs_sp0,
        pd_fs_sp1,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert protocon0_fs.connect(optional=True)
    assert protocon0_fs.serialise_config(output_dir)
    assert protocon1_fs.connect(optional=True)
    assert protocon1_fs.serialise_config(output_dir)
    assert orchestrator_fs.connect()
    assert orchestrator_fs.serialise_config(output_dir)
    assert monitor_fs.connect()
    assert monitor_fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    elf.copy_elf("fat", "orchestrator_fs", None)
    elf.copy_elf("fat", "monitor_fs", None)
    elf.copy_elf("fat", "protocon0_fs", None)
    elf.copy_elf("fat", "protocon1_fs", None)

    elf.update_elf_section("orchestrator_fs.elf", "blk_client_config", "blk_client_orchestrator_fs")
    elf.update_elf_section("orchestrator_fs.elf", "fs_server_config", "fs_server_orchestrator_fs")

    elf.update_elf_section("monitor_fs.elf", "blk_client_config", "blk_client_monitor_fs")
    elf.update_elf_section("monitor_fs.elf", "fs_server_config", "fs_server_monitor_fs")

    elf.update_elf_section("protocon0_fs.elf", "blk_client_config", "blk_client_protocon0_fs")
    elf.update_elf_section("protocon0_fs.elf", "fs_server_config", "fs_server_protocon0_fs")

    elf.update_elf_section("protocon1_fs.elf", "blk_client_config", "blk_client_protocon1_fs")
    elf.update_elf_section("protocon1_fs.elf", "fs_server_config", "fs_server_protocon1_fs")

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())

    assert sdf.generate_svc(output_dir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument(
        "--vm-layout",
        required=True,
        help="path to libtrustedlo config/vm_layout.py",
    )
    parser.add_argument(
        "--monitor-vm-layout",
        required=True,
        help="path to monitor config/vm_layout.py",
    )

    args = parser.parse_args()

    layout_txlo = VSpace.load(args.vm_layout, "libtrustedlo_vm_layout")
    layout_monitor = VSpace.load(args.monitor_vm_layout, "monitor_vm_layout")

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SDF(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    elf = elftools(args.objcopy)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)

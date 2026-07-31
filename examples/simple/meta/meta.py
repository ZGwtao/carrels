# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import sys
import argparse
import importlib
from pathlib import Path
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

from elf import Elftools
from vspace import VSpace
from infra import CarrelsContainerInfra as Infra

assert version("sdfgen").split(".")[1] == "33", "Unexpected sdfgen version"

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
    serial_virt_tx = PD("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = PD("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver,
                                serial_virt_tx, virt_rx=serial_virt_rx)

    blk_driver = PD("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = PD("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    if board.name == "maaxboard":
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
    protocons = container_infra.add_clients(16)
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

    for pc in container_infra.protocons:
        serial_system.add_client(pc, optional=True)
        timer_system.add_client(pc, optional=True)

    pd_fs_engine = PD("engine_fs", "engine_fs.elf", priority=96)
    pd_fs_orchestrator = PD("orchestrator_fs", "orchestrator_fs.elf", priority=96)
    engine_fs = LionsOs.FileSystem.Fat(sdf, pd_fs_engine, pd_engine, blk=blk_system, partition=1)
    orchestrator_fs = LionsOs.FileSystem.Fat(sdf, pd_fs_orchestrator, pd_orchestrator, blk=blk_system, partition=0)

    pd_fs_protocon0 = PD("protocon0_fs", "protocon0_fs.elf", priority=96)
    pd_fs_protocon1 = PD("protocon1_fs", "protocon1_fs.elf", priority=96)
    protocon0_fs = LionsOs.FileSystem.Fat(sdf, pd_fs_protocon0, protocons[0], blk=blk_system, partition=2)
    protocon1_fs = LionsOs.FileSystem.Fat(sdf, pd_fs_protocon1, protocons[1], blk=blk_system, partition=3)

    pds = [
        pd_fs_engine,
        pd_fs_orchestrator,
        pd_fs_protocon0,
        pd_fs_protocon1,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    # Net subsystem
    net_node = dtb.node(board.ethernet)
    assert net_node is not None

    eth_driver = PD("eth_driver", "eth_driver.elf",
                    priority=101, budget=100, period=400)
    net_virt_tx = PD("net_virt_tx", "network_virt_tx.elf", priority=100, budget=20000)
    net_virt_rx = PD("net_virt_rx", "network_virt_rx.elf", priority=99)
    vswitch = PD("net_vswitch", "network_vswitch.elf", priority=98)
    net_system = Sddf.Net(sdf, net_node, eth_driver, net_virt_tx, net_virt_rx, vswitch=vswitch)
    client0_net_copier = PD(
        "client0_net_copier", "network_copy0.elf", priority=97, budget=20000)

    net_system.add_client_with_copier(pd_engine, client0_net_copier, vswitch=True)

    pds = [
        eth_driver,
        net_virt_rx,
        net_virt_tx,
        client0_net_copier,
        vswitch,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert protocon0_fs.connect(optional=True)
    assert protocon0_fs.serialise_config(output_dir)
    assert protocon1_fs.connect(optional=True)
    assert protocon1_fs.serialise_config(output_dir)
    assert orchestrator_fs.connect()
    assert orchestrator_fs.serialise_config(output_dir)
    assert engine_fs.connect()
    assert engine_fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    assert net_system.connect()
    assert net_system.serialise_config(output_dir)

    # generate all LionsOS services descriptors for engines.
    assert sdf.generate_svc(output_dir)

    elf.copy_elf("fat", "orchestrator_fs", None)
    elf.copy_elf("fat", "engine_fs", None)
    elf.copy_elf("fat", "protocon0_fs", None)
    elf.copy_elf("fat", "protocon1_fs", None)

    elf.update_elf_section("orchestrator_fs.elf", "blk_client_config", "blk_client_orchestrator_fs")
    elf.update_elf_section("orchestrator_fs.elf", "fs_server_config", "fs_server_orchestrator_fs")

    elf.update_elf_section("engine_fs.elf", "blk_client_config", "blk_client_engine_fs")
    elf.update_elf_section("engine_fs.elf", "fs_server_config", "fs_server_engine_fs")

    elf.update_elf_section("protocon0_fs.elf", "blk_client_config", "blk_client_protocon0_fs")
    elf.update_elf_section("protocon0_fs.elf", "fs_server_config", "fs_server_protocon0_fs")

    elf.update_elf_section("protocon1_fs.elf", "blk_client_config", "blk_client_protocon1_fs")
    elf.update_elf_section("protocon1_fs.elf", "fs_server_config", "fs_server_protocon1_fs")

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
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--vm-layout", required=True,
                        help="path to libtrustedlo config/vm_layout.py")
    parser.add_argument("--monitor-vm-layout", required=True,
                        help="path to monitor config/vm_layout.py")

    args = parser.parse_args()

    layout_txlo = VSpace.load(args.vm_layout, "libtrustedlo_vm_layout")
    layout_monitor = VSpace.load(args.monitor_vm_layout, "monitor_vm_layout")

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SDF(board.arch, board.paddr_top)

    elf = Elftools(args.objcopy)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)

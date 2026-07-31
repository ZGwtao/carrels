# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription as SDF
from vspace import VSpace

PD = SDF.ProtectionDomain
MR = SDF.MemoryRegion
MAP = SDF.Map
CHN = SDF.Channel

class CarrelsContainerEngine:
    def __init__(
        self,
        sdf: SDF,
        engine: PD,
        orchestrator: PD,
        layout_txlo: VSpace,
        layout_monitor: VSpace,
        cid_limit: int
    ):
        if cid_limit <= 0 or cid_limit > 16:
            raise ValueError("Invalid cid_limit given")

        self.sdf = sdf
        self.engine = engine
        self.orchestrator = orchestrator
        self.layout_txlo = layout_txlo
        self.layout_monitor = layout_monitor
        self.cid_limit = cid_limit
        self._free_cids: set[int] = set(range(cid_limit))
        self._client_cids: dict[PD, int] = {}

    def allocate_cid(self, client: PD) -> int:
        if not self._free_cids:
            raise RuntimeError(f"No CID available; limit is {self.cid_limit}")
        cid = min(self._free_cids)
        self._free_cids.remove(cid)
        self._client_cids[client] = cid
        return cid

    def create_mr(self, pd_name: str, mr_name: str, size: int) -> MR:
        prefix = f"{self.engine.name}/{pd_name}/"
        return MR(self.sdf, name=prefix + mr_name.lower(), size=size)

    def create_txlo_mr(self, pd_name: str, mr_name: str) -> MR:
        prefix = f"{self.engine.name}/{pd_name}/"
        mr_size = self.layout_txlo.size(mr_name)
        return MR(self.sdf, name=prefix + mr_name.lower(), size=mr_size)

    def create_map_exec(self, mr: MR, vaddr: int) -> MAP:
        return MAP(mr, vaddr, perms="rwx", cached="true")

    def create_map_uncached_data(self, mr: MR, vaddr: int) -> MAP:
        return MAP(mr, vaddr, perms="rw", cached="false")

    def create_map_cached_data(self, mr: MR, vaddr: int) -> MAP:
        return MAP(mr, vaddr, perms="rw", cached="true")

    def create_map_io(self, mr: MR, vaddr: int) -> MAP:
        return self.create_map_uncached_data(mr, vaddr)

    def create_map_monitor_mr_with_idx(self, mr: MR, mr_mon_name: str, idx: int) -> MAP:
        return self.create_map_cached_data(mr, self.layout_monitor.region_base(mr_mon_name, idx))

    def create_map_txlo_data_mr(self, mr: MR, mr_name: str) -> MAP:
        return self.create_map_cached_data(mr, self.layout_txlo.base(mr_name))

    def create_map_txlo_exec_mr(self, mr: MR, mr_name: str) -> MAP:
        return self.create_map_exec(mr, self.layout_txlo.base(mr_name))

    def setup_mr_images(self, pc: PD, cid: int):
        container_elf = self.create_txlo_mr(pc.name, "CONTAINER_IMAGE")
        trampoline_elf = self.create_txlo_mr(pc.name, "TRAMPOLINE_IMAGE")
        self.sdf.add_mr(container_elf)
        self.sdf.add_mr(trampoline_elf)
        self.engine.add_map(self.create_map_monitor_mr_with_idx(container_elf, "CONTAINER_IMAGE", cid))
        self.engine.add_map(self.create_map_monitor_mr_with_idx(trampoline_elf, "TRAMPOLINE_IMAGE", cid))
        pc.add_map(self.create_map_txlo_data_mr(container_elf, "CONTAINER_IMAGE"))
        pc.add_map(self.create_map_txlo_data_mr(trampoline_elf, "TRAMPOLINE_IMAGE"))

    def setup_mr_trampoline(self, pc: PD):
        trampoline_args = self.create_txlo_mr(pc.name, "TRAMPOLINE_ARGS")
        trampoline_exec = self.create_txlo_mr(pc.name, "TRAMPOLINE_PROGRAM")
        trampoline_stack = self.create_txlo_mr(pc.name, "TRAMPOLINE_STACK")

        self.sdf.add_mr(trampoline_args)
        self.sdf.add_mr(trampoline_exec)
        self.sdf.add_mr(trampoline_stack)

        pc.add_map(self.create_map_txlo_data_mr(trampoline_stack, "TRAMPOLINE_STACK"))
        pc.add_map(self.create_map_txlo_exec_mr(trampoline_exec, "TRAMPOLINE_PROGRAM"))
        pc.add_map(self.create_map_txlo_data_mr(trampoline_args, "TRAMPOLINE_ARGS"))

    def setup_mr_txlo(self, pc: PD, cid: int):
        txlo_xrt_ceiling = self.create_txlo_mr(pc.name, "LOADER_METADATA")
        txlo_xrt_request = self.create_txlo_mr(pc.name, "TXLO_XRT_REQ")
        txlo_context = self.create_txlo_mr(pc.name, "LOADER_CONTEXT")
        txlo_exec = self.create_txlo_mr(pc.name, "LOADER_PROGRAM")
        self.sdf.add_mr(txlo_xrt_ceiling)
        self.sdf.add_mr(txlo_xrt_request)
        self.sdf.add_mr(txlo_context)
        self.sdf.add_mr(txlo_exec)
        self.engine.add_map(self.create_map_monitor_mr_with_idx(txlo_xrt_ceiling, "LOADER_METADATA", cid))
        self.engine.add_map(self.create_map_monitor_mr_with_idx(txlo_xrt_request, "TXLO_XRT_REQ", cid))
        self.engine.add_map(self.create_map_monitor_mr_with_idx(txlo_context, "LOADER_CONTEXT", cid))
        self.engine.add_map(self.create_map_monitor_mr_with_idx(txlo_exec, "LOADER_PROGRAM", cid))
        pc.add_map(self.create_map_txlo_data_mr(txlo_xrt_ceiling, "LOADER_METADATA"))
        pc.add_map(self.create_map_txlo_data_mr(txlo_xrt_request, "TXLO_XRT_REQ"))
        pc.add_map(self.create_map_txlo_data_mr(txlo_context, "LOADER_CONTEXT"))
        pc.add_map(self.create_map_txlo_exec_mr(txlo_exec, "LOADER_PROGRAM"))

    def setup_mr_application(self, pc: PD):
        container_stack = self.create_txlo_mr(pc.name, "CONTAINER_STACK")
        container_exec = self.create_txlo_mr(pc.name, "CONTAINER_PROGRAM")
        self.sdf.add_mr(container_stack)
        self.sdf.add_mr(container_exec)

        pc.add_map(self.create_map_txlo_data_mr(container_stack, "CONTAINER_STACK"))
        pc.add_map(self.create_map_txlo_exec_mr(container_exec, "CONTAINER_PROGRAM"))

    def setup_mr_io_quque(self, pc: PD, cid: int):
        client_monitor_rx_free = self.create_mr(pc.name, "rx/free", 0x3000)
        client_monitor_tx_free = self.create_mr(pc.name, "tx/free", 0x3000)
        client_monitor_rx_active = self.create_mr(pc.name, "rx/active", 0x3000)
        client_monitor_tx_active = self.create_mr(pc.name, "tx/active", 0x3000)
        client_monitor_rx_data = self.create_mr(pc.name, "rx/data", 0x100000)
        client_monitor_tx_data = self.create_mr(pc.name, "tx/data", 0x100000)

        self.sdf.add_mr(client_monitor_rx_free)
        self.sdf.add_mr(client_monitor_rx_active)
        self.sdf.add_mr(client_monitor_rx_data)
        self.sdf.add_mr(client_monitor_tx_free)
        self.sdf.add_mr(client_monitor_tx_active)
        self.sdf.add_mr(client_monitor_tx_data)

        pc.add_map(MAP(client_monitor_rx_free, 0x04800000, perms="rw", cached="false"))
        pc.add_map(MAP(client_monitor_tx_free, 0x04803000, perms="rw", cached="false"))
        pc.add_map(MAP(client_monitor_rx_active, 0x04806000, perms="rw", cached="false"))
        pc.add_map(MAP(client_monitor_tx_active, 0x04809000, perms="rw", cached="false"))
        pc.add_map(MAP(client_monitor_rx_data, 0x0480C000, perms="rw", cached="false"))
        pc.add_map(MAP(client_monitor_tx_data, 0x0490C000, perms="rw", cached="false"))

        monitor_queue_base = 0x80000000 + cid * 0x400000

        self.engine.add_map(self.create_map_io(client_monitor_tx_free, monitor_queue_base + 0x000000))
        self.engine.add_map(self.create_map_io(client_monitor_tx_active, monitor_queue_base + 0x006000))
        self.engine.add_map(self.create_map_io(client_monitor_tx_data, monitor_queue_base + 0x00C000))
        self.engine.add_map(self.create_map_io(client_monitor_rx_free, monitor_queue_base + 0x003000))
        self.engine.add_map(self.create_map_io(client_monitor_rx_active, monitor_queue_base + 0x009000))
        self.engine.add_map(self.create_map_io(client_monitor_rx_data, monitor_queue_base + 0x10C000))

    def add_client(self, pc: PD) -> int:
        cid = self.allocate_cid(pc)
        self.engine.add_child_pd(pc, child_id=cid)

        self.setup_mr_images(pc, cid)
        self.setup_mr_txlo(pc, cid)
        self.setup_mr_trampoline(pc)
        self.setup_mr_application(pc)
        self.setup_mr_io_quque(pc, cid)

        self.sdf.add_channel(CHN(a=self.engine, b=pc, a_id=(24 + cid), b_id=15, pp_b=True))
        self.sdf.add_channel(CHN(a=self.engine, b=pc, a_id=(40 + cid), b_id=16))

        """
        the things above are for setting up the regions for a normal conatiner
        the things below are for unikernels specifically.
        """
        uk_boot_stack = self.create_mr(pc.name, "uk_boot_stack", (0x1000 * (1 << 4)))
        uk_boot_heap = self.create_mr(pc.name, "uk_boot_heap", (0x1000 * (1 << 10)))
        self.sdf.add_mr(uk_boot_stack)
        self.sdf.add_mr(uk_boot_heap)
        pc.add_map(MAP(uk_boot_stack, 0xFF008000, perms="rw", cached="true"))
        pc.add_map(MAP(uk_boot_heap, 0xFF018000, perms="rw", cached="true"))

        return cid

    def connect_orchestrator(self):
        prefix = f"{self.engine.name}/{self.orchestrator.name}/"

        image_txlo = MR(self.sdf, prefix + "txlo", 0x800000)
        image_trampoline = MR(self.sdf, prefix + "trampoline", 0x800000)
        image_application = MR(self.sdf, prefix + "application", 0x800000)

        self.sdf.add_mr(image_txlo)
        self.sdf.add_mr(image_trampoline)
        self.sdf.add_mr(image_application)

        self.engine.add_map(self.create_map_cached_data(image_txlo, 0x6000000))
        self.engine.add_map(self.create_map_cached_data(image_trampoline, 0x6800000))
        self.engine.add_map(self.create_map_cached_data(image_application, 0x7000000))

        self.orchestrator.add_map(self.create_map_cached_data(image_txlo, 0x4000000))
        self.orchestrator.add_map(self.create_map_cached_data(image_trampoline, 0x6000000))
        self.orchestrator.add_map(self.create_map_cached_data(image_application, 0xB000000))

        self.sdf.add_channel(CHN(a=self.engine, b=self.orchestrator, a_id=23, b_id=1, pp_b=True))
        self.sdf.add_channel(CHN(a=self.engine, b=self.orchestrator, a_id=15, b_id=30))

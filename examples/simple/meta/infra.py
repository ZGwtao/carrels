# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from engine import CarrelsContainerEngine as Engine
from sdfgen import SystemDescription as SDF
from vspace import VSpace


PD = SDF.ProtectionDomain


class CarrelsContainerInfra:
    def __init__(
        self,
        sdf: SDF,
        layout_txlo: VSpace,
        layout_monitor: VSpace,
        client_limit: int = 16,
    ):
        if client_limit <= 0 or client_limit > 16:
            raise ValueError("Invalid client_limit given")

        self.sdf = sdf
        self.layout_txlo = layout_txlo
        self.layout_monitor = layout_monitor
        self.client_limit = client_limit

        self.pd_orchestrator = PD(
            "orchestrator",
            "orchestrator.elf",
            priority=60,
            stack_size=0x10000,
        )

        self.pd_engine = PD(
            "container_monitor",
            "monitor.elf",
            priority=64,
            stack_size=0x10000,
            is_monitor=True,
        )

        self.engine = Engine(
            sdf=self.sdf,
            engine=self.pd_engine,
            orchestrator=self.pd_orchestrator,
            layout_txlo=self.layout_txlo,
            layout_monitor=self.layout_monitor,
            cid_limit=self.client_limit,
        )

        self.protocons: list[PD] = []

    def connect_orchestrator(self) -> None:
        self.engine.connect_orchestrator()

    def add_client(
        self,
        name: str | None = None,
        priority: int = 53,
    ) -> PD:
        index = len(self.protocons)

        if index >= self.client_limit:
            raise RuntimeError(
                f"Cannot add more than {self.client_limit} clients"
            )

        if name is None:
            name = f"protocon{index}"

        client = PD(
            name,
            priority=priority,
        )

        self.engine.add_client(client)
        self.protocons.append(client)

        return client

    def add_clients(
        self,
        count: int,
        priority: int = 53,
    ) -> list[PD]:

        if count <= 0 or count > 16:
            raise ValueError("Invalid client num given for container infra.")
        cur_num = self.client_limit - len(self.protocons)
        if count > cur_num:
            raise ValueError(f"Cannot add {count} clients; only {cur_num} slots remain")

        clients = []
        for _ in range(count):
            clients.append(self.add_client(priority=priority))
        return clients

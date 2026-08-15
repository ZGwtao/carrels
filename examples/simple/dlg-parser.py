#!/usr/bin/env python3

# Copyright 2026, UNSW
# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

"""Inspect a delegation bundle emitted by the Microkit tool."""

from argparse import ArgumentParser
from pathlib import Path
import struct


DLG_MAGIC = b"CapDelg\0"

RESOURCE_CHANNEL_NOTIFY = 1
RESOURCE_CHANNEL_PPC = 2
RESOURCE_MEMORY_REGION = 3
RESOURCE_IOPORT = 4

RESOURCE_NAMES = {
    RESOURCE_CHANNEL_NOTIFY: "channel-notify",
    RESOURCE_CHANNEL_PPC: "channel-ppc",
    RESOURCE_MEMORY_REGION: "memory-region",
    RESOURCE_IOPORT: "ioport",
}


def parse_header(data):
    magic, delegator_cnt, _ = struct.unpack_from("<8sII", data)
    return delegator_cnt, struct.calcsize("<8sII")


def parse_delegator(data, off):
    record_size, resource_cnt, delegation_cap, pd_id = struct.unpack_from("<HHIQ", data, off)
    off += struct.calcsize("<HHIQ")

    resources = []
    for _ in range(resource_cnt):
        resource, off = parse_resource(data, off)
        resources.append(resource)

    return {
        "record_size": record_size,
        "resource_cnt": resource_cnt,
        "delegation_cap": delegation_cap,
        "pd_id": pd_id,
        "resources": resources,
    }, off


def parse_resource(data, off):
    kind, flags, slot, cap_count, arg0, arg1 = struct.unpack_from("<BBHHQQ", data, off)
    off += struct.calcsize("<BBHHQQ")

    return {
        "kind": kind,
        "flags": flags,
        "slot": slot,
        "cap_count": cap_count,
        "arg0": arg0,
        "arg1": arg1,
    }, off


def parse_delegation_bundle(bundle_path: Path):
    data = bundle_path.read_bytes()
    delegator_cnt, off = parse_header(data)
    delegators = []

    for _ in range(delegator_cnt):
        delegator, off = parse_delegator(data, off)
        delegators.append(delegator)

    return data, delegators


def print_resource(resource):
    kind = resource["kind"]
    name = RESOURCE_NAMES.get(kind, f"unknown-{kind}")
    slot = resource["slot"]
    cap_count = resource["cap_count"]
    arg0 = resource["arg0"]
    arg1 = resource["arg1"]
    caps = str(slot) if cap_count == 1 else f"{slot}..{slot + cap_count - 1}"

    if kind == RESOURCE_MEMORY_REGION:
        rights = resource["flags"] & 0x7
        cached = bool(resource["flags"] & (1 << 3))
        print(f"    {name}: caps={caps}, vaddr={arg0:#x}, page_size={arg1:#x}, rights={rights:#x}, cached={cached}")
    elif kind == RESOURCE_IOPORT:
        print(f"    {name}: cap={slot}, addr={arg0:#x}, size={arg1:#x}")
    else:
        print(f"    {name}: cap={slot}")


def print_bundle(bundle_path: Path, data, delegators):
    print(f"Delegation bundle: {bundle_path}")
    print(f"Size: {len(data)} bytes")
    print(f"Delegators: {len(delegators)}")

    for i, delegator in enumerate(delegators):
        print()
        print(f"[{i}] pd_id={delegator['pd_id']}, delegation_cap={delegator['delegation_cap']}, resources={delegator['resource_cnt']}, record_size={delegator['record_size']}")
        for resource in delegator["resources"]: print_resource(resource)


def main():
    parser = ArgumentParser()
    parser.add_argument("bundle", type=Path, help="Microkit delegation bundle (.dlg)")
    args = parser.parse_args()

    data, delegators = parse_delegation_bundle(args.bundle)
    print_bundle(args.bundle, data, delegators)


if __name__ == "__main__":
    main()

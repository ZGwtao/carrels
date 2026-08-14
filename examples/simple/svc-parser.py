# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

import struct

p = "build/container_monitor.svc"

with open(p, "rb") as f:
    b = f.read()

magic = b[:8]
version, reserved, service_count, total_size = struct.unpack_from("<HHII", b, 8)

print("magic:", magic)
print("version:", version)
print("service_count:", service_count)
print("total_size:", total_size)

off = 20

for i in range(service_count):
    record_size = struct.unpack_from("<I", b, off)[0]
    pd_id = struct.unpack_from("<Q", b, off + 4)[0]
    service_id = b[off + 12]
    service_type = b[off + 13]
    resource_count = struct.unpack_from("<I", b, off + 14)[0]
    path_len = struct.unpack_from("<I", b, off + 18)[0]

    print(f"\nservice {i}:")
    print("  record_size:", record_size)
    print("  pd_id:", pd_id)
    print("  service_id:", service_id)
    print("  service_type:", service_type)
    print("  resource_count:", resource_count)

    pos = off + 22

    for r in range(resource_count):
        kind = b[pos]
        value = struct.unpack_from("<Q", b, pos + 1)[0]
        print(f"  resource {r}: kind={kind}, value={value}")
        pos += 9

    path = b[pos:pos + path_len].decode()
    print("  path:", path)

    off += record_size

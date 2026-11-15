#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Check the shared BAR protocol using a Linux PCI sysfs resource0 file."""

import argparse
import mmap
import secrets
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("resource", help="/sys/bus/pci/devices/<BDF>/resource0")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()
    with open(args.resource, "r+b", buffering=0) as resource:
        with mmap.mmap(resource.fileno(), 0x100000) as bar:
            def read(offset):
                return struct.unpack_from("<I", bar, offset)[0]

            if read(0) != 0x45504331:
                raise SystemExit("BAR0 does not contain the shared BAR sample signature")
            heartbeat = read(12)
            request = read(8) ^ (secrets.randbits(32) | 1)
            struct.pack_into("<I", bar, 4, request)
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if read(8) == request and read(12) != heartbeat:
                    print(f"BAR0 echo passed: 0x{request:08x}, heartbeat {read(12)}")
                    return
                time.sleep(0.01)
            raise SystemExit("Timed out waiting for the endpoint response")


if __name__ == "__main__":
    main()

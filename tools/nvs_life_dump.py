#!/usr/bin/env python3
"""Read-only decoder for the XUNA life keys in an ESP-IDF NVS partition dump.

Input: a raw NVS partition image (today 0x9000 / 0x5000, read with
`esptool.py read_flash`), never the live device. Lists the keys of namespace
`xuna_life` (v1 `slot0/slot1`, v2 `life0/life1`, identity) and, with
`--out-dir`, writes each blob as `<key>.bin` for host tools such as
`sim/migration_check.cpp`. The input file is only read.

Format reference: ESP-IDF NVS (pages of 4096 B: 32 B header, 32 B entry state
bitmap, 126 entries of 32 B; blob v2 = BLOB_IDX 0x48 + BLOB_DATA 0x42 chunks,
legacy single BLOB 0x41). Only entries in state WRITTEN are used.
"""
import argparse
import os
import struct
import sys

PAGE = 4096
ENTRY = 32
ENTRIES = 126
PAGE_ACTIVE = 0xFFFFFFFE
PAGE_FULL = 0xFFFFFFFC
TYPE_U8 = 0x01
TYPE_STR = 0x21
TYPE_BLOB = 0x41
TYPE_BLOB_DATA = 0x42
TYPE_BLOB_IDX = 0x48
STATE_WRITTEN = 0b10


def entries(image):
    """Yields (page_seq, index, raw entry, payload bytes) for written entries."""
    for base in range(0, len(image) - PAGE + 1, PAGE):
        state, seq = struct.unpack_from("<II", image, base)
        if state not in (PAGE_ACTIVE, PAGE_FULL):
            continue
        bitmap = image[base + 32:base + 64]
        i = 0
        while i < ENTRIES:
            bits = (bitmap[i // 4] >> ((i % 4) * 2)) & 0b11
            off = base + 64 + i * ENTRY
            raw = image[off:off + ENTRY]
            span = raw[2] if raw[2] else 1
            if bits == STATE_WRITTEN:
                payload = image[off + ENTRY:off + span * ENTRY]
                yield seq, i, raw, payload
                i += span
            else:
                i += 1


def key_of(raw):
    return raw[8:24].split(b"\0", 1)[0].decode("ascii", "replace")


def decode(image, namespace):
    ns_index = None
    for _, _, raw, _ in entries(image):
        if raw[0] == 0 and raw[1] == TYPE_U8 and key_of(raw) == namespace:
            ns_index = raw[24]
    if ns_index is None:
        return {}
    chunks, indexes, blobs, scalars = {}, {}, {}, {}
    for seq, _, raw, payload in entries(image):
        if raw[0] != ns_index:
            continue
        key, kind, chunk = key_of(raw), raw[1], raw[3]
        if kind == TYPE_BLOB_DATA:
            size = struct.unpack_from("<H", raw, 24)[0]
            chunks[(key, chunk)] = (seq, payload[:size])
        elif kind == TYPE_BLOB_IDX:
            size, count, start = struct.unpack_from("<IBB", raw, 24)
            indexes[key] = (size, count, start)
        elif kind == TYPE_BLOB:
            size = struct.unpack_from("<H", raw, 24)[0]
            blobs[key] = payload[:size]
        elif kind == TYPE_STR:
            size = struct.unpack_from("<H", raw, 24)[0]
            scalars[key] = payload[:size].rstrip(b"\0").decode("utf-8", "replace")
        else:
            width = {0x01: 1, 0x11: 1, 0x02: 2, 0x12: 2, 0x04: 4, 0x14: 4,
                     0x08: 8, 0x18: 8}.get(kind)
            if width:
                fmt = {1: "B", 2: "H", 4: "I", 8: "Q"}[width]
                if kind & 0x10:
                    fmt = fmt.lower()
                scalars[key] = struct.unpack_from("<" + fmt, raw, 24)[0]
    for key, (size, count, start) in indexes.items():
        parts = [chunks.get((key, start + n)) for n in range(count)]
        if all(parts):
            data = b"".join(p[1] for p in parts)
            if len(data) == size:
                blobs[key] = data
    return {"blobs": blobs, "scalars": scalars}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", help="raw NVS partition dump (read only)")
    parser.add_argument("--namespace", default="xuna_life")
    parser.add_argument("--out-dir", help="write each blob as <key>.bin here")
    args = parser.parse_args()
    with open(args.image, "rb") as handle:
        image = handle.read()
    result = decode(image, args.namespace)
    if not result:
        print("namespace %s not found" % args.namespace)
        return 1
    for key, value in sorted(result["scalars"].items()):
        print("scalar %-15s %s" % (key, value))
    for key, data in sorted(result["blobs"].items()):
        head = data[:4].hex(" ")
        print("blob   %-15s %4d B  %s" % (key, len(data), head))
        if args.out_dir:
            if os.path.abspath(args.out_dir) == os.path.dirname(os.path.abspath(args.image)):
                print("refusing to write next to the backup; choose another --out-dir")
                return 2
            os.makedirs(args.out_dir, exist_ok=True)
            with open(os.path.join(args.out_dir, key + ".bin"), "wb") as out:
                out.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())

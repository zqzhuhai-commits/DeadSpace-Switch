#!/usr/bin/env python3
"""Read back a built RomFS or NRO and hash every file against its manifest."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct


def verify(path):
    with Path(path).open("rb") as f:
        length = f.seek(0, 2)
        def read(offset, size):
            if offset < 0 or size < 0 or offset + size > length:
                raise ValueError("Package range outside file")
            f.seek(offset)
            data = f.read(size)
            if len(data) != size:
                raise ValueError("Truncated package")
            return data
        base, size = 0, length
        if length >= 0x80 and read(0x10, 4) == b"NRO0":
            aset = struct.unpack("<I", read(0x18, 4))[0]
            if read(aset, 4) != b"ASET":
                raise ValueError("Missing NRO asset header")
            offset, size = struct.unpack("<QQ", read(aset + 40, 16))
            base = aset + offset
        if size < 80 or base + size > length:
            raise ValueError("Invalid RomFS extent")
        h = struct.unpack("<10Q", read(base, 80))
        if h[0] != 80 or h[9] > size:
            raise ValueError("Invalid RomFS header")
        for offset, count in [(h[1],h[2]),(h[3],h[4]),(h[5],h[6]),(h[7],h[8])]:
            if offset < 80 or offset + count > size or count > 16 * 1024**2:
                raise ValueError("Invalid RomFS metadata range")
        data_end = min((v for v in [h[1], h[3], h[5], h[7]] if v >= h[9]), default=size)
        dirs, files = read(base + h[3], h[4]), read(base + h[7], h[8])
        def entry(table, off, fmt):
            n = struct.calcsize(fmt)
            if off < 0 or off + n > len(table):
                raise ValueError("RomFS entry outside metadata")
            values = struct.unpack_from(fmt, table, off)
            name_len = values[-1]
            if name_len > 1024 or off + n + name_len > len(table):
                raise ValueError("Invalid RomFS name length")
            name = table[off+n:off+n+name_len].decode("utf-8")
            if name in [".", ".."] or any(c in name for c in ["/", "\\", "\0"]):
                raise ValueError("Unsafe RomFS name")
            return values, name
        pending, seen_dirs, seen_files, found = [(0, "", 0)], set(), set(), {}
        end = 0xffffffff
        while pending:
            off, parent, depth = pending.pop()
            if off in seen_dirs or depth > 64 or len(seen_dirs) > 10000:
                raise ValueError("RomFS directory cycle or depth limit")
            seen_dirs.add(off)
            d, name = entry(dirs, off, "<6I")
            directory = (parent + "/" + name).strip("/")
            child = d[2]
            siblings = set()
            while child != end:
                if child in siblings:
                    raise ValueError("RomFS sibling cycle")
                siblings.add(child)
                item, _ = entry(dirs, child, "<6I")
                pending.append((child, directory, depth+1))
                child = item[1]
            child = d[3]
            while child != end:
                if child in seen_files or len(seen_files) > 10000:
                    raise ValueError("RomFS file cycle or count limit")
                seen_files.add(child)
                item, name = entry(files, child, "<IIQQII")
                if not name or item[0] != off or item[2] + item[3] > data_end - h[9]:
                    raise ValueError("Invalid RomFS file entry")
                full = (directory + "/" + name).lstrip("/")
                if full in found:
                    raise ValueError("Duplicate RomFS path")
                found[full] = (base + h[9] + item[2], item[3])
                child = item[1]
        offset, count = found.pop("manifest.json")
        if count > 16 * 1024**2:
            raise ValueError("Manifest too large")
        manifest = json.loads(read(offset, count))
        expected = {}
        for item in manifest["files"]:
            name = item["path"]
            if PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts or name in expected:
                raise ValueError("Unsafe or duplicate manifest path")
            expected[name] = item
        if set(expected) != set(found) or len(found) != manifest["file_count"]:
            raise ValueError("Embedded file set differs from manifest")
        total = 0
        for name, (offset, count) in found.items():
            digest = hashlib.sha256()
            for start in range(0, count, 1024**2):
                digest.update(read(offset + start, min(1024**2, count-start)))
            if count != expected[name]["size"] or digest.hexdigest() != expected[name]["sha256"]:
                raise ValueError("Embedded content mismatch: " + name)
            total += count
        if total != manifest["total_bytes"]:
            raise ValueError("Embedded byte count mismatch")
        return {"verified_files": len(found), "verified_bytes": total, "game_playable": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    print(json.dumps(verify(parser.parse_args().package), indent=2))

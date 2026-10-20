#!/usr/bin/env python3
"""Audit the supported donor and dry-run guest mapping/relocation on the host.

Never executes code, allocates executable memory, or changes the input library.
External symbols remain unresolved; a successful audit is NOT a working loader.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

EXPECTED = "0ed42b611415015807f759ec9b5457857143ce39"
RELOC_NAMES = {0: "R_ARM_NONE", 2: "R_ARM_ABS32", 3: "R_ARM_REL32",
               21: "R_ARM_GLOB_DAT", 22: "R_ARM_JUMP_SLOT", 23: "R_ARM_RELATIVE"}


def audit(path, base=0x10000000):
    if not 0 <= base <= 0xffffffff or base % 4096:
        raise ValueError("Guest base must be an aligned 32-bit address")
    raw = Path(path).read_bytes()
    digest = hashlib.sha1(raw).hexdigest()
    if digest != EXPECTED:
        raise ValueError("Unsupported donor SHA1: " + digest)

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(raw):
            raise ValueError("Truncated ELF structure")
        return struct.unpack_from(fmt, raw, offset)

    header = unpack("<16sHHIIIIIHHHHHH", 0)
    if header[0][:7] != b"\x7fELF\x01\x01\x01" or header[1:3] != (3, 40):
        raise ValueError("Expected ELF32 ARM shared object")
    phoff, shoff = header[5], header[6]
    phsize, phnum, shsize, shnum = header[9:13]
    if phsize != 32 or shsize != 40:
        raise ValueError("Unexpected ELF table entry sizes")
    phdrs = [unpack("<8I", phoff + i * phsize) for i in range(phnum)]
    sections = [unpack("<10I", shoff + i * shsize) for i in range(shnum)]
    segments = [p for p in phdrs if p[0] == 1]
    if not segments:
        raise ValueError("No load segments")
    end = max(p[2] + p[5] for p in segments)
    if end > 64 * 1024**2 or base + end > 2**32:
        raise ValueError("Guest mapping outside 32-bit bounds")
    memory = bytearray(end)
    occupied = []
    for p in segments:
        _, off, va, _, filesz, memsz, flags, align = p
        if filesz > memsz or off + filesz > len(raw):
            raise ValueError("Invalid load segment")
        if align > 1 and (align & (align - 1) or va % align != off % align):
            raise ValueError("Invalid segment alignment")
        if any(va < hi and va + memsz > lo for lo, hi in occupied):
            raise ValueError("Overlapping load segments")
        occupied.append((va, va + memsz))
        memory[va:va + filesz] = raw[off:off + filesz]
        if any(memory[va + filesz:va + memsz]):
            raise ValueError("BSS was not initialized to zero")

    def mapped(va, length):
        if not any(lo <= va and va + length <= hi for lo, hi in occupied):
            raise ValueError("Guest access outside mapped segments")

    def string(table, offset):
        if not 0 <= offset < len(table):
            raise ValueError("String offset outside table")
        stop = table.find(b"\0", offset)
        if stop < 0:
            raise ValueError("Unterminated ELF string")
        return table[offset:stop].decode("ascii")

    symbols = {}
    imports, exports = set(), set()
    for i, s in enumerate(sections):
        if s[1] != 11:  # SHT_DYNSYM
            continue
        if s[9] != 16 or s[5] % 16 or s[6] >= len(sections):
            raise ValueError("Invalid dynamic symbol table")
        strs = sections[s[6]]
        table = raw[strs[4]:strs[4] + strs[5]]
        entries = []
        for off in range(s[4], s[4] + s[5], 16):
            name, value, size, info, other, ndx = unpack("<IIIBBH", off)
            name = string(table, name)
            entries.append((name, value, ndx, info >> 4))
            if name and ndx == 0:
                imports.add(name)
            elif name and (name.startswith("Java_") or name == "JNI_OnLoad"):
                exports.add(name)
        symbols[i] = entries

    relocations, applied, deferred = Counter(), Counter(), Counter()
    for s in sections:
        if s[1] != 9:  # SHT_REL
            continue
        if s[9] != 8 or s[5] % 8 or s[6] not in symbols:
            raise ValueError("Invalid relocation section")
        table = symbols[s[6]]
        for off in range(s[4], s[4] + s[5], 8):
            target, info = unpack("<II", off)
            kind, index = info & 255, info >> 8
            if kind not in RELOC_NAMES:
                raise ValueError("Unsupported relocation type: " + str(kind))
            label = RELOC_NAMES[kind]
            relocations[label] += 1
            if kind == 0:
                continue
            mapped(target, 4)
            addend = struct.unpack_from("<I", memory, target)[0]
            if kind == 23:
                if index:
                    raise ValueError("RELATIVE relocation has a symbol")
                result = base + addend
            else:
                if index >= len(table):
                    raise ValueError("Relocation symbol index out of range")
                name, value, ndx, binding = table[index]
                if ndx == 0:
                    deferred[name] += 1
                    continue
                symbol_address = value if ndx == 0xfff1 else base + value
                if kind == 2:
                    result = symbol_address + addend
                elif kind == 3:
                    result = symbol_address + addend - (base + target)
                elif kind in (21, 22):
                    result = symbol_address
            struct.pack_into("<I", memory, target, result & 0xffffffff)
            applied[label] += 1

    needed = []
    textrel = False
    for s in sections:
        if s[1] != 6:  # SHT_DYNAMIC
            continue
        strs = sections[s[6]]
        table = raw[strs[4]:strs[4] + strs[5]]
        for off in range(s[4], s[4] + s[5], 8):
            tag, value = unpack("<II", off)
            if tag == 1:
                needed.append(string(table, value))
            if tag == 22:
                textrel = True
    return {"donor_sha1": digest, "file_size": len(raw), "format": "ELF32 ARM ET_DYN",
            "guest_base": hex(base), "guest_image_size": end, "load_segments": len(segments),
            "bss_zero_fill": "pass", "needed_libraries": needed, "text_relocations_required": textrel,
            "imports": sorted(imports), "gl_import_count": sum(n.startswith("gl") for n in imports),
            "jni_exports": sorted(exports), "relocations": dict(relocations),
            "host_dry_run_applied": dict(applied), "external_relocations_deferred": dict(sorted(deferred.items())),
            "dry_run_image_sha256": hashlib.sha256(memory).hexdigest(),
            "limitations": ["Host bytearray only, not a Switch guest-memory mapping",
                            "External imports unresolved; constructors and game entry points never called",
                            "No instruction execution or gameplay validation"]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("library", type=Path)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    try:
        result = json.dumps(audit(a.library), indent=2) + "\n"
        if a.output:
            a.output.write_text(result)
        else:
            print(result, end="")
    except (OSError, ValueError, struct.error) as e:
        p.exit(2, f"Audit failed: {e}\n")


if __name__ == "__main__":
    main()

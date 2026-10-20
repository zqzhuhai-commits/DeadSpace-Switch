#!/usr/bin/env python3
"""Compare every pinned HD mip decoded by runtime C++ to texture2ddecoder 1.0.6.

Requires a locally extracted pack and the optional reference decoder; no download
or game execution. Keeps only metadata in the report, never resource bytes.
"""
import argparse
from collections import Counter
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = r'''
#include "hd_texture.hpp"
#include <cstring>
extern "C" int decode_hd(const unsigned char* raw, size_t size, unsigned level, unsigned char* output, size_t capacity) {
  try {
    auto texture=ds::HdTexture::parse(std::vector<unsigned char>(raw,raw+size));
    auto rgba=texture.rgba(level);
    if(rgba.size()!=capacity)return 0;
    std::memcpy(output,rgba.data(),rgba.size());return 1;
  } catch(...) { return 0; }
}
'''


def run(pack, reference_path):
    if reference_path:
        sys.path.insert(0, str(reference_path))
    import texture2ddecoder as reference
    if reference.__version__ != "1.0.6":
        raise ValueError("Expected texture2ddecoder 1.0.6")
    audit = json.loads((ROOT / "docs/REANIMATED-HD-AUDIT.json").read_text())
    counts, mip_counts, output_bytes = Counter(), Counter(), 0
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        (work / "bridge.cpp").write_text(BRIDGE)
        lib = work / "hd-check.so"
        subprocess.run(["g++", "-std=c++17", "-O2", "-shared", "-fPIC", "-I" + str(ROOT / "runtime"),
                        str(work / "bridge.cpp"), str(ROOT / "runtime/hd_texture.cpp"),
                        str(ROOT.parent / "src/atc_decompress.cpp"),
                        str(ROOT.parent / "third_party/powervr/PVRTDecompress.cpp"), "-o", str(lib)], check=True)
        native = ctypes.CDLL(str(lib))
        native.decode_hd.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t]
        native.decode_hd.restype = ctypes.c_int
        for item in audit["files"]:
            raw = (pack / item["name"]).read_bytes()
            if len(raw) != item["bytes"] or hashlib.sha256(raw).hexdigest() != item["sha256"]:
                raise ValueError("Pinned texture content differs: " + item["name"])
            w, h, levels = item["width"], item["height"], item["mip_levels"]
            fmt = int(item["gl_internal_format"], 16)
            cursor = 64 + struct.unpack_from("<I", raw, 60)[0]
            counts[item["encoding"]] += 1
            for level in range(levels):
                size = struct.unpack_from("<I", raw, cursor)[0]
                cursor += 4
                encoded = raw[cursor:cursor+size]
                cursor += (size + 3) & ~3
                width, height = max(1, w >> level), max(1, h >> level)
                expected = reference.decode_etc1(encoded, width, height) if fmt == 0x8D64 else reference.decode_atc_rgba8(encoded, width, height)
                rgba = bytearray(expected)
                rgba[0::4], rgba[2::4] = expected[2::4], expected[0::4]  # reference returns BGRA
                actual = ctypes.create_string_buffer(width * height * 4)
                if native.decode_hd(raw, len(raw), level, actual, len(actual)) != 1 or actual.raw != rgba:
                    raise ValueError(f"Runtime/reference mismatch: {item['name']} level {level}")
                mip_counts[item["encoding"]] += 1
                output_bytes += len(actual)
    return {"source_apk_sha256": audit["apk_sha256"], "reference": "texture2ddecoder 1.0.6 (BGRA converted to RGBA)",
            "texture_counts": dict(counts), "mip_counts": dict(mip_counts), "compared_rgba_bytes": output_bytes,
            "all_mips_equal": True, "switch_hardware_tested": False, "original_game_executed": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack", type=Path)
    parser.add_argument("--reference-site-packages", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = run(args.pack, args.reference_site_packages)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

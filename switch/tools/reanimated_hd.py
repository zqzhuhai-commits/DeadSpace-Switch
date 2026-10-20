#!/usr/bin/env python3
"""Audit/extract Reanimated v1.3 HD textures, without importing its game/runtime.

Extraction is not runtime integration. Requires Python 3.11+. No APK execution.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import struct
import tempfile
import zipfile
import zlib

APK_SHA256 = "dbd94ee54fc3fefdddb101fac4eba1e2058a5db7cfcf30ac41ad96e88b285a3a"
APK_BYTES = 715366829
DONOR_SHA1 = "0ed42b611415015807f759ec9b5457857143ce39"
PREFIX = "assets/texture_hd/"
KTX_MAGIC = b"\xabKTX 11\xbb\r\n\x1a\n"
FORMATS = {0x8D64: ("ETC1_RGB8_OES", 8), 0x87EE: ("ATC_RGBA_INTERPOLATED_ALPHA_AMD", 16)}
MAX_M3G = 64 * 1024**2


def ktx_info(raw):
    if len(raw) < 64 or raw[:12] != KTX_MAGIC:
        raise ValueError("Not a KTX1 texture")
    endian, typ, typ_size, fmt, internal, base, w, h, depth, arrays, faces, levels, metadata = struct.unpack_from("<13I", raw, 12)
    if endian != 0x04030201 or typ or typ_size != 1 or fmt or internal not in FORMATS:
        raise ValueError("Unsupported HD KTX encoding")
    if not 1 <= w <= 4096 or not 1 <= h <= 4096 or depth or arrays or faces != 1:
        raise ValueError("Unsupported HD KTX dimensions/shape")
    if levels != max(w, h).bit_length() or metadata > 1024**2 or metadata % 4:
        raise ValueError("Invalid HD KTX mip chain/metadata")
    cursor = 64 + metadata
    for level in range(levels):
        if cursor + 4 > len(raw):
            raise ValueError("Truncated KTX level header")
        count = struct.unpack_from("<I", raw, cursor)[0]
        cursor += 4
        expected = ((max(1, w >> level) + 3) // 4) * ((max(1, h >> level) + 3) // 4) * FORMATS[internal][1]
        if count != expected or cursor + count > len(raw):
            raise ValueError("Invalid KTX compressed level size")
        cursor += (count + 3) & ~3
    if cursor != len(raw):
        raise ValueError("KTX trailing data or padding mismatch")
    return {"width": w, "height": h, "mip_levels": levels, "gl_internal_format": hex(internal),
            "encoding": FORMATS[internal][0], "gl_base_format": hex(base)}


def texture_key(width, height, gl_format, level0):
    """Observed v1.3 key: FNV-1a64(LE32 width,height,format || level-0 bytes).

    Independently verified against all 222 packaged texture names. The pixel
    type and mip levels after level zero are not part of this key.
    """
    value = 0xCBF29CE484222325
    for part in (struct.pack("<III", width, height, gl_format), level0):
        for byte in part:
            value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def images(raw):
    """Read bounded Image2D objects from the supplied JSR184/IM-M3G assets."""
    if len(raw) > MAX_M3G or raw[:12] not in [b"\xabJSR184\xbb\r\n\x1a\n", b"\xabIM-M3G\xbb\r\n\x1a\n"]:
        raise ValueError("Unsupported M3G header/size")
    position, index = 12, 0
    while position < len(raw):
        if position + 13 > len(raw):
            raise ValueError("Truncated M3G section")
        compression, total, _ = struct.unpack_from("<BII", raw, position)
        if compression not in [0, 1] or total < 13 or position + total > len(raw):
            raise ValueError("Invalid M3G section range")
        section = raw[position+9:position+total-4]
        position += total
        if compression:
            inflater = zlib.decompressobj()
            section = inflater.decompress(section, MAX_M3G + 1)
            if len(section) > MAX_M3G or not inflater.eof or inflater.unused_data:
                raise ValueError("Invalid/oversized compressed M3G section")
        p = 0
        while p < len(section):
            if p + 5 > len(section):
                raise ValueError("Truncated M3G object")
            kind, count = struct.unpack_from("<BI", section, p)
            p += 5
            if p + count > len(section):
                raise ValueError("M3G object outside section")
            data = memoryview(section)[p:p+count]
            p += count
            index += 1
            if kind != 10:
                continue
            q = 4
            def word():
                nonlocal q
                if q + 4 > len(data):
                    raise ValueError("Truncated Image2D word")
                v = struct.unpack_from("<I", data, q)[0]
                q += 4
                return v
            animations = word()
            if animations > 4096:
                raise ValueError("Image2D animation count bound")
            q += animations * 4
            parameters = word()
            if parameters > 4096:
                raise ValueError("Image2D parameter count bound")
            for _ in range(parameters):
                word()
                length = word()
                q += length
                if q > len(data):
                    raise ValueError("Image2D parameter outside object")
            if q + 10 > len(data):
                raise ValueError("Truncated Image2D descriptor")
            fmt, mutable, width, height = struct.unpack_from("<BBII", data, q)
            q += 10
            if mutable:
                continue
            palette = word()
            q += palette
            size = word()
            if q + size > len(data) or not 1 <= width <= 4096 or not 1 <= height <= 4096:
                raise ValueError("Image2D pixel range/dimensions")
            pixels = data[q:q+size]
            if palette:
                continue
            standard = {96: (0x1906, 1), 97: (0x1909, 1), 98: (0x190A, 2), 99: (0x1907, 3), 100: (0x1908, 4)}
            if fmt in standard:
                gl_format, stride = standard[fmt]
                n = width * height * stride
                if len(pixels) != n:
                    continue
            elif fmt in [116, 124, 125]:
                # Verified by exact key matches; these supplied objects upload ETC1.
                gl_format = 0x8D64
                n = ((width + 3) // 4) * ((height + 3) // 4) * 8
                if len(pixels) < n:
                    raise ValueError("Short compressed Image2D level zero")
            else:
                continue
            yield {"key": texture_key(width, height, gl_format, pixels[:n]), "object": index,
                   "base_dimensions": [width, height], "base_image_format": fmt,
                   "base_gl_format": hex(gl_format), "base_level0_bytes": n}


def inspect(apk, output=None, map_originals=False):
    apk = Path(apk)
    if apk.stat().st_size != APK_BYTES:
        raise ValueError("Expected the exact Reanimated v1.3 release APK size")
    with apk.open("rb") as f:
        if hashlib.file_digest(f, "sha256").hexdigest() != APK_SHA256:
            raise ValueError("APK SHA256 differs from the published v1.3 release")
    result = {"format": 1, "source": "DeadSpaceReanimated-1.3.apk", "apk_sha256": APK_SHA256,
              "runtime_integrated": False, "switch_hardware_tested": False, "files": []}
    with zipfile.ZipFile(apk) as archive:
        seen, total = set(), 0
        for item in archive.infolist():
            if not item.filename.startswith(PREFIX):
                continue
            name = item.filename[len(PREFIX):]
            if not re.fullmatch(r"[0-9a-f]{16}\.ktx", name) or name in seen or stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError("Unsafe/duplicate HD texture path")
            seen.add(name)
            if item.file_size > 32 * 1024**2:
                raise ValueError("HD texture size bound")
            total += item.file_size
            if total > 512 * 1024**2:
                raise ValueError("HD pack total size bound")
            data = archive.read(item)
            entry = {"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), **ktx_info(data)}
            result["files"].append(entry)
            if output is not None:
                (output / name).write_bytes(data)
        if len(seen) != 222 or total != 340222792:
            raise ValueError("Unexpected v1.3 HD file set")
        result.update(file_count=len(seen), total_bytes=total, encoding_counts=dict(Counter(f["encoding"] for f in result["files"])))
        result["apk_game_library_sha1"] = hashlib.sha1(archive.read("assets/game/libDeadSpace32.so")).hexdigest()
        result["matches_current_donor_whitelist"] = result["apk_game_library_sha1"] == DONOR_SHA1
        if map_originals:
            by_key = {Path(f["name"]).stem: f for f in result["files"]}
            mappings = []
            for item in archive.infolist():
                if not item.filename.startswith("assets/game/assets/") or not item.filename.endswith(".m3g"):
                    continue
                if item.file_size > MAX_M3G:
                    raise ValueError("M3G source file size bound")
                for image in images(archive.read(item)):
                    if image["key"] in by_key:
                        hd = by_key[image["key"]]
                        mappings.append({**image, "source": item.filename[len("assets/game/assets/"):],
                                         "hd_dimensions": [hd["width"], hd["height"]]})
            result["mappings"] = mappings
            result["mapped_texture_count"] = len({m["key"] for m in mappings})
            result["mapping_scope"] = "APK's own base assets; PSV donor assets not compared"
    return result


def extract(apk, output, map_originals=False, donor_assets=None):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; use a new directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".reanimated-hd-", dir=output.parent))
    try:
        result = inspect(apk, stage, map_originals)
        if donor_assets is not None:
            result["donor_compatibility"] = match_assets(result, donor_assets)
        (stage / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
        stage.rename(output)
        return result
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def match_assets(report, assets):
    """Compare HD keys to the actual imported donor, never to APK substitutes.

    A match proves identity of a base texture, not that the renderer replaces it.
    Unknown M3G signatures are reported rather than counted as matching assets.
    """
    assets = Path(assets)
    if not assets.is_dir() or assets.is_symlink():
        raise ValueError("Expected an imported donor assets directory")
    by_key = {Path(f["name"]).stem: f for f in report["files"]}
    mappings, unsupported = [], []
    scanned = 0
    for path in sorted(assets.rglob("*")):
        if path.suffix.lower() != ".m3g":
            continue
        if path.is_symlink() or any(p.is_symlink() for p in path.parents):
            raise ValueError("Donor M3G symlinks are not accepted")
        if not path.is_file() or path.stat().st_size > MAX_M3G:
            raise ValueError("Donor M3G file type/size bound")
        name = path.relative_to(assets).as_posix()
        raw = path.read_bytes()
        if raw[:12] not in [b"\xabJSR184\xbb\r\n\x1a\n", b"\xabIM-M3G\xbb\r\n\x1a\n"]:
            unsupported.append(name)
            continue
        scanned += 1
        for image in images(raw):
            if image["key"] not in by_key:
                continue
            hd = by_key[image["key"]]
            w, h = image["base_dimensions"]
            if hd["width"] <= w or hd["width"] % w or hd["height"] % h or hd["width"] // w != hd["height"] // h:
                raise ValueError("HD/base scale mismatch: " + name)
            mappings.append({**image, "source": name, "hd_dimensions": [hd["width"], hd["height"]]})
    matched = {m["key"] for m in mappings}
    return {"mapping_scope": "supplied donor assets; exact base texture key",
            "scanned_m3g_files": scanned, "unsupported_m3g_files": unsupported,
            "mapped_texture_count": len(matched), "mappings": mappings,
            "unmatched_hd_keys": sorted(set(by_key) - matched),
            "runtime_integrated": False, "switch_hardware_tested": False}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("apk", type=Path)
    p.add_argument("--extract", type=Path, help="New HD-only output directory")
    p.add_argument("--map-originals", action="store_true", help="Match HD keys against the APK's M3G Image2D objects")
    p.add_argument("--report", type=Path)
    p.add_argument("--donor-data", type=Path, help="Compare against a validated Xperia Play/Vita data root")
    args = p.parse_args()
    try:
        if args.donor_data:
            from prepare_data import validate
            validate(args.donor_data)
        donor_assets = args.donor_data / "assets" if args.donor_data else None
        report = extract(args.apk, args.extract, args.map_originals, donor_assets) if args.extract else inspect(args.apk, map_originals=args.map_originals)
        if args.donor_data and not args.extract:
            report["donor_compatibility"] = match_assets(report, args.donor_data / "assets")
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({k: v for k, v in report.items() if k not in ["files", "mappings"]}, indent=2))
    except (OSError, ValueError, zipfile.BadZipFile, struct.error, zlib.error) as e:
        p.exit(2, f"HD audit failed: {e}\n")

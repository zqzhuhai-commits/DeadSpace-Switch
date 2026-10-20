#!/usr/bin/env python3
"""Import a user-owned Xperia Play/Vita game data tree without executing it.

Only data and the exact supported game library are copied. VPK executables,
Vita kernel plugins, keys and unrelated files are never imported.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile

SHA1 = "0ed42b611415015807f759ec9b5457857143ce39"
LIB = "lib/armeabi/libEAMGameDeadSpace.so"
REQUIRED = (LIB, "assets/EAMCore.ini", "assets/published/data/achievements.sb")
MAX_TOTAL = 2 * 1024**3


def validate(stage):
    for name in REQUIRED:
        path = stage / name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"Missing non-empty game file: {name}")
    library = stage / LIB
    if library.stat().st_size > 64 * 1024**2:
        raise ValueError("Library too large")
    data = library.read_bytes()
    digest = hashlib.sha1(data).hexdigest()
    if digest != SHA1:
        raise ValueError(f"Unsupported library SHA1 {digest}; expected {SHA1}")
    if data[:7] != b"\x7fELF\x01\x01\x01" or data[16:20] != b"\x03\x00\x28\x00":
        raise ValueError("Expected little-endian ELF32 ARM shared library")
    return digest


def import_data(source, output):
    source, output = Path(source).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; choose a new directory")
    # A destination inside the source would recursively import its own staging tree.
    if source.is_dir() and (output == source or source in output.parents):
        raise ValueError("Output must be outside the source directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deadspace-stage-", dir=output.parent))
    try:
        if source.is_dir():
            candidates = [p for p in source.rglob("libEAMGameDeadSpace.so")
                          if p.as_posix().endswith("/" + LIB)]
            if len(candidates) != 1:
                raise ValueError("Expected exactly one game data tree containing " + LIB)
            root = candidates[0].parents[2]
            total = 0
            for name in (LIB, "assets"):
                entry = root / name
                paths = [entry] if entry.is_file() else entry.rglob("*")
                for item in paths:
                    if item.is_symlink() or any(p.is_symlink() for p in item.parents if p != source.parent):
                        raise ValueError("Symlinks are not accepted in game data")
                    if not item.is_file():
                        continue
                    total += item.stat().st_size
                    if total > MAX_TOTAL:
                        raise ValueError("Game data exceeds 2 GiB bound")
                    target = stage / item.relative_to(root)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(item, target)
        else:
            with zipfile.ZipFile(source) as archive:
                entries = archive.infolist()
                # Reject ambiguous paths even when outside the selected game tree.
                seen = set()
                for e in entries:
                    p = PurePosixPath(e.filename)
                    if p.is_absolute() or ".." in p.parts or "\\" in e.filename:
                        raise ValueError("Unsafe archive path")
                    if e.filename in seen:
                        raise ValueError("Duplicate archive path")
                    seen.add(e.filename)
                    if stat.S_ISLNK(e.external_attr >> 16):
                        raise ValueError("Archive symlinks are not accepted")
                libs = [e for e in entries if e.filename == LIB or e.filename.endswith("/" + LIB)]
                if len(libs) != 1:
                    raise ValueError("No unique game library found. A standalone VPK is insufficient; supply the Vita data ZIP or APK.")
                prefix = libs[0].filename[:-len(LIB)]
                total = 0
                for e in entries:
                    if not e.filename.startswith(prefix) or e.is_dir():
                        continue
                    rel = e.filename[len(prefix):]
                    if rel != LIB and not rel.startswith("assets/"):
                        continue
                    total += e.file_size
                    if total > MAX_TOTAL:
                        raise ValueError("Archive expands beyond 2 GiB bound")
                    target = stage / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(e) as src, target.open("xb") as dst:
                        shutil.copyfileobj(src, dst, 1024 * 1024)
        digest = validate(stage)
        manifest = {"format": 1, "library_sha1": digest, "source_name": source.name,
                    "asset_check": "required sentinels only; completeness not proven",
                    "game_execution": "not implemented"}
        (stage / "donor-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if output.exists():
            raise ValueError("Destination appeared during import")
        os.rename(stage, output)
        return manifest
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path, help="Extracted Vita/Android directory, ZIP, or APK")
    p.add_argument("output", type=Path, help="New destination, typically sdcard/switch/deadspace/gamedata")
    a = p.parse_args()
    try:
        print(json.dumps(import_data(a.source, a.output), indent=2))
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        p.exit(2, f"Import failed: {e}\n")


if __name__ == "__main__":
    main()

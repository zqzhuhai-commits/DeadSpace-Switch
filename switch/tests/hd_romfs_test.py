#!/usr/bin/env python3
"""Composition checks only: synthetic assets never bypass the production SHA gate."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_romfs
import reanimated_hd as hd


def require_failure(fn, message):
    try:
        fn()
    except ValueError:
        return
    raise AssertionError(message)


pixels = bytes(range(16))
key = hd.texture_key(2, 2, 0x1908, pixels)
obj = struct.pack("<III", 0, 0, 0) + struct.pack("<BBII", 100, 0, 2, 2) + struct.pack("<II", 0, 16) + pixels
objects = struct.pack("<BI", 10, len(obj)) + obj
m3g = b"\xabJSR184\xbb\r\n\x1a\n" + struct.pack("<BII", 0, len(objects) + 13, len(objects)) + objects + bytes(4)
unmatched = "0000000000000000.ktx"
pack = {"files": [{"name": key + ".ktx", "width": 4, "height": 4, "encoding": "fixture"},
                  {"name": unmatched, "width": 4, "height": 4, "encoding": "fixture"}],
        "apk_sha256": "fixture-only", "file_count": 2, "total_bytes": 14}

with tempfile.TemporaryDirectory() as folder:
    work = Path(folder)
    assets = work / "source/assets"
    assets.mkdir(parents=True)
    (assets / "same.m3g").write_bytes(m3g)
    (assets / "unknown.m3g").write_bytes(b"unknown signature")
    result = hd.match_assets(pack, assets)
    assert result["mapped_texture_count"] == 1
    assert result["unsupported_m3g_files"] == ["unknown.m3g"]
    assert not result["runtime_integrated"]
    changed = bytearray(m3g)
    changed[-5] ^= 1
    (assets / "same.m3g").write_bytes(changed)
    assert hd.match_assets(pack, assets)["mapped_texture_count"] == 0
    (assets / "same.m3g").write_bytes(m3g)
    (assets / "link.m3g").symlink_to(assets / "same.m3g")
    require_failure(lambda: hd.match_assets(pack, assets), "Accepted linked donor texture")
    (assets / "link.m3g").unlink()

    def fixture_import(source, target):
        (target / "assets").mkdir(parents=True)
        (target / "assets/a.m3g").write_bytes(m3g)

    def fixture_extract(apk, target):
        target.mkdir()
        (target / (key + ".ktx")).write_bytes(b"fixture")
        (target / unmatched).write_bytes(b"unused!")
        return pack

    with patch.object(prepare_romfs, "import_data", fixture_import), patch.object(hd, "extract", fixture_extract):
        output = work / "romfs"
        manifest = prepare_romfs.prepare(work / "source", output, work / "fixture.apk")
        assert manifest["hd_textures"]["matched_donor_textures"] == 1
        assert manifest["hd_textures"]["file_count"] == 1
        assert manifest["hd_textures"]["total_bytes"] == 7
        assert manifest["hd_textures"]["source_file_count"] == 2
        assert not (output / "texture_hd" / unmatched).exists()
        embedded = json.loads((output / "texture_hd/manifest.json").read_text())
        assert embedded["file_count"] == 1 and embedded["total_bytes"] == 7
        assert [entry["name"] for entry in embedded["files"]] == [key + ".ktx"]
        assert manifest["campaign_verified"] is False
        assert manifest["hd_textures"]["runtime_integrated"] is False
        for item in manifest["files"]:
            raw = (output / item["path"]).read_bytes()
            assert len(raw) == item["size"]
            assert hashlib.sha256(raw).hexdigest() == item["sha256"]
        assert any(item["path"].startswith("texture_hd/") for item in manifest["files"])
        assert json.loads((output / "manifest.json").read_text()) == manifest
        require_failure(lambda: prepare_romfs.prepare(work / "source", output), "Overwrote existing RomFS")
        with patch.object(hd, "match_assets", return_value={"mapped_texture_count": 0}):
            require_failure(lambda: prepare_romfs.prepare(work / "source", work / "failed", work / "fixture.apk"), "Embedded unmatched HD pack")
        assert not (work / "failed").exists()
        assert not list(work.glob(".deadspacemobile-romfs-*"))
    # No patch: the public importer still rejects these synthetic assets.
    require_failure(lambda: prepare_romfs.prepare(work / "source", work / "invalid"), "Production donor gate bypassed")
    assert not (work / "invalid").exists()
print("PASS: donor-specific texture keys, changed pixels, symlinks, HD manifest coverage, transactional rollback and production donor gate")

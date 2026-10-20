#!/usr/bin/env python3
"""Stage user-supplied game files for an embedded RomFS; never publish assets."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from prepare_data import import_data
import reanimated_hd


def prepare(source, output, hd_apk=None):
    source, output = Path(source).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    if source.is_dir() and (output == source or source in output.parents):
        raise ValueError("Output must be outside source")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deadspacemobile-romfs-", dir=output.parent))
    try:
        import_data(source, stage / "gamedata")
        hd_summary = None
        if hd_apk is not None:
            pack = reanimated_hd.extract(hd_apk, stage / "texture_hd")
            compatibility = reanimated_hd.match_assets(pack, stage / "gamedata/assets")
            if not compatibility["mapped_texture_count"]:
                raise ValueError("No HD keys match this donor; refusing to embed an unusable pack")
            (stage / "texture_hd/donor-compatibility.json").write_text(json.dumps(compatibility, indent=2) + "\n")
            hd_summary = {"directory": "texture_hd", "apk_sha256": pack["apk_sha256"],
                          "file_count": pack["file_count"], "total_bytes": pack["total_bytes"],
                          "matched_donor_textures": compatibility["mapped_texture_count"],
                          "runtime_integrated": False, "switch_hardware_tested": False}
        entries = []
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                entries.append({"path": path.relative_to(stage).as_posix(), "size": path.stat().st_size, "sha256": digest})
        manifest = {"format": 1, "title": "Dead Space Mobile", "files": entries,
                    "file_count": len(entries), "total_bytes": sum(e["size"] for e in entries),
                    "campaign_verified": False}
        if hd_summary is not None:
            manifest["hd_textures"] = hd_summary
        (stage / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        stage.rename(output)
        return manifest
    finally:
        if stage.exists():
            shutil.rmtree(stage)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--hd-apk", type=Path, help="Pinned Reanimated v1.3 APK; embed HD-only files after matching the donor")
    args = parser.parse_args()
    report = prepare(args.source, args.output, args.hd_apk)
    print(f"Staged {report['file_count']} files, {report['total_bytes']} bytes. Not a playable build.")

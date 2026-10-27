#!/usr/bin/env python3
"""Stage user-supplied game files for an embedded RomFS; never publish assets."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from prepare_data import import_data
import reanimated_hd
from set_save_language import localize


def prepare(source, output, hd_apk=None, settings=None):
    source, output = Path(source).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    if source.is_dir() and (output == source or source in output.parents):
        raise ValueError("Output must be outside source")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deadspacemobile-romfs-", dir=output.parent))
    try:
        import_data(source, stage / "gamedata")
        if settings is not None:
            preferences=stage/"gamedata/preferences";preferences.mkdir()
            (preferences/"settings.sb").write_bytes(localize(Path(settings).read_bytes(),"zh-cn"))
        hd_summary = None
        if hd_apk is not None:
            pack = reanimated_hd.extract(hd_apk, stage / "texture_hd")
            compatibility = reanimated_hd.match_assets(pack, stage / "gamedata/assets")
            if not compatibility["mapped_texture_count"]:
                raise ValueError("No HD keys match this donor; refusing to embed an unusable pack")
            matched = {entry["key"] + ".ktx" for entry in compatibility["mappings"]}
            selected = []
            for entry in pack["files"]:
                if entry["name"] in matched:
                    selected.append(entry)
                else:
                    (stage / "texture_hd" / entry["name"]).unlink()
            selected_bytes = sum((stage / "texture_hd" / entry["name"]).stat().st_size for entry in selected)
            selected_pack = {**pack, "files": selected, "file_count": len(selected),
                             "total_bytes": selected_bytes, "selection": "exact donor texture keys",
                             "source_file_count": pack["file_count"], "source_total_bytes": pack["total_bytes"],
                             "encoding_counts": dict(Counter(entry["encoding"] for entry in selected))}
            (stage / "texture_hd/manifest.json").write_text(json.dumps(selected_pack, indent=2) + "\n")
            (stage / "texture_hd/donor-compatibility.json").write_text(json.dumps(compatibility, indent=2) + "\n")
            hd_summary = {"directory": "texture_hd", "apk_sha256": pack["apk_sha256"],
                          "file_count": len(selected), "total_bytes": selected_bytes,
                          "source_file_count": pack["file_count"],
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
    parser.add_argument("--settings", type=Path, help="Game-generated settings.sb; seed a new save with Simplified Chinese")
    args = parser.parse_args()
    report = prepare(args.source, args.output, args.hd_apk,args.settings)
    print(f"Staged {report['file_count']} files, {report['total_bytes']} bytes. Not a playable build.")

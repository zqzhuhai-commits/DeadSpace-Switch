#!/usr/bin/env python3
"""Stage user-supplied game files for an embedded RomFS; never publish assets."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from prepare_data import import_data


def prepare(source, output):
    source, output = Path(source).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    if source.is_dir() and (output == source or source in output.parents):
        raise ValueError("Output must be outside source")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deadspacemobile-romfs-", dir=output.parent))
    try:
        import_data(source, stage / "gamedata")
        entries = []
        for path in sorted((stage / "gamedata").rglob("*")):
            if path.is_file():
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                entries.append({"path": path.relative_to(stage).as_posix(), "size": path.stat().st_size, "sha256": digest})
        manifest = {"format": 1, "title": "Dead Space Mobile", "files": entries,
                    "file_count": len(entries), "total_bytes": sum(e["size"] for e in entries),
                    "campaign_verified": False}
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
    args = parser.parse_args()
    report = prepare(args.source, args.output)
    print(f"Staged {report['file_count']} files, {report['total_bytes']} bytes. Not a playable build.")

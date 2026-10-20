#!/usr/bin/env python3
"""Exercise readback against devkitPro's independent RomFS writer."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("verify_romfs", Path(__file__).parents[1] / "tools/verify_romfs.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as folder:
    work = Path(folder)
    root = work / "root"
    (root / "gamedata/assets/nested").mkdir(parents=True)
    fixtures = {"gamedata/assets/EAMCore.ini": b"independent readback fixture\n", "gamedata/assets/nested/test.bin": bytes(range(256)) * 41}
    items = []
    for name, data in fixtures.items():
        (root / name).write_bytes(data)
        items.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    (root / "manifest.json").write_text(json.dumps({"files": items, "file_count": len(items), "total_bytes": sum(len(v) for v in fixtures.values())}))
    image = work / "test.romfs"
    subprocess.run([sys.argv[1], str(root), str(image)], check=True, capture_output=True)
    assert module.verify(image)["verified_files"] == 2
    raw = image.read_bytes()
    damaged = bytearray(raw)
    damaged[raw.index(fixtures["gamedata/assets/EAMCore.ini"])] ^= 1
    for label, content in [("changed content", damaged), ("truncated image", raw[:-24])]:
        image.write_bytes(content)
        try:
            module.verify(image)
        except (ValueError, KeyError):
            pass
        else:
            raise AssertionError("Accepted " + label)
    print("PASS: independent RomFS readback; rejects changed content and truncated metadata")

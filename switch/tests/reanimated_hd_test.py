#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location("reanimated_hd", Path(__file__).parents[1] / "tools/reanimated_hd.py")
hd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hd)


def reject(fn):
    try:
        fn()
    except (ValueError, struct.error):
        return
    raise AssertionError("Malformed texture/archive accepted")


header = hd.KTX_MAGIC + struct.pack("<13I", 0x04030201, 0, 1, 0, 0x8D64, 0x1908, 8, 8, 0, 0, 1, 4, 0)
body = b"".join(struct.pack("<I", n) + bytes(n) for n in [32, 8, 8, 8])
texture = header + body
assert hd.ktx_info(texture)["mip_levels"] == 4
reject(lambda: hd.ktx_info(texture[:-1]))
reject(lambda: hd.ktx_info(texture + b"unexpected"))
bad = bytearray(texture)
struct.pack_into("<I", bad, 64, 0xFFFFFFFF)
reject(lambda: hd.ktx_info(bad))
bad = bytearray(texture)
struct.pack_into("<I", bad, 40, 8192)  # height
reject(lambda: hd.ktx_info(bad))
# Independent native implementation of the observed hash produced this vector.
assert hd.texture_key(2, 2, 0x1908, bytes(range(16))) == "8b955b6c3c9b6d9a"
obj = (struct.pack("<III", 9, 0, 1) + struct.pack("<II", 77, 3) + b"abc" +
       struct.pack("<BBII", 100, 0, 2, 2) + struct.pack("<II", 0, 16) + bytes(range(16)))
objects = struct.pack("<BI", 10, len(obj)) + obj
m3g = b"\xabJSR184\xbb\r\n\x1a\n" + struct.pack("<BII", 0, len(objects)+13, len(objects)) + objects + bytes(4)
image = list(hd.images(m3g))
assert len(image) == 1 and image[0]["key"] == "8b955b6c3c9b6d9a"
reject(lambda: list(hd.images(m3g[:-1])))
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    fake = root / "wrong.apk"
    fake.write_bytes(b"wrong donor")
    output = root / "pack"
    reject(lambda: hd.extract(fake, output))
    assert not output.exists() and not list(root.glob(".reanimated-hd-*"))
    output.mkdir()
    (output / "keep").write_text("unchanged")
    reject(lambda: hd.extract(fake, output))
    assert (output / "keep").read_text() == "unchanged"
print("PASS: KTX mip/range checks, native hash vector, M3G user parameters, failed extraction cleanup and overwrite refusal")

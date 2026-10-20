#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_data", ROOT / "tools/prepare_data.py")
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)


def run():
    with tempfile.TemporaryDirectory() as t:
        work = Path(t)
        exe = work / "core-test"
        subprocess.run(["g++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-I" + str(ROOT / "include"),
                        "-I" + str(ROOT.parent / "third_party/powervr"),
                        str(ROOT / "source/preflight.cpp"), str(ROOT / "tests/core_test.cpp"),
                        str(ROOT.parent / "third_party/powervr/PVRTDecompress.cpp"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        # The real donor SHA1 gate is never relaxed. Synthetic archives must fail.
        for name, contents in [
            ("vpk", {"eboot.bin": b"not a game library"}),
            ("traversal", {"../escape": b"x"}),
            ("wrong-donor", {data.LIB: b"x" * 80}),
        ]:
            source = work / (name + ".zip")
            with zipfile.ZipFile(source, "w") as z:
                for p, b in contents.items():
                    z.writestr(p, b)
            output = work / (name + "-output")
            try:
                data.import_data(source, output)
            except ValueError:
                assert not output.exists(), "failed import published incomplete data"
            else:
                raise AssertionError(name + " incorrectly accepted")
        assert not list(work.glob(".deadspace-stage-*")), "staging tree leaked"
        print("PASS: rejects standalone VPK, unsafe ZIP and wrong donor; no partial destination")


if __name__ == "__main__":
    run()

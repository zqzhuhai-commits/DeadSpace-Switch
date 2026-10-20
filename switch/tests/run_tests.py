#!/usr/bin/env python3
import importlib.util
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_data", ROOT / "tools/prepare_data.py")
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)


def run(native_gl=False):
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
        jni_exe = work / "jni-test"
        subprocess.run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "runtime"), str(ROOT / "runtime/guest_jni.cpp"),
                        str(ROOT / "tests/guest_jni_test.cpp"), "-o", str(jni_exe)], check=True)
        subprocess.run([str(jni_exe)], check=True)
        memory_exe = work / "memory-test"
        subprocess.run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "runtime"), str(ROOT / "tests/guest_memory_test.cpp"),
                        "-o", str(memory_exe)], check=True)
        subprocess.run([str(memory_exe)], check=True)
        hd_exe = work / "hd-test"
        subprocess.run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "runtime"), str(ROOT / "runtime/hd_texture.cpp"),
                        str(ROOT.parent / "src/atc_decompress.cpp"), str(ROOT.parent / "third_party/powervr/PVRTDecompress.cpp"),
                        str(ROOT / "tests/hd_texture_test.cpp"), "-o", str(hd_exe)], check=True)
        subprocess.run([str(hd_exe)], check=True)
        if native_gl:
            gl_exe = work / "native-gl-test"
            subprocess.run(["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I" + str(ROOT / "runtime"), str(ROOT / "runtime/guest_gl.cpp"), str(ROOT / "runtime/hd_texture.cpp"),
                            str(ROOT.parent / "third_party/powervr/PVRTDecompress.cpp"),
                            str(ROOT.parent / "src/atc_decompress.cpp"), str(ROOT / "tests/guest_gl_native_test.cpp"),
                            "-ldl", "-o", str(gl_exe)], check=True)
            subprocess.run([str(gl_exe)], check=True)
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-gl", action="store_true", help="Also require native EGL/desktop compatibility GL readback")
    run(parser.parse_args().native_gl)

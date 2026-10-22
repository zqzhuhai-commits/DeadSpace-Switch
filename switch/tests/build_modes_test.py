#!/usr/bin/env python3
"""Exercise Make's incremental configuration transitions without a cross SDK."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    project = root/'switch'
    shutil.copytree(ROOT, project, ignore=shutil.ignore_patterns('build', '__pycache__'))
    power = root/'third_party/powervr'
    power.mkdir(parents=True)
    (power/'PVRTDecompress.cpp').touch()
    binary = root/'sdk/devkitA64/bin'
    binary.mkdir(parents=True)
    tools = root/'sdk/tools/bin'
    tools.mkdir(parents=True)
    compiler = binary/'aarch64-none-elf-g++'
    compiler.write_text('''#!/usr/bin/env python3
import json, pathlib, sys
args=sys.argv[1:]
out=pathlib.Path(args[args.index('-o')+1])
out.write_text(json.dumps(args))
with open('build/commands','a') as f:f.write(str(out)+'\\n')
''')
    compiler.chmod(0o755)
    for name, index in [('nacptool', 5), ('elf2nro', 2), ('elf2nso', 2)]:
        tool = tools/name
        tool.write_text(f'#!/usr/bin/env python3\nimport pathlib,sys\npathlib.Path(sys.argv[{index}]).touch()\n')
        tool.chmod(0o755)
    for path in ['libds_cpu_probe.a', 'dynarmic/src/dynarmic/libdynarmic.a',
                 'dynarmic/externals/fmt/libfmt.a', 'dynarmic/externals/mcl/src/libmcl.a']:
        target = project/'build/cpu'/path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.touch()
    env = dict(os.environ, DEVKITPRO=str(root/'sdk'))
    def make(*options):
        subprocess.run(['make', '-C', str(project), '--no-print-directory', *options], env=env,
                       check=True, stdout=subprocess.DEVNULL)
    def commands():
        return (project/'build/commands').read_text().splitlines()
    make()
    first = len(commands())
    make()
    assert len(commands()) == first, 'unchanged build recompiled objects'
    make('CPU_PROBE=1')
    assert '-DDS_CPU_PROBE' in (project/'build/main.o').read_text(), 'CPU mode reused platform objects'
    make('CPU_PROBE=1', 'STANDALONE=1', 'build/deadspace-switch.nso')
    assert 'standalone.o' in (project/'build/deadspace-switch.elf').read_text(), 'standalone linked diagnostics entry'
    make()
    assert '-DDS_CPU_PROBE' not in (project/'build/main.o').read_text(), 'platform mode reused CPU objects'
    make('CPU_PROBE=1')
    first = len(commands())
    os.utime(project/'build/cpu/libds_cpu_probe.a', None)
    make('CPU_PROBE=1')
    assert len(commands()) == first + 1 and commands()[-1].endswith('.elf'), 'CPU archive update failed to relink'
print('PASS: incremental build reuse, platform/CPU/standalone transitions and runtime archive relink')

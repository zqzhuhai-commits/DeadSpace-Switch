#!/usr/bin/env python3
"""Build a local, standalone development NSP. Never uploads assets or keys."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def validate_npdm(path):
    config = json.loads(Path(path).read_text())
    if not isinstance(config, dict):
        raise ValueError('NPDM JSON must be an object')
    title = config.get('program_id', config.get('title_id'))
    if not isinstance(title, str) or not re.fullmatch(r'(?:0x)?[0-9a-fA-F]{16}', title):
        raise ValueError('NPDM program_id/title_id must be a 16-digit hexadecimal string')
    title = int(title, 16)
    if not 0x0100000000000000 <= title < 0x0200000000000000 or title & 0xfff:
        raise ValueError('Choose an unused base application program ID ending in 000')
    if config.get('name') != 'deadspacemobile' or config.get('is_64_bit') is not True:
        raise ValueError('NPDM must name the 64-bit application deadspacemobile')
    for field, legacy in [('program_id_range_min', 'title_id_range_min'),
                          ('program_id_range_max', 'title_id_range_max')]:
        value = config.get(field, config.get(legacy))
        if not isinstance(value, str) or int(value, 16) != title:
            raise ValueError('NPDM ID range must exactly match its application ID')
    return f'{title:016x}'


def verify_nsp(path):
    """Structural PFS0 check, not NCA decryption or install/playability validation."""
    with Path(path).open('rb') as f:
        length = f.seek(0, 2)
        f.seek(0)
        header = f.read(16)
        if len(header) != 16:
            raise ValueError('Truncated NSP')
        magic, count, string_size, reserved = struct.unpack('<4sIII', header)
        if magic != b'PFS0' or reserved or not 3 <= count <= 32 or string_size > 65536:
            raise ValueError('Invalid NSP PFS0 header or missing program/control/meta content')
        table = f.read(count * 24)
        names = f.read(string_size)
        if len(table) != count * 24 or len(names) != string_size:
            raise ValueError('Truncated NSP file table')
        data_start = 16 + count * 24 + string_size
        seen, ranges = set(), []
        for i in range(count):
            offset, size, name_offset, padding = struct.unpack_from('<QQII', table, i * 24)
            if padding or name_offset >= len(names):
                raise ValueError('Invalid NSP entry')
            end = names.find(b'\0', name_offset)
            if end < 0:
                raise ValueError('Unterminated NSP name')
            name = names[name_offset:end].decode('ascii')
            if not re.fullmatch(r'[0-9a-fA-F]{32}(?:\.cnmt)?\.nca', name) or name in seen:
                raise ValueError('Unexpected or duplicate NSP content')
            if not size or offset + size > length - data_start:
                raise ValueError('NSP content outside archive')
            seen.add(name)
            ranges.append((offset, offset + size))
        ranges.sort()
        if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
            raise ValueError('Overlapping NSP content')


def build(args):
    output = args.output.resolve()
    if output.suffix.lower() != '.nsp':
        raise ValueError('Output filename must end in .nsp')
    if output.exists():
        raise ValueError('Output already exists; choose a new NSP filename')
    title = validate_npdm(args.npdm_json)
    keyset = args.keyset.resolve(strict=True)
    if not keyset.is_file():
        raise ValueError('Keyset must be a local file')
    devkit = Path(os.environ['DEVKITPRO'])
    os.environ['PATH'] = str(devkit/'tools/bin') + os.pathsep + os.environ['PATH']
    packer = shutil.which(args.hacbrewpack)
    for tool in ['elf2nso', 'npdmtool', 'nacptool', 'make']:
        if not shutil.which(tool):
            raise ValueError(f'Missing build tool: {tool}')
    if not packer:
        raise ValueError('hacbrewpack executable not found')
    cpu_build = args.cpu_build.resolve(strict=True)
    if not (cpu_build/'libds_cpu_probe.a').is_file():
        raise ValueError('Build the Switch CPU runtime first with build_cpu_probe.sh')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.deadspacemobile-package-', dir=output.parent) as temp:
        stage = Path(temp)
        romfs = stage/'romfs'
        command = ['python3', str(ROOT/'switch/tools/prepare_romfs.py'), str(args.data.resolve()), str(romfs)]
        if args.hd_apk:
            command += ['--hd-apk', str(args.hd_apk.resolve())]
        subprocess.run(command, check=True)
        subprocess.run(['make', '-C', str(ROOT/'switch'), '-j2', 'CPU_PROBE=1', 'STANDALONE=1',
                        f'CPU_BUILD_DIR={cpu_build}', f'ROMFS_ROOT={romfs}', f'TITLE_ID={title}',
                        'all', 'build/deadspace-switch.nso'], check=True)
        subprocess.run(['python3', str(ROOT/'switch/tools/verify_romfs.py'),
                        str(ROOT/'switch/build/deadspace-switch.nro')], check=True)
        exefs, control = stage/'exefs', stage/'control'
        exefs.mkdir(); control.mkdir()
        shutil.copyfile(ROOT/'switch/build/deadspace-switch.nso', exefs/'main')
        subprocess.run(['npdmtool', str(args.npdm_json.resolve()), str(exefs/'main.npdm')], check=True)
        shutil.copyfile(ROOT/'switch/build/deadspace-switch.nacp', control/'control.nacp')
        shutil.copyfile(ROOT/'switch/assets/icon.jpg', control/'icon_AmericanEnglish.dat')
        # All hacBrewPack temporary directories start empty and stay private.
        stage.chmod(0o700)
        subprocess.run([packer, '--keyset', str(keyset), '--exefsdir', str(exefs),
                        '--romfsdir', str(romfs), '--controldir', str(control), '--nologo',
                        '--tempdir', str(stage/'tmp'), '--ncadir', str(stage/'nca'),
                        '--nspdir', str(stage/'nsp')], cwd=stage, check=True)
        packages = list((stage/'nsp').glob('*.nsp'))
        if len(packages) != 1:
            raise ValueError('Expected exactly one generated NSP')
        verify_nsp(packages[0])
        # Atomic no-overwrite publish on the same filesystem as the staging dir.
        os.link(packages[0], output)
    print(f'Created {output}; installation and gameplay remain unverified.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True, help='Supported original donor data directory/archive')
    parser.add_argument('--hd-apk', type=Path)
    parser.add_argument('--cpu-build', type=Path, required=True)
    parser.add_argument('--npdm-json', type=Path, required=True, help='Reviewed standalone application permissions including libnx JIT access')
    parser.add_argument('--keyset', type=Path, required=True)
    parser.add_argument('--hacbrewpack', default='hacbrewpack')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'NSP build failed: {error}\n')


if __name__ == '__main__':
    main()

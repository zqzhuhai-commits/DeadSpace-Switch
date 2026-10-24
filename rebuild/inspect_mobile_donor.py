#!/usr/bin/env python3
"""Read-only ZIP inventory for the clean mobile port. Never executes donor code.

Usage: python inspect_mobile_donor.py donor.zip --output inventory.json
No game bytes, dialogue or font data are included in the report.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile

EXPECTED = '0ed42b611415015807f759ec9b5457857143ce39'
LIB = 'lib/armeabi/libEAMGameDeadSpace.so'


def inspect(path):
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        seen = set()
        for e in entries:
            p = PurePosixPath(e.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in e.filename or ':' in e.filename:
                raise ValueError('Unsafe ZIP path')
            if e.filename in seen:
                raise ValueError('Duplicate ZIP path')
            seen.add(e.filename)
        libs = [e for e in entries if e.filename == LIB or e.filename.endswith('/' + LIB)]
        if len(libs) != 1:
            raise ValueError('Expected exactly one donor library')
        lib = libs[0]
        if not 20 <= lib.file_size <= 64 * 1024**2:
            raise ValueError('Invalid library size')
        raw = z.read(lib)
        sha1 = hashlib.sha1(raw).hexdigest()
        if sha1 != EXPECTED or raw[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', raw, 16) != (3, 40):
            raise ValueError('Unsupported donor: expected Xperia Play 1.1.33 ELF32 ARM')
        prefix = lib.filename[:-len(LIB)]
        assets = [e for e in entries if e.filename.startswith(prefix + 'assets/') and not e.is_dir()]
        names = {e.filename[len(prefix):] for e in assets}
        for required in ['assets/EAMCore.ini', 'assets/published/data/achievements.sb']:
            if required not in names:
                raise ValueError('Missing sentinel: ' + required)
        total = sum(e.file_size for e in assets)
        if total > 2 * 1024**3:
            raise ValueError('Assets exceed 2 GiB audit bound')
        groups = Counter()
        for e in assets:
            groups[PurePosixPath(e.filename).suffix.lower()] += e.file_size
        candidates = sorted(n for n in names if any(t in n.lower() for t in ['zh_', 'subtitle', '/fonts/', 'locale', 'language']))
        return {
            'format': 1, 'library_sha1': sha1, 'library_bytes': len(raw),
            'edition': 'Dead Space Mobile / Xperia Play v1.1.33 compatible donor',
            'asset_files': len(assets), 'asset_bytes': total,
            'asset_bytes_by_extension': dict(sorted(groups.items())),
            'localization_candidates': candidates,
            'campaign_chinese_verified': False,
            'localization_note': 'downloadcontent/zh_CN.txt is downloader UI; fonts and subtitles require format and glyph checks.',
            'game_executed': False, 'switch_tested': False,
            'completeness': 'Required sentinels only; chapter completeness unverified',
            'redistribution': 'No asset redistribution permission established by this audit',
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('donor', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.donor)
    # Exclusive create prevents accidental replacement of an earlier evidence file.
    with args.output.open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
        f.write('\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Negative packaging fixtures never need keys or copyrighted game content."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location('package_nsp', Path(__file__).resolve().parents[1]/'tools/package_nsp.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def archive(entries):
    names = b''
    table = b''
    for offset, size, name in entries:
        table += struct.pack('<QQII', offset, size, len(names), 0)
        names += name.encode() + b'\0'
    return struct.pack('<4sIII', b'PFS0', len(entries), len(names), 0) + table + names + b'x' * 48


with tempfile.TemporaryDirectory() as temp:
    config = Path(temp)/'application.json'
    fields = dict(name='deadspacemobile', is_64_bit=True, program_id='0100123456789000',
                  program_id_range_min='0100123456789000', program_id_range_max='0100123456789000')
    config.write_text(json.dumps(fields))
    assert pack.validate_npdm(config) == '0100123456789000'
    for key, value in [('program_id', '0100123456789001'), ('program_id_range_max', '010012345678a000'),
                       ('name', 'other-game'), ('is_64_bit', False)]:
        config.write_text(json.dumps(dict(fields, **{key: value})))
        try:
            pack.validate_npdm(config)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted inconsistent NPDM ' + key)
    path = Path(temp)/'test.nsp'
    entries = [(i*16, 16, str(i)*32+'.nca') for i in range(3)]
    path.write_bytes(archive(entries))
    pack.verify_nsp(path)
    for bad in [b'', archive(entries)[:-1], archive(entries[:1]),
                archive([(0, 16, '0'*32+'.nca')]*3),
                archive([(0, 16, '../file.nca')]+entries[1:]),
                archive([(0, 32, '0'*32+'.nca')]+entries[1:])]:
        path.write_bytes(bad)
        try:
            pack.verify_nsp(path)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted malformed NSP')
print('PASS: NPDM identity/ID consistency; NSP truncation, bounds, duplicates, names and overlap rejection')

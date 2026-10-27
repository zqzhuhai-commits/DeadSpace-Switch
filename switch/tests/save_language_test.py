import importlib.util
from pathlib import Path
import struct

spec=importlib.util.spec_from_file_location('language',Path(__file__).parents[1]/'tools/set_save_language.py')
language=importlib.util.module_from_spec(spec);spec.loader.exec_module(language)
assert language.fnv1(b'')==0x811c9dc5
assert language.fnv1(struct.pack('<I',33))==0xe985ab2a
strings=language.FIELDS+['en-us']
table=struct.pack('<I',len(strings))+b''.join(struct.pack('<I',len(s))+s.encode() for s in strings)
data=bytes.fromhex('000004000c008b00')+bytes(136)
def section(tag,payload):
    raw=tag+struct.pack('<II',len(payload),language.fnv1(payload))+payload
    return raw+bytes((-len(raw))%4)
raw=b'SBIN\x02\0\0\0'+b''.join(section(tag,payload) for tag,payload in [
    (b'STRU',b''),(b'FIEL',b''),(b'ENUM',b''),(b'OHDR',struct.pack('<I',33)),
    (b'DATA',data),(b'STRS',table),(b'SYMB',bytes(4))])
converted=language.localize(raw)
assert converted!=raw and len(converted)==len(raw)
assert language.localize(converted,'en-us')==raw
assert language.localize(converted)==converted
for invalid in (b'bad',raw[:-1],raw.replace(b'en-us',b'zz-zz'),raw.replace(b'language',b'Language')):
    try:language.localize(invalid)
    except ValueError:pass
    else:raise AssertionError('Malformed settings accepted')
print('PASS: independent FNV-1 vectors, locale round trip, checksums and settings schema rejection')

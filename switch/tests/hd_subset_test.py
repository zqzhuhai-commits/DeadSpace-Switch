#!/usr/bin/env python3
import importlib.util,struct
from pathlib import Path
spec=importlib.util.spec_from_file_location('subset',Path(__file__).resolve().parents[1]/'tools/prepare_hd_subset.py')
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
raw=b'\xabKTX 11\xbb\r\n\x1a\n'+struct.pack('<13I',0x04030201,0,1,0,0x8d64,0x1908,64,64,0,0,1,7,0)
for level in range(7):
    w=max(1,64>>level);n=((w+3)//4)**2*8
    raw+=struct.pack('<I',n)+bytes([level])*n
trimmed,w,h,drop=m.trim(raw,32)
assert (w,h,drop)==(32,32,1)
assert m.ktx_info(trimmed)['mip_levels']==6
assert trimmed[68:68+512]==bytes([1])*512
assert m.trim(raw,64)[0]==raw
try:m.trim(raw[:-1])
except ValueError:pass
else:raise AssertionError('Truncation accepted')
print('PASS: exact lower mip retention, dimensions/counts, no-op and truncated input rejection')

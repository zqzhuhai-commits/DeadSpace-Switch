#!/usr/bin/env python3
import sys
from pathlib import Path
import tempfile
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import chinese_resources as cr

with tempfile.TemporaryDirectory() as temp:
    work=Path(temp)
    game=work/'game'
    english=game/'assets/published/stringdata/en-us/deadspace.bin'
    english.parent.mkdir(parents=True)
    schema=b'SBIN\x02\0\0\0ENUM\0\0\0\0'
    english.write_bytes(schema+b'DATAenglish')
    font=game/'assets/published/fonts/default.ttf'
    font.parent.mkdir(parents=True)
    font.write_bytes(b'original-font')
    apk=work/'test.apk'
    def archive(database):
        with zipfile.ZipFile(apk,'w') as z:
            z.writestr(cr.PREFIX+cr.FILES[0],database)
            z.writestr(cr.PREFIX+cr.FILES[1],b'\0\1\0\0test-font')
            z.writestr('assets/game/libDeadSpace32.so',b'never import code')
    archive(schema+b'DATAchinese')
    report=cr.overlay(apk,game)
    assert report['locale']=='zh_CN' and len(report['files'])==4
    assert report['english_fallback_is_chinese']
    assert english.read_bytes()==schema+b'DATAchinese'
    assert font.read_bytes()==b'\0\1\0\0test-font'
    assert (game/'assets/published'/cr.FILES[0]).read_bytes()==schema+b'DATAchinese'
    assert not list(game.rglob('*.so'))
    archive(b'SBIN\x02\0\0\0DIFFERENTDATAinvalid')
    try: cr.overlay(apk,game)
    except ValueError: pass
    else: raise AssertionError('Incompatible schema accepted')
    assert (game/'assets/published'/cr.FILES[0]).read_bytes()==schema+b'DATAchinese'
print('PASS: Chinese resource schema gate, exact allowlist, no guest code replacement, rejection before write')

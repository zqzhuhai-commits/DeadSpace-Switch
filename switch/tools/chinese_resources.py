"""Copy Chinese strings/fonts from a user-supplied Reanimated APK, never its code."""
from pathlib import Path
import hashlib
import zipfile

PREFIX = 'assets/game/assets/published/'
FILES = ('stringdata/zh-cn/deadspace.bin', 'fonts/Ascender_Uni.ttf')


def schema(data):
    end = data.find(b'DATA')
    if not data.startswith(b'SBIN\x02\0\0\0') or not 8 <= end <= 65536:
        raise ValueError('Unsupported string database schema')
    return data[:end]


def overlay(apk, game_root):
    root = Path(game_root) / 'assets/published'
    reference = (root / 'stringdata/en-us/deadspace.bin').read_bytes()
    contents = {}
    with zipfile.ZipFile(apk) as archive:
        for name in FILES:
            entries = [entry for entry in archive.infolist() if entry.filename == PREFIX + name]
            if len(entries) != 1 or not 0 < entries[0].file_size <= 32 * 1024**2:
                raise ValueError('Missing, duplicate or oversized Chinese resource: ' + name)
            contents[name] = archive.read(entries[0])
    if schema(contents[FILES[0]]) != schema(reference):
        raise ValueError('Chinese database schema differs from the supported donor')
    if contents[FILES[1]][:4] not in (b'\x00\x01\x00\x00', b'OTTO', b'ttcf'):
        raise ValueError('Invalid Chinese font header')
    # The Xperia donor or an existing save may select en-us despite the
    # platform locale. A Chinese package uses the translated database for that
    # fallback too. Alias existing font filenames so English-selected styles
    # still have CJK glyphs; only font resources inside this staging tree change.
    contents['stringdata/en-us/deadspace.bin'] = contents[FILES[0]]
    for font in sorted((root / 'fonts').glob('*')):
        if font.is_file() and font.suffix.lower() == '.ttf':
            contents['fonts/' + font.name] = contents[FILES[1]]
    result = []
    for name, data in contents.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        result.append({'path': 'gamedata/assets/published/' + name,
                       'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    return {'locale': 'zh_CN', 'files': result, 'schema_matches_donor': True,
            'english_fallback_is_chinese': True,
            'translation_and_hardware_verified': False}

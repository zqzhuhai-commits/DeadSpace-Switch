#!/usr/bin/env python3
"""Copy a game-generated settings.sb, selecting a supported locale.

Only changes the validated locale entry in the settings string table. Never
changes a game library, translation table, checkpoint or original input file.
"""
import argparse
from pathlib import Path
import struct

FIELDS=['brightness','language','profile_index','subtitles_enabled','tutorials_enabled','auto_reload','alt_fire_toggle_mode','sfx_volume','music_volume','sensitivity','y_flipped','aim_toggle_insted_hold']
LOCALES=('en-us','de-de','fr-fr','es-es','it-it','ko-kr','zh-cn')

def fnv1(data):
    value=2166136261
    for byte in data:value=((value*16777619)^byte)&0xffffffff
    return value

def localize(raw,locale='zh-cn'):
    if locale not in LOCALES:
        raise ValueError('Unsupported locale')
    if raw[:8]!=b'SBIN\x02\x00\x00\x00' or len(raw)>16384:
        raise ValueError('Expected bounded SBIN v2 settings')
    sections={};offset=8
    while offset<len(raw):
        if offset+12>len(raw):raise ValueError('Truncated section')
        tag=raw[offset:offset+4];length=struct.unpack_from('<I',raw,offset+4)[0]
        if tag in sections or offset+12+length>len(raw):raise ValueError('Invalid section bounds')
        stored=struct.unpack_from('<I',raw,offset+8)[0]
        if stored!=fnv1(raw[offset+12:offset+12+length]):raise ValueError('Settings section checksum mismatch')
        sections[tag]=(offset+12,length);offset=(offset+12+length+3)&~3
    if offset!=len(raw) or set(sections)!={b'STRU',b'FIEL',b'ENUM',b'OHDR',b'DATA',b'STRS',b'SYMB'}:
        raise ValueError('Unexpected settings section layout')
    if any(sections[tag][1] for tag in (b'STRU',b'FIEL',b'ENUM')) or sections[b'DATA'][1]!=144 or sections[b'OHDR'][1]!=4:
        raise ValueError('Unsupported settings object layout')
    object_start=sections[b'OHDR'][0];data_start=sections[b'DATA'][0]
    if struct.unpack_from('<I',raw,object_start)[0]!=33 or raw[data_start:data_start+8]!=bytes.fromhex('000004000c008b00'):
        raise ValueError('Unsupported settings object schema')
    start,length=sections[b'STRS'];table=raw[start:start+length]
    if length<4:raise ValueError('Empty string table')
    count=struct.unpack_from('<I',table)[0]
    if count!=13:raise ValueError('Unexpected settings field count')
    cursor=4;strings=[];ranges=[]
    for _ in range(count):
        if cursor+4>length:raise ValueError('Truncated string length')
        size=struct.unpack_from('<I',table,cursor)[0];cursor+=4
        if cursor+size>length:raise ValueError('Truncated string')
        strings.append(table[cursor:cursor+size].decode('utf-8'));ranges.append((cursor,size));cursor+=size
    if cursor!=length or strings[:-1]!=FIELDS or strings[-1] not in LOCALES:
        raise ValueError('Settings language schema not recognized')
    position,size=ranges[-1]
    if size!=len(locale):raise ValueError('Locale length changes are not supported')
    result=bytearray(raw);result[start+position:start+position+size]=locale.encode('ascii')
    struct.pack_into('<I',result,start-4,fnv1(result[start:start+length]))
    return bytes(result)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path,help='New file; existing files are never replaced')
    parser.add_argument('--locale',choices=LOCALES,default='zh-cn')
    args=parser.parse_args()
    result=localize(args.source.read_bytes(),args.locale)
    with args.output.open('xb') as file:file.write(result)

if __name__=='__main__':main()

#!/usr/bin/env python3
"""Independently read/decrypt/check our homebrew NSP; never emit key material."""
import argparse,hashlib,json,struct,tempfile
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
from verify_romfs import verify as verify_romfs

def sha(b):return hashlib.sha256(b).digest()
def cut(b,o,n):
    if o<0 or n<0 or o+n>len(b):raise ValueError('Out-of-bounds package field')
    return b[o:o+n]
def pfs(b):
    if b[:4]!=b'PFS0':raise ValueError('Not PFS0')
    count,strings=struct.unpack_from('<II',b,4)
    if count>10000:raise ValueError('PFS0 count')
    names=cut(b,16+24*count,strings);start=16+24*count+strings;files={}
    for i in range(count):
        off,size,name,_=struct.unpack_from('<QQII',b,16+24*i)
        if name>=len(names):raise ValueError('PFS0 name')
        end=names.find(b'\0',name)
        if end<0:raise ValueError('PFS0 name termination')
        key=names[name:end].decode()
        if not key or key in files or '/' in key or '\\' in key:raise ValueError('PFS0 path')
        files[key]=cut(b,start+off,size)
    return files
def romfs_files(b):
    h=struct.unpack_from('<10Q',b);assert h[0]==80
    dirs=cut(b,h[3],h[4]);files=cut(b,h[7],h[8]);found={};seen=set()
    def directory(off,parent):
        if off in seen:raise ValueError('RomFS cycle')
        seen.add(off);d=struct.unpack_from('<6I',dirs,off)
        name=cut(dirs,off+24,d[5]).decode();prefix=parent+name+'/' if name else parent
        child=d[3]
        while child!=0xffffffff:
            v=struct.unpack_from('<IIQQII',files,child);name=cut(files,child+32,v[5]).decode()
            if '/' in name or '\\' in name or prefix+name in found:raise ValueError('RomFS path')
            found[prefix+name]=cut(b,h[9]+v[2],v[3]);child=v[1]
        child=d[2]
        while child!=0xffffffff:
            directory(child,prefix);child=struct.unpack_from('<I',dirs,child+4)[0]
    directory(0,'');return found
def decrypt(key,mode,b):
    d=Cipher(algorithms.AES(key),mode).decryptor();return d.update(b)+d.finalize()
def verify(package,keyfile,stage):
    keys={}
    for line in Path(keyfile).read_text(encoding='utf-8-sig').splitlines():
        if '=' in line:
            k,v=line.split('=',1)
            if k.strip() in ('header_key','key_area_key_application_00'):keys[k.strip()]=bytes.fromhex(v.strip())
    if set(keys)!=set(('header_key','key_area_key_application_00')):raise ValueError('Required key names missing')
    contents=pfs(Path(package).read_bytes());records=[];decoded={};program_romfs=None;control=None;exe=None;cnmt=None
    for name,b in contents.items():
        digest=sha(b)
        if name.split('.')[0]!=digest[:16].hex():raise ValueError('NCA filename hash mismatch')
        head=b''.join(decrypt(keys['header_key'],modes.XTS(i.to_bytes(16,'big')),cut(b,i*512,512)) for i in range(6))
        if head[0x200:0x204]!=b'NCA3':raise ValueError('NCA header decrypt failed')
        size,tid=struct.unpack_from('<QQ',head,0x208)
        if size!=len(b) or tid!=int('0100d5ace0040000',16):raise ValueError('NCA size/ID')
        if head[0x230:0x240]!=bytes(16):raise ValueError('Unexpected rights-managed content')
        content_type=head[0x205]
        if max(head[0x206],head[0x220])>1:raise ValueError('Unexpected key generation')
        area=decrypt(keys['key_area_key_application_00'],modes.ECB(),head[0x300:0x340]);sections=[]
        for index in range(4):
            start,end=struct.unpack_from('<II',head,0x240+index*16)
            if not start and not end:continue
            fs=head[0x400+index*512:0x600+index*512]
            if sha(fs)!=head[0x280+index*32:0x2a0+index*32]:raise ValueError('FS header hash')
            raw=cut(b,start*512,(end-start)*512)
            if fs[4]==3:
                counter=fs[0x140:0x148][::-1]+(start*512//16).to_bytes(8,'big')
                raw=decrypt(area[32:48],modes.CTR(counter),raw)
            elif fs[4]!=1:raise ValueError('Unsupported content encryption')
            if fs[3]==2:
                block=struct.unpack_from('<I',fs,0x28)[0]
                ho,hs,po,ps=struct.unpack_from('<QQQQ',fs,0x30)
                table=cut(raw,ho,hs);data=cut(raw,po,ps)
                if sha(table)!=fs[8:40]:raise ValueError('PFS0 master hash')
                for i in range(0,len(data),block):
                    if sha(data[i:i+block])!=cut(table,(i//block)*32,32):raise ValueError('PFS0 block hash')
                extracted=pfs(data)
                if content_type==1:
                    if len(extracted)!=1:raise ValueError('CNMT count')
                    cnmt=next(iter(extracted.values()))
                elif content_type==0:exe=extracted
                sections.append({'index':index,'format':'PFS0','files':list(extracted)})
            elif fs[3]==3:
                if fs[8:12]!=b'IVFC':raise ValueError('IVFC header')
                levels=[]
                for i in range(6):
                    offset,length,power,_=struct.unpack_from('<QQII',fs,0x18+i*24)
                    if power!=14:raise ValueError('Unexpected IVFC block size')
                    levels.append(cut(raw,offset,length))
                if sha(levels[0])!=fs[0xc8:0xe8]:raise ValueError('IVFC master hash')
                for i in range(1,6):
                    child=levels[i];parent=levels[i-1]
                    for j in range(0,len(child),16384):
                        if sha(child[j:j+16384].ljust(16384,b'\0'))!=cut(parent,(j//16384)*32,32):raise ValueError('IVFC level hash: level=%d block=%d bytes=%d'%(i,j//16384,len(child)))
                if content_type==0:program_romfs=levels[-1]
                elif content_type==2:control=romfs_files(levels[-1])
                sections.append({'index':index,'format':'RomFS','ivfc_levels_verified':6})
            else:raise ValueError('Unsupported hash type')
        decoded[digest[:16]]=b
        records.append({'file':name,'bytes':len(b),'sha256':digest.hex(),'content_type':content_type,'sections':sections})
    if len(records)!=3 or not exe or not control or program_romfs is None or cnmt is None:raise ValueError('Missing application content')
    extended,count=struct.unpack_from('<HH',cnmt,14)
    if count!=2 or struct.unpack_from('<Q',cnmt)[0]!=int('0100d5ace0040000',16):raise ValueError('CNMT application metadata')
    for i in range(count):
        record=cnmt[32+extended+i*56:32+extended+(i+1)*56];b=decoded[record[32:48]]
        if sha(b)!=record[:32] or len(b)!=int.from_bytes(record[48:54],'little'):raise ValueError('CNMT content record')
    if sha(cnmt[:-32])!=cnmt[-32:]:raise ValueError('CNMT digest')
    for name,b in exe.items():
        if b!=(Path(stage)/'exefs'/name).read_bytes():raise ValueError('ExeFS differs from built application')
    for name,b in control.items():
        if b!=(Path(stage)/'control'/name).read_bytes():raise ValueError('Control differs from application metadata')
    if set(exe)!=set(('main','main.npdm')) or exe['main'][:4]!=b'NSO0':raise ValueError('Not standalone application')
    with tempfile.TemporaryDirectory(prefix='nsp-verify-') as tmp:
        rom=Path(tmp)/'romfs.bin';rom.write_bytes(program_romfs);resources=verify_romfs(rom)
    return {'package':Path(package).name,'bytes':Path(package).stat().st_size,'sha256':sha(Path(package).read_bytes()).hex(),
        'title_id':'0100d5ace0040000','ncas':records,'resource_verification':resources,'standalone_application':True,
        'encryption_readback_verified':True,'content_hashes_verified':True,'forwarder':False,'keys_included':False,
        'switch_install_tested':False,'switch_hardware_tested':False}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package');p.add_argument('--keys',required=True);p.add_argument('--stage',required=True);p.add_argument('--report',required=True)
    a=p.parse_args();r=verify(a.package,a.keys,a.stage);Path(a.report).write_text(json.dumps(r,indent=2),encoding='utf-8');print(json.dumps(r,indent=2))

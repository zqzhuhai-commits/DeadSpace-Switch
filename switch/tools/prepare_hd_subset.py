#!/usr/bin/env python3
"""Select exact donor HD matches and existing <=2048 mip levels for local use."""
import argparse,hashlib,json,struct
from pathlib import Path
from reanimated_hd import ktx_info

def trim(raw,maximum=2048):
    info=ktx_info(raw)
    w,h=info['width'],info['height']
    skip=0
    while max(w,h)>maximum:w=max(1,w//2);h=max(1,h//2);skip+=1
    cursor=64+struct.unpack_from('<I',raw,60)[0]
    for _ in range(skip):
        n=struct.unpack_from('<I',raw,cursor)[0];cursor+=4+((n+3)&~3)
    header=bytearray(raw[:64+struct.unpack_from('<I',raw,60)[0]])
    struct.pack_into('<I',header,36,w);struct.pack_into('<I',header,40,h)
    struct.pack_into('<I',header,56,info['mip_levels']-skip)
    result=bytes(header)+raw[cursor:];ktx_info(result)
    return result,w,h,skip

def prepare(audit,source,output):
    report=json.loads(Path(audit).read_text(encoding='utf-8'))
    output=Path(output)
    if output.exists():raise ValueError('Output already exists')
    matches={m['key']:m for m in report['donor_compatibility']['mappings']}
    files=[];skipped=[]
    for f in report['files']:
        key=Path(f['name']).stem
        if key not in matches:continue
        raw=(Path(source)/f['name']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=f['sha256']:raise ValueError('Source HD digest differs')
        data,w,h,drop=trim(raw)
        bw,bh=matches[key]['base_dimensions']
        if w<=bw or h<=bh or w%bw or h%bh or w//bw!=h//bh:
            skipped.append({'key':key,'reason':'2048 cap leaves no compatible upscale'});continue
        files.append((f['name'],data,{'name':f['name'],'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'width':w,'height':h,'dropped_top_mips':drop,'source':matches[key]['source']}))
    if not files:raise ValueError('No compatible HD textures')
    output.mkdir(parents=True)
    for name,data,_ in files:(output/name).write_bytes(data)
    summary={'source_apk_sha256':report['apk_sha256'],'exact_donor_matches':len(matches),'file_count':len(files),'total_bytes':sum(len(data) for _,data,_ in files),'maximum_dimension':2048,'files':[f for _,_,f in files],'skipped':skipped,'switch_hardware_tested':False}
    (output/'manifest.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    return summary

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('audit');p.add_argument('source');p.add_argument('output');a=p.parse_args()
    r=prepare(a.audit,a.source,a.output);print(json.dumps({k:v for k,v in r.items() if k not in ('files','skipped')}))

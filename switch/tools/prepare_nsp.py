#!/usr/bin/env python3
"""Prepare a standalone application ExeFS/control, without copying any keys."""
import argparse,hashlib,json,os,shutil,struct,subprocess
from pathlib import Path

TITLE_ID='0100d5ace0040000'

def prepare(elf,devkitpro,output,icon,version='0.4.2-compat',title_id=TITLE_ID,title_name='Dead Space Mobile'):
    elf,devkitpro,output,icon=map(lambda p:Path(p).resolve(),(elf,devkitpro,output,icon))
    if output.exists():raise ValueError('Output already exists')
    if elf.read_bytes()[:5]!=b'\x7fELF\x02':raise ValueError('Expected built AArch64 ELF64')
    output.mkdir(parents=True);(output/'exefs').mkdir();(output/'control').mkdir()
    # Preserve the packaging policy used by the user's working 0.3.8 lineage.
    config=json.loads((Path(__file__).resolve().parents[1]/'package/npdm.json').read_text(encoding='utf-8'))
    if len(title_id)!=16 or not 0x0100000000000000<=int(title_id,16)<0x0200000000000000 or int(title_id,16)&0xfff:
        raise ValueError('Expected an aligned homebrew application ID')
    for field in ('program_id','program_id_range_min','program_id_range_max'):config[field]='0x'+title_id
    (output/'npdm.json').write_text(json.dumps(config,indent=2),encoding='utf-8')
    suffix='.exe' if os.name=='nt' else ''
    def run(name,*args):subprocess.run([str(devkitpro/'tools/bin'/f'{name}{suffix}'),*map(str,args)],check=True)
    run('elf2nso',elf,output/'exefs/main')
    run('npdmtool',output/'npdm.json',output/'exefs/main.npdm')
    run('nacptool','--create',title_name,'Switch Port Contributors',version,output/'control/control.nacp','--titleid='+title_id)
    for language in ('AmericanEnglish','SimplifiedChinese','TraditionalChinese'):
        shutil.copyfile(icon,output/'control'/('icon_'+language+'.dat'))
    assert (output/'exefs/main').read_bytes()[:4]==b'NSO0'
    assert (output/'exefs/main.npdm').read_bytes()[:4]==b'META'
    files=[{'path':p.relative_to(output).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(output.rglob('*')) if p.is_file()]
    report={'title_id':title_id,'source_elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'files':files,'forwarder':False,'keys_copied':False,'switch_hardware_tested':False}
    (output/'preparation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf');p.add_argument('devkitpro');p.add_argument('output');p.add_argument('--icon',required=True);p.add_argument('--version',default='0.4.2-compat');p.add_argument('--title-id',default=TITLE_ID);p.add_argument('--name',default='Dead Space Mobile')
    args=p.parse_args();print(json.dumps(prepare(args.elf,args.devkitpro,args.output,args.icon,args.version,args.title_id,args.name),indent=2))

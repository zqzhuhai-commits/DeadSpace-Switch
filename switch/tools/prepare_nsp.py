#!/usr/bin/env python3
"""Prepare a standalone application ExeFS/control, without copying any keys."""
import argparse,hashlib,json,os,shutil,struct,subprocess
from pathlib import Path

TITLE_ID='0100d5ace0040000'

def prepare(elf,devkitpro,output,icon,version='0.4.0-rebuild'):
    elf,devkitpro,output,icon=map(lambda p:Path(p).resolve(),(elf,devkitpro,output,icon))
    if output.exists():raise ValueError('Output already exists')
    if elf.read_bytes()[:5]!=b'\x7fELF\x02':raise ValueError('Expected built AArch64 ELF64')
    output.mkdir(parents=True);(output/'exefs').mkdir();(output/'control').mkdir()
    # Application, 64-bit, 39-bit VA. Normal user syscalls plus CodeMemory/JIT.
    allowed=list(range(1,0x36))+[0x40,0x41,0x42,0x43,0x44,0x45,0x4b,0x4c,0x50,0x51,0x52,0x73,0x74,0x75,0x76,0x77,0x78]
    config={'name':'deadspacemobile','program_id':'0x'+TITLE_ID,'program_id_range_min':'0x'+TITLE_ID,'program_id_range_max':'0x'+TITLE_ID,
        'main_thread_stack_size':'0x400000','main_thread_priority':44,'default_cpu_id':0,'process_category':0,'pool_partition':0,
        'version':'0x0','system_resource_size':'0x0','signature_key_generation':0,
        'optimize_memory_allocation':False,'disable_device_address_space_merge':False,'enable_alias_region_extra_size':False,'prevent_code_reads':False,
        'is_64_bit':True,'address_space_type':3,'is_retail':True,
        'filesystem_access':{'permissions':'0xFFFFFFFFFFFFFFFF'},'service_host':[],'service_access':['*'],
        'kernel_capabilities':[{'type':'kernel_flags','value':{'highest_thread_priority':63,'lowest_thread_priority':16,'highest_cpu_id':2,'lowest_cpu_id':0}},
        {'type':'syscalls','value':{'svc%02X'%v:'0x%02X'%v for v in allowed}},
        {'type':'application_type','value':1},{'type':'min_kernel_version','value':'0x30'},
        {'type':'handle_table_size','value':1024},{'type':'debug_flags','value':{'allow_debug':False,'force_debug':False,'force_debug_prod':False}}]}
    (output/'npdm.json').write_text(json.dumps(config,indent=2),encoding='utf-8')
    suffix='.exe' if os.name=='nt' else ''
    def run(name,*args):subprocess.run([str(devkitpro/'tools/bin'/f'{name}{suffix}'),*map(str,args)],check=True)
    run('elf2nso',elf,output/'exefs/main')
    run('npdmtool',output/'npdm.json',output/'exefs/main.npdm')
    run('nacptool','--create','Dead Space Mobile','Switch Port Contributors',version,output/'control/control.nacp','--titleid='+TITLE_ID)
    for language in ('AmericanEnglish','SimplifiedChinese','TraditionalChinese'):
        shutil.copyfile(icon,output/'control'/('icon_'+language+'.dat'))
    assert (output/'exefs/main').read_bytes()[:4]==b'NSO0'
    assert (output/'exefs/main.npdm').read_bytes()[:4]==b'META'
    files=[{'path':p.relative_to(output).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(output.rglob('*')) if p.is_file()]
    report={'title_id':TITLE_ID,'source_elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'files':files,'forwarder':False,'keys_copied':False,'switch_hardware_tested':False}
    (output/'preparation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf');p.add_argument('devkitpro');p.add_argument('output');p.add_argument('--icon',required=True);p.add_argument('--version',default='0.4.0-rebuild')
    args=p.parse_args();print(json.dumps(prepare(args.elf,args.devkitpro,args.output,args.icon,args.version),indent=2))

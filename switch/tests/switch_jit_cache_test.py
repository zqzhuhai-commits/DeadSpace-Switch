#!/usr/bin/env python3
"""Execute the patched CodeBlock with mocked libnx cache/permission operations."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
DEPENDENCY=ROOT/'third_party/dynarmic'
FILES=['externals/oaknut/include/oaknut/code_block.hpp','src/dynarmic/backend/arm64/address_space.cpp']
with tempfile.TemporaryDirectory() as temp:
    work=Path(temp)
    for name in FILES:
        target=work/name
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(subprocess.check_output(['git','show','HEAD:'+name],cwd=DEPENDENCY))
    subprocess.run(['git','apply',str(ROOT/'switch/patches/dynarmic-switch-jit.patch')],cwd=work,check=True)
    (work/'switch.h').write_text(r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
using Result=unsigned;
#define R_FAILED(value) ((value)!=0)
enum JitType {JitType_SetProcessMemoryPermission,JitType_CodeMemory};
struct Jit {JitType type;std::size_t size;void* rw_addr;void* rx_addr;bool is_executable;};
namespace mock {
struct Range {void* address;std::size_t bytes;};
inline std::vector<Range> clean,invalid;
inline JitType type=JitType_CodeMemory;
inline unsigned transitions=0;
inline bool fail=false;
}
inline Result jitCreate(Jit* j,std::size_t size){
*j={mock::type,size,new std::uint8_t[size],new std::uint8_t[size],false};return 0;
}
inline void* jitGetRxAddr(Jit* j){return j->rx_addr;}
inline void* jitGetRwAddr(Jit* j){return j->rw_addr;}
inline Result jitClose(Jit* j){delete[] static_cast<std::uint8_t*>(j->rw_addr);delete[] static_cast<std::uint8_t*>(j->rx_addr);return 0;}
inline Result jitTransitionToWritable(Jit* j){j->is_executable=false;return 0;}
inline Result jitTransitionToExecutable(Jit* j){++mock::transitions;if(mock::fail)return 1;j->is_executable=true;return 0;}
inline void armDCacheFlush(void* address,std::size_t bytes){mock::clean.push_back({address,bytes});}
inline void armICacheInvalidate(void* address,std::size_t bytes){mock::invalid.push_back({address,bytes});}
''')
    source=work/'test.cpp'
    source.write_text(r'''
#include <oaknut/code_block.hpp>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("Switch cache regression");}
int main(){
    constexpr std::size_t size=32*1024*1024;
    {
        oaknut::CodeBlock block(size);
        block.invalidate_all();block.protect(); // prelude publication
        check(mock::clean.size()==1&&mock::clean[0].bytes==size);
        mock::clean.clear();mock::invalid.clear();
        block.unprotect();
        block.invalidate(block.ptr()+1024,64); // new emitted block
        block.invalidate(block.ptr()+16,8);    // relink an existing caller
        block.protect();
        check(mock::transitions==0); // no full-buffer libnx transition
        check(mock::clean.size()==2&&mock::invalid.size()==2);
        check(mock::clean[0].address==reinterpret_cast<std::uint8_t*>(block.wptr())+4096);
        check(mock::invalid[0].address==reinterpret_cast<std::uint8_t*>(block.ptr())+4096);
        check(mock::clean[0].bytes==64&&mock::clean[1].bytes==8);
        check(mock::invalid[0].bytes==64&&mock::invalid[1].bytes==8);
        block.invalidate(block.ptr()+size/4,0);
        check(mock::clean.size()==2);
        bool rejected=false;
        try{block.invalidate(block.ptr()+size/4-1,8);}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&mock::clean.size()==2);
    }
    mock::type=JitType_SetProcessMemoryPermission;
    mock::clean.clear();mock::invalid.clear();
    {
        oaknut::CodeBlock block(4096);
        block.unprotect();block.invalidate(block.ptr(),16);block.protect();
        check(mock::transitions==1&&mock::clean.empty()&&mock::invalid.empty());
        mock::fail=true;
        bool rejected=false;
        try{block.protect();}catch(const std::runtime_error&){rejected=true;}
        check(rejected);
    }
}
''')
    executable=work/'test'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-D__SWITCH__','-I'+str(work),'-I'+str(work/'externals/oaknut/include'),str(source),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
print('PASS: patched CodeMemory range/alias synchronization, old-link updates, bounds, zero ranges and legacy permission transitions')

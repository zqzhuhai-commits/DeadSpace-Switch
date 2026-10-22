#!/usr/bin/env python3
"""A terminated process must leave its last startup checkpoint on disk."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    work=Path(temp)
    source=work/"checkpoint.cpp"
    source.write_text('''#include "boot_log.hpp"
#include <cstdlib>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    if(!ds::boot_log(argv[1],argv[2]))return 3;
    std::_Exit(0); // no destructor or normal stdio shutdown
}
''')
    exe=work/"checkpoint"
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I"+str(ROOT/"include"),str(source),"-o",str(exe)],check=True)
    log=work/"diagnostics.log"
    for stage in ["BEGIN: JIT", "END: JIT", "BEGIN: first frame"]:
        subprocess.run([str(exe),str(log),stage],check=True)
    assert log.read_text()=="BEGIN: JIT\nEND: JIT\nBEGIN: first frame\n"
    result=subprocess.run([str(exe),str(work/"missing"/"log"),"unwritable"])
    assert result.returncode==3
print("PASS: startup checkpoints survive abrupt process exit, append across sessions and report open failure")

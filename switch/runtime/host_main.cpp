#include "cpu_probe.hpp"
#include "constructor_probe.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: ds-cpu-probe libEAMGameDeadSpace.so\n";return 64;}
    const auto report=cpu_probe(argv[1]);std::cout<<report;
    if(report.find("PASS: 5/5")==std::string::npos)return 1;
    const auto boot=constructor_probe(argv[1]);std::cout<<boot;
    return boot.find("PASS: 209 actual")!=std::string::npos?0:1;
}

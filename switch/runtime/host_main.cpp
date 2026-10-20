#include "cpu_probe.hpp"
#include "constructor_probe.hpp"
#include "lifecycle_probe.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: ds-cpu-probe libEAMGameDeadSpace.so [data_root] [frames]\n";return 64;}
    const auto report=cpu_probe(argv[1]);std::cout<<report;
    if(report.find("PASS: 5/5")==std::string::npos)return 1;
    const auto boot=constructor_probe(argv[1]);std::cout<<boot;
    if(boot.find("PASS: 209 actual")==std::string::npos)return 1;
    const std::string data=argc>2?argv[2]:".";
    const unsigned frames=argc>3?unsigned(std::atoi(argv[3])):1;
    const auto life=lifecycle_probe(argv[1],data,frames);std::cout<<life;
    return life.find("PASS:")==std::string::npos?1:0;
}

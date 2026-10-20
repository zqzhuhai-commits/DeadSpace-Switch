#include "cpu_probe.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: ds-cpu-probe libEAMGameDeadSpace.so\n";return 64;}
    const auto report=cpu_probe(argv[1]);std::cout<<report;
    return report.find("PASS: 5/5")!=std::string::npos?0:1;
}

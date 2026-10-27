#include "loading_budget.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok){if(!ok)throw std::runtime_error("Loading budget regression");}
int main(){try{
    ds::LoadingBudget load(true,15907607);
    check(!load.extend(15907607));
    check(load.extend(140420322)==100000000); // Device log: old 200M cap hit here.
    check(!load.extend(140420322));
    for(unsigned i=0;i<7;++i)check(load.extend(140420323+i)==100000000);
    check(load.total()==1000000000&&!load.extend(UINT64_MAX));
    ds::LoadingBudget play(false,0);check(!play.extend(UINT64_MAX));
    ds::LoadingBudget counter(true,100);check(!counter.extend(99));
    std::cout<<"PASS: logged load progress extends; no progress/in-game/overflow cannot extend; finite ceiling\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

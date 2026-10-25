#include "guest_math.hpp"
#include <iostream>
#include <limits>
#include <map>
static void check(bool ok){if(!ok)throw std::runtime_error("Guest math ABI regression");}
int main(){try{
    for(double value:{16.0398006439209,-16.0398006439209,0.0,-0.0,1.0,-1.0,.25,-.25,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto input=ds::guest_double_bits(value);std::map<uint32_t,uint32_t> words;
        auto result=ds::guest_modf(uint32_t(input),uint32_t(input>>32),0x1000,[&](uint32_t p,uint32_t v){words[p]=v;});
        check(words.size()==2&&words.count(0x1000)&&words.count(0x1004));
        double integral;double fraction=std::modf(value,&integral);
        double actual=ds::guest_double(uint32_t(result),uint32_t(result>>32));
        double stored=ds::guest_double(words[0x1000],words[0x1004]);
        check(std::isnan(fraction)?std::isnan(actual):actual==fraction&&std::signbit(actual)==std::signbit(fraction));
        check(std::isnan(integral)?std::isnan(stored):stored==integral&&std::signbit(stored)==std::signbit(integral));
    }
    for(uint32_t pointer:{0u,0xfffffffcu}){bool rejected=false;try{ds::guest_modf(0,0,pointer,[](uint32_t,uint32_t){});}catch(const std::runtime_error&){rejected=true;}check(rejected);}
    check(ds::guest_ldexp(0,0x3ff80000,uint32_t(-3))==0x3fc8000000000000ull);
    check(ds::guest_ldexp(0,0x3ff00000,1024)==0x7ff0000000000000ull);
    check(ds::guest_ldexp(0,0xbff00000,uint32_t(-1075))==0x8000000000000000ull);
    std::cout<<"PASS: ARM32 double split/writeback, signed zero, infinities, NaN, pointer bounds and ldexp exponent/limits\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

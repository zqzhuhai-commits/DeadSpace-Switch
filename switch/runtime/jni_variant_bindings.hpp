#pragma once
#include "jni_table_indices.hpp"
#include <array>
#include <string>

namespace ds {
template<class Register> void register_jni_variants(Register jslot) {
    const std::array<const char*,8> types={"Boolean","Byte","Char","Short","Int","Long","Float","Double"};
    for(unsigned i=0;i<types.size();++i){
        const auto type=std::string(types[i]);
        jslot(JNI_GetBooleanArrayElements+i,("Get"+type+"ArrayElements").c_str());
        jslot(JNI_ReleaseBooleanArrayElements+i,("Release"+type+"ArrayElements").c_str());
        jslot(JNI_GetBooleanArrayRegion+i,("Get"+type+"ArrayRegion").c_str());
        jslot(JNI_SetBooleanArrayRegion+i,("Set"+type+"ArrayRegion").c_str());
    }
    const std::array<const char*,10> returns={"Object","Boolean","Byte","Char","Short","Int","Long","Float","Double","Void"};
    for(unsigned i=0;i<returns.size();++i)for(unsigned variant=0;variant<3;++variant){
        const std::string suffix=variant==1?"V":variant==2?"A":"";
        jslot(JNI_CallObjectMethod+i*3+variant,("Call"+std::string(returns[i])+"Method"+suffix).c_str());
        jslot(JNI_CallStaticObjectMethod+i*3+variant,("CallStatic"+std::string(returns[i])+"Method"+suffix).c_str());
    }
}
}

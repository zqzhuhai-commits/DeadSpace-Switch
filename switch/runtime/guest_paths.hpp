#pragma once
#include <stdexcept>
#include <string>
#include <sstream>

namespace ds {
// Horizon device paths do not start with '/'. Do not prepend data_root again
// when Java has already supplied romfs:/gamedata or sdmc:/.../gamedata.
inline std::string resolve_data_path(const std::string& root,const std::string& raw) {
    std::string relative;
    if(raw==root)relative="";
    else if(raw.rfind(root+"/",0)==0)relative=raw.substr(root.size()+1);
    else if(raw.rfind("appbundle:",0)==0) {
        relative=raw.substr(10);
        while(!relative.empty()&&relative.front()=='/')relative.erase(0,1);
        relative="assets/"+relative;
    } else {
        if((!raw.empty()&&raw.front()=='/')||raw.find(':')!=std::string::npos)
            throw std::runtime_error("Guest path outside selected data root");
        relative=raw;
    }
    std::string result=root,part;
    std::istringstream components(relative);
    while(std::getline(components,part,'/')) {
        if(part==".."||part.find(':')!=std::string::npos||part.find('\\')!=std::string::npos)
            throw std::runtime_error("Guest path escapes selected data root");
        if(!part.empty()&&part!=".")result+="/"+part;
    }
    return result;
}
}

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
            throw std::runtime_error("Guest path outside selected data root: "+raw);
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
struct GuestPath {std::string path;bool writable;};
struct GuestStoragePaths {
    std::string data_root,save_root;
    bool ancestor(const std::string& raw)const {
        if(raw.empty())return false;
        const auto prefix=raw+(raw.back()=='/'?"":"/");
        return data_root.rfind(prefix,0)==0||(!save_root.empty()&&save_root.rfind(prefix,0)==0);
    }
    GuestPath resolve(const std::string& raw)const {
        const bool resource=raw.rfind("appbundle:",0)==0||raw==data_root||raw.rfind(data_root+"/",0)==0||raw=="assets"||raw.rfind("assets/",0)==0;
        if(resource||save_root.empty())return {resolve_data_path(data_root,raw),false};
        return {resolve_data_path(save_root,raw),true};
    }
    GuestPath metadata(const std::string& raw)const {
        // EAIO recursively stats parents before creating its data directory.
        // Parent metadata is read-only; it never grants fopen or write access.
        if(ancestor(raw))return {raw,false};
        return resolve(raw);
    }
};
}

#pragma once
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ds {
// A small virtual namespace: appbundle:/assets are immutable; /data is a
// separate writable save tree. Guest absolute paths never name host files.
class GuestStorage {
    std::filesystem::path assets, saves;
    static bool beneath(const std::filesystem::path& path,const std::filesystem::path& root) {
        auto p=path.begin();for(auto r=root.begin();r!=root.end();++r,++p)if(p==path.end()||*p!=*r)return false;
        return true;
    }
public:
    GuestStorage(const std::string& donor,const std::string& save_directory):assets(std::filesystem::path(donor)/"assets"),saves(save_directory) {
        std::filesystem::create_directories(saves);
#ifndef __SWITCH__
        assets=std::filesystem::weakly_canonical(assets);saves=std::filesystem::weakly_canonical(saves);
#endif
        const auto seed=std::filesystem::path(donor)/"preferences/settings.sb";
        const auto destination=resolve("/data/var/settings.sb",true);
        if(std::filesystem::is_regular_file(seed)&&!std::filesystem::exists(destination)){
            std::filesystem::create_directories(resolve("/data/var",true));
            std::filesystem::copy_file(seed,destination,std::filesystem::copy_options::none);
        }
    }
    std::string resolve(std::string raw,bool writing=false) const {
        bool resource=false;
        if(raw.rfind("appbundle:",0)==0){raw=raw.substr(10);resource=true;}
        else if(raw.rfind("assets/",0)==0){raw=raw.substr(7);resource=true;}
        else if(raw=="/data")raw.clear();
        else if(raw.rfind("/data/",0)==0)raw=raw.substr(6);
        while(!raw.empty()&&raw.front()=='/')raw.erase(raw.begin());
        // The engine's extracted-content VFS names this tree independently
        // of the APK's appbundle: scheme. It is still immutable game data.
        if(raw=="published"||raw.rfind("published/",0)==0)resource=true;
        if(raw.find(':')!=std::string::npos||raw.find('\\')!=std::string::npos)throw std::runtime_error("Invalid guest path namespace");
        const auto relative=std::filesystem::path(raw);
        for(const auto& part:relative)if(part=="..")throw std::runtime_error("Guest path traversal rejected");
        if(resource&&writing)throw std::runtime_error("Write to immutable game resources rejected");
        const auto& root=resource?assets:saves;
        auto result=(root/relative).lexically_normal();
#ifndef __SWITCH__
        result=std::filesystem::weakly_canonical(result);
        if(!beneath(result,root))throw std::runtime_error("Guest symlink escapes storage root");
#endif
        return result.generic_string();
    }
};
}

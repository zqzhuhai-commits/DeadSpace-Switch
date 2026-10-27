#pragma once
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <array>
#include <sys/stat.h>

namespace ds {
// A small virtual namespace: appbundle:/assets are immutable; /data is a
// separate writable save tree. Guest absolute paths never name host files.
class GuestStorage {
    std::filesystem::path assets, saves;
    static void seed_settings(const std::filesystem::path& source,const std::string& destination){
        struct stat st{};
        if(::stat(destination.c_str(),&st)==0){
            if(!S_ISREG(st.st_mode))throw std::runtime_error("Settings destination is not a regular file");
            if(st.st_size>0)return; // Never replace existing player preferences.
        }else if(errno!=ENOENT)throw std::runtime_error("Cannot inspect initial settings destination");
        const auto input_name=source.string();
        auto* input=std::fopen(input_name.c_str(),"rb");
        if(!input)throw std::runtime_error("Cannot open initial settings resource");
        const auto temporary=destination+".initializing";
        auto* output=std::fopen(temporary.c_str(),"wb");
        if(!output){std::fclose(input);throw std::runtime_error("Cannot create initial settings temporary file");}
        std::array<unsigned char,4096> buffer{};size_t total=0;bool ok=true;
        for(;;){
            const auto count=std::fread(buffer.data(),1,buffer.size(),input);
            if(count){total+=count;if(total>1024*1024||std::fwrite(buffer.data(),1,count,output)!=count){ok=false;break;}}
            if(count<buffer.size()){if(std::ferror(input))ok=false;break;}
        }
        if(std::fclose(input)!=0)ok=false;
        if(std::fclose(output)!=0)ok=false;
        if(!ok||!total){std::remove(temporary.c_str());throw std::runtime_error("Initial settings stream copy failed");}
        // A previous failed filesystem::copy_file can leave a zero-byte file.
        // Remove only that incomplete placeholder, after the new copy is closed.
        if(::stat(destination.c_str(),&st)==0){
            if(!S_ISREG(st.st_mode)||st.st_size>0){std::remove(temporary.c_str());return;}
            if(std::remove(destination.c_str())!=0){std::remove(temporary.c_str());throw std::runtime_error("Cannot replace empty initial settings");}
        }else if(errno!=ENOENT){std::remove(temporary.c_str());throw std::runtime_error("Cannot recheck initial settings destination");}
        if(std::rename(temporary.c_str(),destination.c_str())!=0){std::remove(temporary.c_str());throw std::runtime_error("Cannot publish initial settings");}
    }
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
        if(std::filesystem::is_regular_file(seed)){
            std::filesystem::create_directories(resolve("/data/var",true));
            seed_settings(seed,destination);
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

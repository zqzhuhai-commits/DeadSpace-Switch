#include "guest_storage.hpp"
#include <fstream>
#include <iostream>
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void reject(F fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught,"Unsafe path accepted");}
int main(int argc,char** argv){
    try {
        check(argc==2,"Supply a new temporary directory");
        const std::filesystem::path root=argv[1];check(std::filesystem::create_directory(root),"Directory already exists");
        struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{root};
        std::filesystem::create_directories(root/"donor/assets");
        std::filesystem::create_directories(root/"donor/preferences");std::ofstream(root/"donor/preferences/settings.sb")<<"seeded locale fixture";
        std::ofstream(root/"donor/assets/original")<<"original";
        ds::GuestStorage fs((root/"donor").string(),(root/"saves").string());
        check(std::filesystem::is_regular_file(root/"saves/var/settings.sb"),"Initial preferences not seeded");
        {std::ifstream seeded(root/"saves/var/settings.sb");std::string text;std::getline(seeded,text);check(text=="seeded locale fixture","Initial preferences bytes differ");}
        std::ofstream(root/"saves/var/settings.sb",std::ios::trunc).close();
        ds::GuestStorage repaired((root/"donor").string(),(root/"saves").string());
        check(std::filesystem::file_size(root/"saves/var/settings.sb")==21,"Zero-byte interrupted settings not repaired");
        check(!std::filesystem::exists(root/"saves/var/settings.sb.initializing"),"Temporary settings leaked");
        std::ofstream(root/"saves/var/settings.sb",std::ios::trunc)<<"player settings";
        ds::GuestStorage restarted((root/"donor").string(),(root/"saves").string());
        check(std::filesystem::file_size(root/"saves/var/settings.sb")==15,"Restart overwrote player preferences");
        const auto asset=std::filesystem::weakly_canonical(root/"donor/assets/original").generic_string();
        check(fs.resolve("appbundle:original")==asset,"Resource mapping wrong");
        check(fs.resolve("assets/original")==asset,"Resource alias wrong");
        check(fs.resolve("/data/published/data")==fs.resolve("appbundle:/published/data"),"Extracted content VFS escaped assets");
        reject([&]{fs.resolve("/data/published/data",true);});
        check(fs.resolve("/")==fs.resolve("/data"),"Virtual root escaped saves");
        check(fs.resolve("/data/config",true)==fs.resolve("config",true),"Save alias wrong");
        std::ofstream(fs.resolve("config",true))<<"saved";
        check(std::filesystem::exists(root/"saves/config")&&!std::filesystem::exists(root/"donor/config"),"Save contaminated donor");
        reject([&]{fs.resolve("appbundle:original",true);});
        reject([&]{fs.resolve("../outside",true);});
        reject([&]{fs.resolve("/data/../outside",true);});
        reject([&]{fs.resolve("C:/outside",true);});
        reject([&]{fs.resolve("appbundle:../outside");});
        std::cout<<"PASS: virtual roots, immutable assets, save isolation and path escape rejection\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

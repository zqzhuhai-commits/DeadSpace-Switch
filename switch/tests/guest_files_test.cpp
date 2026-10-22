#include "guest_files.hpp"
#include "guest_stat.hpp"
#include "guest_paths.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    if(argc!=2)return 64;
    try {
        const auto path=(std::filesystem::path(argv[1])/"file-fixture.bin").string();
        {auto* f=std::fopen(path.c_str(),"wb");check(f,"fixture create");std::fwrite("abcdefg",1,7,f);std::fclose(f);}
        ds::GuestFiles files;
        const std::string save=(std::filesystem::path(argv[1])/"saves").string();
        std::filesystem::create_directory(save);
        ds::GuestStoragePaths paths{"romfs:/gamedata",save};
        const auto parent=std::filesystem::path(save).parent_path().string();
        check(paths.metadata(parent).path==parent&&!paths.metadata(parent).writable,"EAIO parent stat regression");
        bool parent_open=false;try{paths.resolve(parent);}catch(const std::exception&){parent_open=true;}check(parent_open,"Parent metadata enabled arbitrary file access");
        check(paths.resolve("appbundle:/published/data/file").path=="romfs:/gamedata/assets/published/data/file"&&!paths.resolve("assets/file").writable,"Packaged resources became writable");
        const auto target=paths.resolve(save+"/slot1.dat");
        check(target.writable&&target.path==save+"/slot1.dat"&&paths.resolve("slot1.dat").path==target.path,"Save directory routing");
        bool escaped=false;try{paths.resolve("sdmc:/switch/deadspace/slot1.dat");}catch(const std::exception&){escaped=true;}check(escaped,"Save escaped into another game's directory");
        {
            ds::GuestFiles session;
            const auto fd=session.open(target.path.c_str(),2|64|128,0600);
            check(fd!=UINT32_MAX,"Create exclusive guest save descriptor");
            check(session.open(target.path.c_str(),1|64|128)==UINT32_MAX&&errno==EEXIST,"Guest O_EXCL ignored");
            check(std::fwrite("saved",1,5,session.descriptor(fd))==5,"Write guest save");
            struct stat persisted{};check(::stat(target.path.c_str(),&persisted)==0&&persisted.st_size==5,"Descriptor writes remained in host stdio buffer");
            check(session.close(fd)==0,"Close save descriptor");
        }
        {
            ds::GuestFiles session;
            const auto fd=session.open(target.path.c_str());char content[8]{};
            check(fd!=UINT32_MAX&&std::fread(content,1,5,session.descriptor(fd))==5&&std::string(content,5)=="saved","Save did not survive runtime restart");
            const auto stream=session.fopen(target.path.c_str(),"ab");
            check(stream&&std::fwrite("!",1,1,session.stream(stream))==1&&session.flush(stream)==0,"Append and flush save");
            check(session.fclose(stream)==0,"Close appended save");
            check(session.open(target.path.c_str(),3)==UINT32_MAX&&errno==EINVAL,"Invalid guest access mode accepted");
            check(session.open(target.path.c_str(),0x80000000)==UINT32_MAX&&errno==EINVAL,"Unknown guest flags accepted");
        }
        check(std::filesystem::file_size(target.path)==6,"Persistent save append size");
        const auto a=files.open(path.c_str()),b=files.open(path.c_str()),c=files.open(path.c_str());
        check(a!=UINT32_MAX&&b!=a&&c!=b,"descriptor allocation");
        check(files.close(b)==0,"close middle descriptor");
        const auto d=files.open(path.c_str());
        check(d!=a&&d!=b&&d!=c,"open collided with a live or stale descriptor");
        check(!files.descriptor(b)&&errno==EBADF,"closed descriptor accepted");
        check(files.close(b)==-1&&errno==EBADF,"double close accepted");
        char bytes[8]{};
        check(std::fread(bytes,1,2,files.descriptor(c))==2,"read descriptor");
        struct stat metadata{};
        check(files.stat(c,metadata)==0&&metadata.st_size==7,"fstat size incorrect");
        const auto file_stat=ds::guest_stat(metadata);
        check(file_stat.size()==104&&file_stat[48]==7&&(file_stat[17]&0xf0)==0x80,"file ABI size/mode incorrect");
        check(file_stat[8]==0&&file_stat[44]==0&&file_stat[60]==0,"stat ABI padding uninitialized");
        check(::stat(argv[1],&metadata)==0&&S_ISDIR(metadata.st_mode),"directory metadata");
        const auto dir_stat=ds::guest_stat(metadata);
        const uint32_t mode=dir_stat[16]|uint32_t(dir_stat[17])<<8;
        check((mode&0xf000)==0x4000,"directory reported as a regular file (recursive mkdir regression)");
        check(files.stat(b,metadata)==-1&&errno==EBADF,"fstat accepted closed fd");
        check(std::ftell(files.descriptor(c))==2,"fstat changed file position");
        check(std::fread(bytes,1,2,files.descriptor(c))==2&&bytes[0]=='c'&&bytes[1]=='d',"fstat damaged read cursor");
        const auto stream=files.fopen(path.c_str(),"rb");
        check(stream&&files.stream(stream),"stdio handle missing");
        check(!files.descriptor(stream)&&errno==EBADF,"stdio token accepted as descriptor");
        check(!files.stream(a)&&errno==EBADF,"descriptor accepted as stdio token");
        auto* fp=files.stream(stream);
        check(std::fread(bytes,1,7,fp)==7&&std::string(bytes,7)=="abcdefg","64-bit host FILE pointer roundtrip");
        check(files.fclose(stream)==0&&!files.stream(stream),"stream close");
        check(files.fclose(stream)==-1&&errno==EBADF,"stale stream accepted");
        check(files.fopen((path+"-missing").c_str(),"rb")==0&&errno==ENOENT,"missing stdio file");
        check(files.open((path+"-missing").c_str())==UINT32_MAX&&errno==ENOENT,"missing descriptor file");
        std::filesystem::remove(path);
        std::cout<<"PASS: opaque stdio tokens, descriptor collision regression, independent cursors, non-destructive stat, invalid handles, persistent save/reopen, append/flush and guest open flags\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

#include "guest_files.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Supply a new test directory");
        const std::filesystem::path root=argv[1];
        require(std::filesystem::create_directory(root),"Test directory exists");
        struct Cleanup {std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{root};
        const auto path=(root/"fixture.bin").string();
        std::ofstream(path,std::ios::binary)<<"abcdefg";
        ds::GuestFiles f;
        const auto a=f.open_fd(path.c_str(),"rb"), b=f.open_fd(path.c_str(),"rb"), c=f.open_fd(path.c_str(),"rb");
        require(a==3&&b==4&&c==5,"Initial descriptors wrong");
        require(f.close_fd(b)==0,"Close middle descriptor failed");
        const auto d=f.open_fd(path.c_str(),"rb");
        require(d!=a&&d!=c,"Descriptor collision after close");
        std::array<char,8> buf{};
        const auto stream=f.stream_for_fd(c);
        require(f.read(stream,buf.data(),2)==2&&std::string(buf.data(),2)=="ab","Read wrong");
        require(f.size(stream)==7&&f.tell(stream)==2,"fstat changed read position");
        require(f.seek(stream,-2,SEEK_END)==0&&f.tell(stream)==5,"Negative seek wrong");
        require(f.read(stream,buf.data(),8)==2&&std::string(buf.data(),2)=="fg","Short EOF read wrong");
        require(f.close_fd(999)==-1&&f.error()==EBADF,"Invalid close accepted");
        require(f.read(0x1234,buf.data(),1)==0&&f.error()==EBADF,"Invalid stream accepted");
        const auto output=(root/"save.bin").string();
        const auto token=f.open_stream(output.c_str(),"wb+");
        require(token>=0x71000000,"Host pointer escaped instead of opaque token");
        require(f.write(token,"saved",5)==5&&f.flush(token)==0,"Write/flush failed");
        require(f.seek(token,0,SEEK_SET)==0&&f.read(token,buf.data(),8)==5&&std::string(buf.data(),5)=="saved","Save readback wrong");
        require(f.close_stream(token)==0&&f.close_stream(token)==-1&&f.error()==EBADF,"Stale handle accepted");
        const auto reopened=f.open_stream(output.c_str(),"rb");
        require(reopened!=token&&f.size(reopened)==5,"Stale token reused");
        require(f.open_stream((root/"missing").string().c_str(),"rb")==0&&f.error()==ENOENT,"Missing file error lost");
        require(f.seek(stream,0,999)==-1&&f.error()==EINVAL,"Invalid seek accepted");
        std::cout<<"PASS: opaque ARM32 handles, close/reopen isolation, EOF, fstat offset, save write/flush/readback, invalid handles\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

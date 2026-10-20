#include "guest_image.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {
    if(argc!=2)return 64;
    try {
        ds::GuestImage image;
        std::string error;
        if(!image.load(argv[1],error))throw std::runtime_error(error);
        require(image.memory.size()<=ds::GuestLayout::image_limit,"Image retains double-biased storage hole");
        require(image.imports.size()==392,"Donor import count mismatch");
        require(image.constructors.size()==209,"Donor constructor count mismatch");
        const auto before=image.memory;
        unsigned n=0;
        require(!image.bind_imports([&](const auto&){return ++n==7?0u:0x60000000u;},error),"Accepted missing import");
        require(image.memory==before,"Failed binding partially changed image");
        uint32_t address=0x60000000;
        if(!image.bind_imports([&](const auto&){address+=16;return address;},error))throw std::runtime_error(error);
        const auto bound=image.memory;
        require(!image.bind_imports([](const auto&){return 0x61000000;},error),"Accepted duplicate binding");
        require(image.memory==bound,"Duplicate binding changed image");
        uint32_t word=0;
        require(!image.read(ds::GuestLayout::stack_base,&word,4),"Image aliases guest stack");
        require(!image.executable(ds::GuestLayout::stack_base,4),"Stack aliases executable image");
        require(!image.read(0xfffffffcu,&word,8),"Accepted wrapped memory range");
        require(!image.read(ds::GuestImage::base+8,&word,SIZE_MAX),"Accepted overflowing read length");
        require(!image.write(ds::GuestImage::base+8,&word,SIZE_MAX),"Accepted overflowing write length");
        require(!image.write(ds::GuestImage::base,&word,4),"Guest wrote read-only code segment");
        require(image.memory==bound,"Rejected write changed image");
        require(!image.load("/nonexistent/deadspacemobile.so",error),"Missing donor accepted");
        require(image.memory.empty() && image.imports.empty() && image.constructors.empty(),"Failed reload retained previous image");
        std::cout<<"PASS: exact donor metadata, transactional binding, duplicate rejection, memory boundaries, failed reload cleanup\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

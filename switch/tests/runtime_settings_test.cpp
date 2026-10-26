#include "runtime_settings.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
int main(){
 const char* path="runtime-toggle-test.tmp";std::remove(path);
 assert(ds::read_runtime_toggle(path,true));assert(!ds::read_runtime_toggle(path,false));
 for(const char* text:{"0","1","2","-1","nan","1 extra","","0\n","1\n"}){
  auto* f=std::fopen(path,"w");assert(f);std::fputs(text,f);std::fclose(f);
  const bool zero=std::string(text)=="0"||std::string(text)=="0\n";
  const bool one=std::string(text)=="1"||std::string(text)=="1\n";
  assert(ds::read_runtime_toggle(path,true)==!zero);
  assert(ds::read_runtime_toggle(path,false)==one);
 }
 std::remove(path);std::cout<<"PASS: runtime toggle defaults, valid values, malformed fallback\n";
}

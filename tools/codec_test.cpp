#include "codec.hpp"
#include <fstream>
#include <iostream>
using namespace cowboy;
Bytes read(const char* p){std::ifstream f(p,std::ios::binary);need(bool(f),p);return Bytes(std::istreambuf_iterator<char>(f),{});}
template<class F> void rejects(F fn){try{fn();}catch(const std::exception&){return;}throw std::runtime_error("Invalid input was accepted");}
int main(int argc,char** argv){try{
 need(argc==5,"Usage: codec_test Kmdl.arc Bmdl.arc model.patch expected.bmd");
 auto a=resource(read(argv[1]),"al_head.bmd"),b=resource(read(argv[2]),"bl_head.bmd"),p=read(argv[3]),expected=read(argv[4]);
 a.insert(a.end(),b.begin(),b.end());need(cowboy::apply(a,p)==expected,"Reconstructed model differs");
 auto wrong=a;wrong[0]^=1;rejects([&]{cowboy::apply(wrong,p);});
 auto damaged=p;damaged.back()^=1;rejects([&]{cowboy::apply(a,damaged);});
 for(size_t n:{size_t(0),size_t(7),size_t(23),size_t(24),p.size()-1}){auto cut=Bytes(p.begin(),p.begin()+n);rejects([&]{cowboy::apply(a,cut);});}
 rejects([&]{resource(Bytes(64,0),"al_head.bmd");});
 rejects([&]{resource(read(argv[1]),"missing.bmd");});
 std::cout<<"PASS: exact reconstruction, wrong base, corrupt/truncated patches, invalid archives\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
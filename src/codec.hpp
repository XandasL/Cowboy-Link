#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace cowboy {
using Bytes=std::vector<uint8_t>;
inline void need(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
inline uint32_t be(const Bytes& b,size_t p){need(p+4<=b.size(),"Truncated data");return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
inline uint32_t crc(const Bytes& b){uint32_t c=~0u;for(auto v:b){c^=v;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
inline Bytes unpack(const Bytes& b){
 if(b.size()<4||std::memcmp(b.data(),"Yaz0",4))return b;
 auto size=be(b,4);need(size<=32*1024*1024,"Archive too large");Bytes out;out.reserve(size);size_t p=16;int bits=0;uint8_t code=0;
 while(out.size()<size){if(!bits){need(p<b.size(),"Truncated Yaz0");code=b[p++];bits=8;}
  if(code&128){need(p<b.size(),"Truncated literal");out.push_back(b[p++]);}
  else{need(p+2<=b.size(),"Truncated backreference");auto a=b[p++],v=b[p++];size_t dist=((a&15)<<8|v)+1,n=a>>4;
   if(!n){need(p<b.size(),"Truncated length");n=b[p++]+18;}else n+=2;
   need(dist<=out.size()&&out.size()+n<=size,"Invalid Yaz0 reference");while(n--)out.push_back(out[out.size()-dist]);}
  code<<=1;--bits;
 }return out;
}
inline Bytes resource(const Bytes& compressed,const char* name){
 auto b=unpack(compressed);need(b.size()>=64&&!std::memcmp(b.data(),"RARC",4),"Unsupported archive format");
 size_t data=32+be(b,12),count=be(b,40),entries=32+be(b,44),strings=32+be(b,52);
 need(count<100000&&entries+count*20<=b.size(),"Invalid archive directory");
 for(size_t i=0;i<count;i++){size_t e=entries+i*20;auto flags=be(b,e+4);if(!(flags&0x01000000))continue;
  size_t n=strings+(flags&0xffff);need(n<b.size(),"Invalid archive name");size_t end=n;while(end<b.size()&&b[end])end++;
  need(end<b.size(),"Unterminated archive name");if(std::string((char*)b.data()+n,end-n)!=name)continue;
  size_t off=data+be(b,e+8),len=be(b,e+12);need(off<=b.size()&&len<=b.size()-off,"Invalid resource range");return Bytes(b.begin()+off,b.begin()+off+len);
 }throw std::runtime_error(std::string("Original game resource missing: ")+name);
}
inline Bytes apply(const Bytes& base,const Bytes& patch){
 need(patch.size()>=24&&!std::memcmp(patch.data(),"CWHPT001",8),"Invalid patch");
 need(be(patch,8)==base.size()&&be(patch,12)==crc(base),"Game model differs from supported original; refusing patch");
 auto length=be(patch,16);need(length<4*1024*1024,"Invalid output length");Bytes out;out.reserve(length);size_t p=24;
 while(p<patch.size()){
  auto op=patch[p++];auto n=be(patch,p);p+=4;need(n<=length-out.size(),"Patch exceeds output size");
  if(op==0){auto start=be(patch,p);p+=4;need(start<=base.size()&&n<=base.size()-start,"Invalid COPY");out.insert(out.end(),base.begin()+start,base.begin()+start+n);}
  else if(op==1){need(n<=patch.size()-p,"Invalid ADD");out.insert(out.end(),patch.begin()+p,patch.begin()+p+n);p+=n;}
  else if(op==2){need(p+8<=patch.size(),"Invalid generated texture");for(uint32_t i=0;i<n;i++)out.push_back(patch[p+i%8]);p+=8;}
  else throw std::runtime_error("Unknown patch instruction");
 }
 need(out.size()==length&&crc(out)==be(patch,20),"Reconstructed model checksum mismatch");return out;
}
}
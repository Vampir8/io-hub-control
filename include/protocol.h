#pragma once
#include <string>
#include <cstdint>

namespace hub {
constexpr size_t MaxPayload = 384;
struct Message { char type = 0; std::string source, state; };
inline bool sameState(const Message& a,const Message& b){return a.type==b.type&&a.source==b.source&&a.state==b.state;}
struct SyncSession {
  uint16_t subscription=0;bool requested=false;
  void connected(uint16_t id){subscription=id;requested=false;}
  void disconnected(){subscription=0;requested=false;}
  bool ack(uint16_t id,uint8_t qos){if(!subscription||id!=subscription||qos==0x80||requested)return false;requested=true;return true;}
};
inline bool utf8(const std::string& s) {
  size_t i=0;
  while(i<s.size()) {
    uint8_t c=s[i++];
    if(c<0x20 || c==0x7f) return false;
    if(c<0x80) continue;
    int n; uint32_t cp, min;
    if(c>=0xc2 && c<=0xdf) {n=1;cp=c&31;min=0x80;}
    else if(c>=0xe0 && c<=0xef) {n=2;cp=c&15;min=0x800;}
    else if(c>=0xf0 && c<=0xf4) {n=3;cp=c&7;min=0x10000;}
    else return false;
    if(i+n>s.size()) return false;
    while(n--) {uint8_t b=s[i++]; if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
    if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
  }
  return true;
}
inline bool unsignedNumber(const std::string& s, uint32_t max, uint32_t& v) {
  if(s.empty() || s.size()>10) return false;
  v=0;
  for(char c:s) {if(c<'0'||c>'9'||v>(max-uint32_t(c-'0'))/10)return false;v=v*10+(c-'0');}
  return v<=max;
}
inline bool parse(const char* data, size_t len, Message& out) {
  if(!data || len<5 || len>MaxPayload) return false;
  std::string s(data,len);
  if(!utf8(s) || (s[0]!='T' && s[0]!='S') || s[1]!='|') return false;
  auto p=s.find('|',2);
  if(p==std::string::npos || p==2 || p+1==len || s.find('|',p+1)!=std::string::npos) return false;
  Message m; m.type=s[0];m.source=s.substr(2,p-2);m.state=s.substr(p+1);
  if(m.source.size()>96 || m.state.size()>96) return false;
  if(m.type=='S') {
    uint32_t index; if(!unsignedNumber(m.source,2147483647,index))return false;
    std::string number=m.state;
    if(number[0]=='-')number.erase(0,1);
    uint32_t value;if(!unsignedNumber(number,2147483647,value))return false;
    m.source=std::to_string(index);
  }
  out=std::move(m);return true;
}
}

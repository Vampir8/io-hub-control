#pragma once
#include <Preferences.h>
#include "config.h"
namespace hub {
class Store {
  Preferences prefs;uint64_t generation=0;int selected=-1;
  struct Header {uint64_t generation;uint32_t length,crc;};
  uint32_t crc(const uint8_t* p,size_t n) {
    uint32_t c=0xffffffff;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;
  }
  bool read(int slot,DynamicJsonDocument& doc,uint64_t& gen) {
    const char* key=slot?"slot1":"slot0";size_t n=prefs.getBytesLength(key);
    if(n<sizeof(Header)||n>MaxConfigBytes+sizeof(Header))return false;
    std::vector<uint8_t> bytes(n);if(prefs.getBytes(key,bytes.data(),n)!=n)return false;
    Header h;memcpy(&h,bytes.data(),sizeof h);
    if(h.length!=n-sizeof h || crc(bytes.data()+sizeof h,h.length)!=h.crc)return false;
    doc.clear();if(decodeConfig(doc,bytes.data()+sizeof h,h.length)||!validate(doc.as<JsonVariantConst>()).empty())return false;
    gen=h.generation;return true;
  }
public:
  bool begin(DynamicJsonDocument& doc) {
    if(!prefs.begin("nchub",false))return false;
    uint64_t ga=0,gb=0;DynamicJsonDocument other(JsonCapacity);
    bool a=read(0,doc,ga),b=read(1,other,gb);
    if(b&&(!a||gb>ga)){doc.set(other);selected=1;generation=gb;return true;}
    if(a){selected=0;generation=ga;return true;}doc.clear();return false;
  }
  bool save(JsonVariantConst doc) {
    size_t length=measureJson(doc);if(length>MaxConfigBytes)return false;
    std::vector<uint8_t> bytes(sizeof(Header)+length+1);
    if(serializeJson(doc,reinterpret_cast<char*>(bytes.data()+sizeof(Header)),length+1)!=length)return false;
    Header h={generation+1,uint32_t(length),crc(bytes.data()+sizeof(Header),length)};
    memcpy(bytes.data(),&h,sizeof h);bytes.resize(sizeof(Header)+length);
    int slot=selected==0?1:0;
    if(prefs.putBytes(slot?"slot1":"slot0",bytes.data(),bytes.size())!=bytes.size())return false;
    // Verify persisted header and CRC without a second JSON document/string.
    const char* key=slot?"slot1":"slot0";
    if(prefs.getBytesLength(key)!=bytes.size() || prefs.getBytes(key,bytes.data(),bytes.size())!=bytes.size())return false;
    Header actual;memcpy(&actual,bytes.data(),sizeof actual);
    if(actual.generation!=h.generation || actual.length!=h.length || actual.crc!=h.crc ||
       crc(bytes.data()+sizeof actual,actual.length)!=h.crc)return false;
    selected=slot;generation=actual.generation;return true;
  }
  bool backup(DynamicJsonDocument& doc){uint64_t gen;return selected>=0 && read(1-selected,doc,gen);}
  void reset(){prefs.clear();}
};
}

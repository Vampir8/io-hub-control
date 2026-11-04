#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
class Preferences {
public:
  inline static std::map<std::string,std::vector<uint8_t>> blobs;
  inline static bool failWrite=false,corruptWrite=false;
  bool begin(const char*,bool){return true;}
  size_t getBytesLength(const char* key){return blobs[key].size();}
  size_t getBytes(const char* key,void* data,size_t length){auto& b=blobs[key];if(length<b.size())return 0;memcpy(data,b.data(),b.size());return b.size();}
  size_t putBytes(const char* key,const void* data,size_t length){if(failWrite)return 0;auto p=static_cast<const uint8_t*>(data);blobs[key]={p,p+length};if(corruptWrite&&!blobs[key].empty())blobs[key].back()^=1;return length;}
  void clear(){blobs.clear();}
};

#pragma once
#include "config.h"
#include "signal_profiles.h"
namespace hub {
constexpr size_t MaxSignalPayload=1024,MaxSignalTopic=320;
struct SignalMessage {Message message;std::string head;uint32_t period=1000;};
inline bool nativeSignal(JsonVariantConst p,JsonVariantConst t){return !t["head"].isNull()&&std::string(p["ws"]["input"]|"json")=="json";}
inline std::string signalTopic(JsonVariantConst p) {
  if(!p["ws"]["topic"].isNull()&&strlen(p["ws"]["topic"].as<const char*>()))return p["ws"]["topic"].as<const char*>();
  const char* label=p["source"];std::string encoded;const char* hex="0123456789ABCDEF";
  for(const unsigned char* s=(const unsigned char*)label;*s;++s){unsigned char ch=*s;if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')encoded+=ch;else{encoded+='%';encoded+=hex[ch>>4];encoded+=hex[ch&15];}}
  return "nControl/signals/"+encoded+"/set";
}
inline bool parseSignal(const char* bytes,size_t length,SignalMessage& out) {
  if(!length||length>MaxSignalPayload)return false;
  DynamicJsonDocument d(4096);if(deserializeJson(d,bytes,length))return false;
  if(std::string(d["schema"]|"")!="ncontrol-signal-v1"||std::string(d["mode"]|"")!="operational"||!text(d["label"],96)||!text(d["head"],32)||!text(d["aspect"],96)||!integer(d["blink_period_ms"],200,5000))return false;
  const auto* profile=signalProfile(d["head"].as<const char*>());if(!profile)return false;
  int aspect=-1;for(int i=0;i<profile->aspectCount;++i)if(std::string(d["aspect"].as<const char*>())==profile->aspects[i])aspect=i;
  if(aspect<0)return false;
  for(const char* key:{"colors","roles","lamps"})if(!d[key].is<JsonArray>()||d[key].size()!=size_t(profile->count))return false;
  const char* effects[]={"off","on","blink"};
  for(int i=0;i<profile->count;++i)if(std::string(d["colors"][i]|"")!=profile->colors[i]||std::string(d["roles"][i]|"")!=profile->roles[i]||std::string(d["lamps"][i]|"")!=effects[profile->lamps[aspect][i]])return false;
  std::string wire="T|"+std::string(d["label"].as<const char*>())+"|"+d["aspect"].as<const char*>();Message message;if(!parse(wire.data(),wire.size(),message))return false;
  out.message=std::move(message);out.head=profile->head;out.period=d["blink_period_ms"];return true;
}
}

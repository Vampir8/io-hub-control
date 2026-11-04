#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cmath>
#include "protocol.h"
#include "signal_profiles.h"

namespace hub {
constexpr size_t JsonCapacity=65536, MaxConfigBytes=24576;
// Upper bound for the read-only pool, including ArduinoJson's string deduplication.
// Compare raw quoted tokens without allocating a second dictionary. Different
// escape spellings may overestimate; decoding can never require more bytes.
inline size_t jsonCapacityFor(const uint8_t* bytes,size_t length,size_t reserve=256) {
  const auto endToken=[&](size_t start){
    size_t j=start+1;
    while(j<length){if(bytes[j]=='\\'){j+=2;}else if(bytes[j]=='"')return j;else ++j;}
    return length;
  };
  size_t slots=0,strings=0;
  for(size_t i=0;i<length;++i){
    if(bytes[i]=='"'){
      const size_t end=endToken(i);bool duplicate=false;
      for(size_t j=0;j<i;++j)if(bytes[j]=='"'){
        const size_t previousEnd=endToken(j);
        if(end-i==previousEnd-j && memcmp(bytes+i,bytes+j,end-i)==0){duplicate=true;break;}
        j=previousEnd;
      }
      if(!duplicate)strings+=end-i;
      i=end;
    }else if(bytes[i]=='{'||bytes[i]=='['||bytes[i]==',')++slots;
  }
  return slots*JSON_ARRAY_SIZE(1)+strings+reserve;
}
// Read-only input forces ArduinoJson to own every string after an NVS buffer is released.
inline DeserializationError decodeConfig(DynamicJsonDocument& doc,const uint8_t* bytes,size_t length) {
  return deserializeJson(doc,bytes,length);
}
constexpr int Pins[]={0,1,2,3,4,5,6,7,8,9,10,20,21};
inline bool pinAllowed(int p) {for(int n:Pins)if(n==p)return true;return false;}
inline bool text(JsonVariantConst v,size_t max,bool empty=false) {
  if(!v.is<const char*>())return false;
  std::string s=v.as<const char*>();
  return s.size()<=max && (empty || !s.empty()) && (s.empty() || utf8(s));
}
inline bool number(JsonVariantConst v,double min,double max) {
  if(!v.is<double>() && !v.is<int>())return false;
  double n=v.as<double>();return std::isfinite(n)&&n>=min&&n<=max;
}
inline bool integer(JsonVariantConst v,int min,int max) {return v.is<int>()&&v.as<int>()>=min&&v.as<int>()<=max;}
inline bool oneOf(JsonVariantConst v,std::initializer_list<const char*> list) {
  if(!v.is<const char*>())return false;
  for(auto x:list)if(std::string(v.as<const char*>())==x)return true;
  return false;
}
inline std::string validatePorts(JsonVariantConst c);
inline std::string validate(JsonVariantConst c) {
  if(!c.is<JsonObjectConst>() || !integer(c["version"],1,2))return "JSON version 1 or 2 is required";
  if(!text(c["device"],32))return "Invalid device name";
  std::string dev=c["device"];
  for(char ch:dev)if(!((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'))return "mDNS name: lowercase letters, digits and hyphens";
  if(dev.front()=='-'||dev.back()=='-')return "Invalid mDNS name";
  if(!text(c["wifi"]["ssid"],32,true)||!text(c["wifi"]["password"],64,true))return "Invalid Wi-Fi settings";
  auto m=c["mqtt"];
  if(!m["enabled"].isNull()&&!m["enabled"].is<bool>())return "MQTT enabled must be a boolean";
  if(!text(m["host"],128,true)||!integer(m["port"],1,65535)||!text(m["clientId"],64)||!text(m["user"],64,true)||!text(m["password"],128,true))return "Invalid MQTT settings";
  if(!text(m["stateTopic"],128))return "Invalid state topic";
  auto topicOK=[](JsonVariantConst t){std::string s=t.as<const char*>();return s.find_first_of("+#")==std::string::npos;};
  if(!topicOK(m["stateTopic"]))return "State topic must be exact, without +/#";
  if(!text(c["modeTopic"],128,true))return "Invalid permission topic";
  if(!topicOK(c["modeTopic"]))return "Permission topic cannot contain +/#";
  if(std::string(c["modeTopic"].as<const char*>())==m["stateTopic"].as<const char*>())return "Mode and state topics must differ";
  for(auto k:{"uart0","externalJtag","allowStrapping"})if(!c[k].is<bool>())return "GPIO reservation settings are missing";
  if(c["version"].as<int>()==2)return validatePorts(c);
  for(auto k:{"servos","chains","groups","bindings"})if(!c[k].is<JsonArrayConst>())return "Configuration array is missing";
  auto servos=c["servos"].as<JsonArrayConst>(), chains=c["chains"].as<JsonArrayConst>(), groups=c["groups"].as<JsonArrayConst>(), bindings=c["bindings"].as<JsonArrayConst>();
  if(servos.size()>6)return "No LEDC resources: at most 6 servos";
  if(chains.size()>2)return "No RMT TX resources: at most 2 chains";
  if(groups.size()>32||bindings.size()>32)return "At most 32 groups and 32 bindings";
  std::set<int> pins,freqs; std::set<std::string> ids,servoIds,chainIds,groupIds;
  std::map<std::string,JsonVariantConst> servoMap,groupMap;std::map<std::string,int> counts;
  auto usePin=[&](JsonVariantConst o)->std::string {
    if(!integer(o["pin"],0,21))return "Invalid GPIO";
    int p=o["pin"];
    if(!pinAllowed(p))return "GPIO is outside the allowed list";
    if(!pins.insert(p).second)return "GPIO conflict: pin already has an owner";
    if((p==2||p==8||p==9)&&!c["allowStrapping"].as<bool>())return "Confirm use of strapping pins";
    if((p==20||p==21)&&c["uart0"].as<bool>())return "Disable UART0 before using GPIO20/21";
    if(p>=4&&p<=7&&c["externalJtag"].as<bool>())return "GPIO4–7 are reserved for external JTAG";
    return "";
  };
  auto named=[&](JsonVariantConst o)->bool {
    if(!text(o["id"],32)||!text(o["name"],64))return false;
    std::string id=o["id"];for(char ch:id)if(!isalnum((unsigned char)ch)&&ch!='-'&&ch!='_')return false;
    return ids.insert(id).second;
  };
  for(auto s:servos) {
    if(!named(s))return "Invalid or duplicate ID/name";
    auto err=usePin(s);if(!err.empty())return err;
    if(!integer(s["hz"],40,333)||!integer(s["minUs"],500,2500)||!integer(s["maxUs"],500,2500)||s["minUs"].as<int>()>=s["maxUs"].as<int>()||s["maxUs"].as<int>()>=1000000/s["hz"].as<int>())return "Invalid servo frequency/pulse range";
    if(!number(s["minAngle"],0,180)||!number(s["maxAngle"],0,180)||s["minAngle"].as<float>()>=s["maxAngle"].as<float>()||!number(s["speed"],1,360))return "Invalid servo angle limits/speed";
    if(!s["reverse"].is<bool>()||!s["detachAfter"].is<bool>()||!oneOf(s["boot"],{"none","safe"})||!oneOf(s["offline"],{"hold","safe","detach"})||!number(s["safeAngle"],s["minAngle"].as<double>(),s["maxAngle"].as<double>()))return "Invalid servo behavior";
    servoIds.insert(s["id"].as<const char*>());servoMap[s["id"].as<const char*>()]=s;freqs.insert(s["hz"].as<int>());
  }
  if(freqs.size()>4)return "No LEDC timers: at most 4 distinct frequencies";
  for(auto ch:chains) {
    if(!named(ch))return "Invalid chain ID/name";
    auto err=usePin(ch);if(!err.empty())return err;
    if(!integer(ch["count"],1,300)||!integer(ch["brightness"],0,255)||!oneOf(ch["type"],{"WS2812B","SK6812RGB","SK6812RGBW"}))return "Invalid LED chain (limit: 300)";
    bool w=std::string(ch["type"].as<const char*>())=="SK6812RGBW";
    if(w?!oneOf(ch["order"],{"RGBW","GRBW"}):!oneOf(ch["order"],{"RGB","GRB"}))return "Color order does not match LED type";
    std::string id=ch["id"];chainIds.insert(id);counts[id]=ch["count"];
  }
  std::map<std::string,std::set<int>> pixels;std::map<std::string,std::string> groupChains;
  for(auto g:groups) {
    if(!named(g)||!text(g["chain"],32)||!chainIds.count(g["chain"].as<const char*>()))return "Invalid group/chain";
    std::string id=g["id"],ch=g["chain"];groupIds.insert(id);groupMap[id]=g;groupChains[id]=ch;
    if(!g["pixels"].is<JsonArrayConst>()||g["pixels"].size()<1||g["pixels"].size()>300)return "LED index list is required";
    for(auto p:g["pixels"].as<JsonArrayConst>())if(!integer(p,0,counts[ch]-1)||!pixels[id].insert(p.as<int>()).second)return "LED index is out of range or duplicated";
    if(!g["priority"].isNull()&&!integer(g["priority"],0,255))return "Priority must be 0–255 or null";
  }
  for(auto a=groupIds.begin();a!=groupIds.end();++a)for(auto b=std::next(a);b!=groupIds.end();++b)if(groupChains[*a]==groupChains[*b]) {
    bool overlap=false;for(int p:pixels[*a])if(pixels[*b].count(p))overlap=true;
    if(overlap&&(groupMap[*a]["priority"].isNull()||groupMap[*b]["priority"].isNull()||groupMap[*a]["priority"].as<int>()==groupMap[*b]["priority"].as<int>()))return "Overlapping groups require explicit, different priorities";
  }
  std::map<std::string,std::string> owners;std::set<std::string> sources;
  size_t ruleCount=0,actionCount=0;
  for(auto b:bindings) {
    if(!named(b)||!b["enabled"].is<bool>()||!oneOf(b["type"],{"T","S"})||!text(b["source"],96)||!b["states"].is<JsonArrayConst>()||b["states"].size()<1)return "Invalid binding";
    std::string source=b["source"],type=b["type"],owner=b["id"];
    if(source.find('|')!=std::string::npos)return "The | separator is reserved";
    if(type=="S"){uint32_t n;if(!unsignedNumber(source,2147483647,n)||source!=std::to_string(n))return "S requires a canonical index from 0 to 2147483647";}
    if(b["enabled"].as<bool>()&&!sources.insert(type+"|"+source).second)return "Source already has an active binding; add actions to it";
    std::set<std::string> states;
    for(auto state:b["states"].as<JsonArrayConst>()) {
      if(++ruleCount>128)return "At most 128 states";
      if(!text(state["value"],96)||!states.insert(state["value"].as<const char*>()).second||!state["actions"].is<JsonArrayConst>()||state["actions"].size()<1)return "Invalid or duplicate state";
      Message parsed;std::string wire=type+"|"+source+"|"+state["value"].as<const char*>();if(!parse(wire.data(),wire.size(),parsed))return "State does not match the T/S parser";
      std::set<std::string> targets;
      for(auto a:state["actions"].as<JsonArrayConst>()) {
        if(++actionCount>256)return "At most 256 actions";
        if(!oneOf(a["kind"],{"servo","led"})||!text(a["target"],32))return "Invalid action";
        std::string target=a["target"],kind=a["kind"];
        if(!targets.insert(target).second)return "Duplicate target within one state";
        if(kind=="servo") {
          if(!servoIds.count(target))return "Action references a missing servo";
          auto s=servoMap[target];if(!number(a["angle"],s["minAngle"].as<double>(),s["maxAngle"].as<double>()))return "Action angle exceeds servo limits";
        }else {
          if(!groupIds.count(target)||!oneOf(a["effect"],{"off","solid","blink","alternate"})||!a["color"].is<JsonArrayConst>()||a["color"].size()!=4)return "Invalid LED action";
          for(auto x:a["color"].as<JsonArrayConst>())if(!integer(x,0,255))return "Color requires 4 components from 0 to 255";
          if(!integer(a["brightness"],0,255)||!integer(a["onMs"],20,60000)||!integer(a["offMs"],20,60000))return "Invalid LED brightness/timing";
          if(std::string(a["effect"].as<const char*>())=="alternate") {
            if(!text(a["other"],32))return "Select the second crossing group";
            std::string other=a["other"];
            if(!groupIds.count(other)||other==target||!targets.insert(other).second)return "Invalid second group";
            if(b["enabled"].as<bool>()) {if(owners.count(other)&&owners[other]!=owner)return "Group already has an owner";owners[other]=owner;}
          }
        }
        if(b["enabled"].as<bool>()){if(owners.count(target)&&owners[target]!=owner)return "Target already has an active owner";owners[target]=owner;}
      }
    }
  }
  return "";
}
#include "ports_config.h"
}

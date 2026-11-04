#include "config.h"
#include "public_config.h"
#include <iostream>
#include <fstream>
#include <iterator>
#include <cstdlib>
using namespace hub;
int checks=0;
void check(bool yes,const char* name){++checks;if(!yes){std::cerr<<"FAIL: "<<name<<"\n";std::exit(1);}}
int main(int argc,char** argv){
  check(argc==2,"example argument");
  std::ifstream f(argv[1]);std::string json((std::istreambuf_iterator<char>(f)),{});
  DynamicJsonDocument base(JsonCapacity),c(JsonCapacity);check(!deserializeJson(base,json),"example JSON");
  check(validate(base.as<JsonVariantConst>()).empty(),"valid example");
  std::vector<uint8_t> nvsBuffer(json.begin(),json.end());
  check(!decodeConfig(c,nvsBuffer.data(),nvsBuffer.size()),"NVS decode");
  std::fill(nvsBuffer.begin(),nvsBuffer.end(),'X');
  check(validate(c.as<JsonVariantConst>()).empty(),"NVS strings survive overwritten temporary buffer");
  std::string expectedDevice=base["device"].as<const char*>();
  check(std::string(c["device"].as<const char*>())==expectedDevice,"NVS device string is owned by document");
  for(const std::string sample:{json,std::string(R"({"a":[[],{},1,2,"przecinek, nawias[ i {", "cytat\" i \\", "Łódź", "\u0141\u00f3d\u017a"]})"),std::string("[]"),std::string("{}")}){
    const size_t budget=jsonCapacityFor(reinterpret_cast<const uint8_t*>(sample.data()),sample.size(),512);
    DynamicJsonDocument sized(budget);
    check(!decodeConfig(sized,reinterpret_cast<const uint8_t*>(sample.data()),sample.size()),"payload-sized pool accepts nested/escaped/UTF-8 JSON");
    check(!sized.overflowed()&&sized.memoryUsage()<=budget,"payload-sized pool covers actual ArduinoJson allocation");
  }
  Message m;
  {
    DynamicJsonDocument source(1024),result(1024);
    deserializeJson(source,R"({"wifi":{"password":"wifi-secret","ssid":"Łódź"},"mqtt":{"password":"mqtt-secret","host":"pc"},"quote\"\\\n":"value"})");
    std::string output;publicConfig(source.as<JsonObjectConst>(),output);
    check(!deserializeJson(result,output),"redacted config remains valid JSON including escaped keys");
    check(result["wifi"]["password"].isNull()&&result["mqtt"]["password"].isNull(),"public config hides both passwords");
    check(std::string(source["wifi"]["password"].as<const char*>())=="wifi-secret","redaction never mutates live secrets");
    check(std::string(result["wifi"]["ssid"].as<const char*>())=="Łódź"&&result["quote\"\\\n"]=="value","redaction preserves other fields and UTF-8");
  }
  {
    std::string repeated="[";
    for(int i=0;i<100;++i){if(i)repeated+=",";repeated+=R"({"effect":"blink","color":"#ff0000","escaped":"a\"b"})";}
    repeated+="]";
    const size_t budget=jsonCapacityFor(reinterpret_cast<const uint8_t*>(repeated.data()),repeated.size(),512);
    DynamicJsonDocument sized(budget);
    check(!decodeConfig(sized,reinterpret_cast<const uint8_t*>(repeated.data()),repeated.size()),"deduplicated pool parses repeated signal definitions");
    check(budget<401*JSON_ARRAY_SIZE(1)+1024,"repeated keys/colors do not reserve repeated string storage");
  }
  for(auto wire:{"T|Turnout1|s","T|ThreeWay|l","T|Signal1|gf","T|Train1|l,1","T|Train1|p,50","S|12|0","S|12|-1","T|Łódź Żółć|rf"})check(parse(wire,strlen(wire),m),wire);
  std::string train="T|Train1|p,50";check(parse(train.data(),train.size(),m)&&m.state=="p,50","preserve commas, no NUL required");
  const char raw[]={'T','|','A','|','r','X'};check(parse(raw,5,m)&&m.state=="r","length-limited callback");
  for(auto bad:{"","T","T|A","T||s","T|A|","S|-1|0","S|2147483648|1","S|x|1","S|12|p,50","S|12|2147483648","D!!!","B","CIR","X|A|s","T|A|r|junk","T|A|r\n","T|A|\xC0\xAF"})check(!parse(bad,strlen(bad),m),bad);
  std::string large(385,'a');check(!parse(large.data(),large.size(),m),"payload bound");
  check(parse("T|A|unknown",11,m)&&m.state=="unknown","unknown state is valid parser input, no guessing");
  Message a,b;parse("T|A|gf",6,a);parse("T|A|gf",6,b);check(sameState(a,b),"duplicate blink state");parse("T|A|rf",6,b);check(!sameState(a,b),"changed state");
  SyncSession sync;sync.connected(7);check(!sync.ack(8,1),"ignore another SUBACK");check(sync.ack(7,1),"first synchronization");check(!sync.ack(7,1),"one synchronization per session");sync.disconnected();check(!sync.ack(7,1),"offline SUBACK");sync.connected(9);check(!sync.ack(9,0x80),"rejected SUBACK");check(sync.ack(9,1),"synchronize after reconnect");
  auto reset=[&]{c.clear();c.set(base);};
  auto invalid=[&](const char* name){check(!validate(c.as<JsonVariantConst>()).empty(),name);};
  reset();c["chains"][0]["pin"]=3;invalid("GPIO conflict");
  reset();c["servos"][0]["pin"]=18;invalid("USB GPIO forbidden");
  reset();c["servos"][0]["pin"]=8;invalid("strap opt-in");
  reset();c["servos"][0]["pin"]=20;c["uart0"]=true;invalid("UART0 reserved");
  reset();c["servos"][0]["pin"]=4;invalid("JTAG reserved");
  reset();for(int i=0;i<6;++i)c["servos"].as<JsonArray>().add(base["servos"][0]);invalid("LEDC channel exhaustion");
  reset();c["chains"].as<JsonArray>().add(base["chains"][0]);c["chains"].as<JsonArray>().add(base["chains"][0]);invalid("RMT TX exhaustion");
  reset();c["servos"].clear();for(int i=0;i<5;++i){auto s=c["servos"].as<JsonArray>().createNestedObject();s.set(base["servos"][0]);s["id"]="servo"+std::to_string(i);s["pin"]=i==4?6:i;s["hz"]=50+i;}c["externalJtag"]=false;c["allowStrapping"]=true;invalid("LEDC timer exhaustion");
  reset();c["groups"][1]["pixels"][0]=1;invalid("overlap without priority");c["groups"][0]["priority"]=1;c["groups"][1]["priority"]=2;check(validate(c.as<JsonVariantConst>()).empty(),"explicit overlap priority");
  reset();c["groups"][0]["pixels"][0]=10;invalid("LED index bound");
  reset();c["servos"][0]["minUs"]=2200;invalid("pulse bounds");
  reset();c["bindings"][0]["states"][0]["actions"][0]["angle"]=180;invalid("action angle limits");
  reset();c["bindings"][1]["states"][0]["actions"].as<JsonArray>().add(base["bindings"][0]["states"][0]["actions"][0]);invalid("active servo ownership");
  reset();c["chains"][0]["order"]="RGBW";invalid("RGB/RGBW mismatch");
  reset();c["bindings"][0]["type"]="S";c["bindings"][0]["source"]="12";c["bindings"][0]["states"][0]["value"]="0";c["bindings"][0]["states"][1]["value"]="1";check(validate(c.as<JsonVariantConst>()).empty(),"custom numeric S mapping");
  reset();c["bindings"][0]["states"][0]["value"]="nowy";check(validate(c.as<JsonVariantConst>()).empty(),"custom exact state permitted");
  std::cout<<"PASS "<<checks<<" checks; no hardware outputs used\n";
}

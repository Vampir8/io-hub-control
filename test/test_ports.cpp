#include "outputs.h"
#include "store.h"
#include "signal_message.h"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace hub;
uint32_t fakeMillis=0,duties[6]={};int writes=0,failLedc=-1,failRmt=-1;
std::vector<uint8_t> frames[2];bool rmtInstalled[2]={},rmtBusy[2]={};
int checks=0;
void check(bool v,const char* label){++checks;if(!v){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
void load(DynamicJsonDocument& cfg,const std::string& path){std::ifstream f(path);std::string json((std::istreambuf_iterator<char>(f)),{});const size_t budget=jsonCapacityFor(reinterpret_cast<const uint8_t*>(json.data()),json.size(),512);DynamicJsonDocument sized(budget);check(!decodeConfig(sized,reinterpret_cast<const uint8_t*>(json.data()),json.size()),"payload-sized pool parses real full template libraries");check(validate(sized.as<JsonVariantConst>()).empty(),"payload-sized document validates independently");cfg.clear();check(!deserializeJson(cfg,json),"parse v2 in firmware capacity");check(validate(cfg.as<JsonVariantConst>()).empty(),"validate v2");}
int main(int argc,char** argv){
  check(argc==2,"example directory");std::string dir=argv[1];DynamicJsonDocument cfg(JsonCapacity),bad(JsonCapacity);load(cfg,dir+"/ports-signals.json");
  Outputs out;check(out.begin(cfg.as<JsonVariantConst>())&&out.chains.size()==10,"10 signal resources on two RMT slots");
  Message msg;parse("T|Signal1|S3",strlen("T|Signal1|S3"),msg);auto state=portState(cfg.as<JsonVariantConst>(),cfg["ports"][0],msg);check(!state.isNull(),"exact source and state");
  for(auto p:cfg["ports"].as<JsonArrayConst>()){auto t=findTemplate(cfg.as<JsonVariantConst>(),p["template"].as<const char*>());out.portAction(p["id"].as<const char*>(),t["states"][2],false);}
  for(fakeMillis=0;fakeMillis<80;fakeMillis+=2)out.tick();
  check(delivered.size()==10,"round robin reaches all ten GPIO");for(auto p:cfg["ports"].as<JsonArrayConst>())check(delivered[p["pin"]][0]==55,"each independent green pixel ON");
  rmtBusy[0]=true;rmtBusy[1]=true;auto frozen=delivered;fakeMillis=600;out.tick();check(delivered==frozen,"in-flight buffers untouched");rmtBusy[0]=rmtBusy[1]=false;
  for(;fakeMillis<680;fakeMillis+=2)out.tick();for(auto p:cfg["ports"].as<JsonArrayConst>())check(delivered[p["pin"]][0]==0,"all blink OFF after own epoch");
  auto epoch=out.chains[0].epoch;for(;fakeMillis<730;fakeMillis+=2)out.tick();check(out.chains[0].epoch==epoch,"render loop does not restart blink");
  auto same=findTemplate(cfg.as<JsonVariantConst>(),"nc-5-GYRYW")["states"][2];out.portAction(out.chains[0].id,same,false);check(out.chains[0].epoch==epoch,"repeated action does not restart blink even after unknown source state");
  bad.set(cfg);bad["ports"][1]["pin"]=bad["ports"][0]["pin"];check(!validate(bad.as<JsonVariantConst>()).empty(),"GPIO conflict");
  bad.set(cfg);bad["ports"].as<JsonArray>().add(cfg["ports"][0]);check(!validate(bad.as<JsonVariantConst>()).empty(),"eleventh device rejected");
  bad.set(cfg);bad["templates"][0]["states"][0]["pixels"][0]["onMs"]=0;check(!validate(bad.as<JsonVariantConst>()).empty(),"invalid shared template rejected");
  out.stopAll();check(!rmtInstalled[0]&&!rmtInstalled[1],"RMT released");
  load(cfg,dir+"/ports-servos.json");check(out.begin(cfg.as<JsonVariantConst>())&&out.servos.size()==10,"six LEDC plus four timer servos");check(!out.servos[9].active,"tenth servo no implicit pulse");out.move(out.servos[9],60);check(out.servos[9].active&&out.servos[9].pos==60,"explicit tenth servo movement");
  out.move(out.servos[9],120);fakeMillis+=50;out.tick();check(out.servos[9].pos>60&&out.servos[9].pos<63,"tenth servo smooth movement");out.servos[9].calibration=true;auto t=findTemplate(cfg.as<JsonVariantConst>(),"turnout");out.portAction(out.servos[9].id,t["states"][0],true);check(out.servos[9].target==120,"tenth servo calibration wins");
  bad.set(cfg);bad["ports"][9]["servo"]["hz"]=60;check(!validate(bad.as<JsonVariantConst>()).empty(),"software PWM 50 Hz enforced");
  Store store;DynamicJsonDocument restored(JsonCapacity);check(!store.begin(restored),"fresh NVS");check(store.save(cfg.as<JsonVariantConst>()),"save ten-servo config");Store reboot;check(reboot.begin(restored)&&restored["ports"].size()==10,"ten ports persist across reboot");
  out.stopAll();load(cfg,dir+"/ports-mixed.json");check(out.begin(cfg.as<JsonVariantConst>())&&out.servos.size()==1&&out.chains.size()==1,"mixed ports");
  ExtraServos extra;for(int i=0;i<4;++i)extra.pin(i,i+4);extra.begin();
  auto e=extra.simulateInterrupt(100);check(e.rise==0&&e.next==20100,"no startup pulses in software timer");
  extra.pulse(0,1000);extra.pulse(1,1500);extra.pulse(2,1500);extra.pulse(3,2000);
  e=extra.simulateInterrupt(20100);check(e.rise==0xf0&&e.next==21100,"four rising edges and earliest falling deadline");
  e=extra.simulateInterrupt(21100);check(e.fall==0x10&&e.next==21600,"1000 us falling edge");
  e=extra.simulateInterrupt(21600);check(e.fall==0x60&&e.next==22100,"equal pulse widths share falling edge");
  e=extra.simulateInterrupt(22100);check(e.fall==0x80&&e.next==40100,"2000 us edge followed by frame remainder");
  for(int i=0;i<4;++i)extra.pulse(i,0);e=extra.simulateInterrupt(23000);check(!e.rise&&e.next==40100,"detachment cannot start an early frame");
  extra.stop();
  DynamicJsonDocument small(1024),staged(JsonCapacity);small["version"]=2;small.shrinkToFit();staged.set(cfg);small=std::move(staged);small.shrinkToFit();check(staged.capacity()==0&&!small.overflowed()&&validate(small.as<JsonVariantConst>()).empty(),"configuration ownership transfers without a second large allocation before output binding");

  std::ifstream signalsFile(dir+"/ncontrol-signals.json");std::string signalsJson((std::istreambuf_iterator<char>(signalsFile)),{});DynamicJsonDocument fixtures(98304);check(!deserializeJson(fixtures,signalsJson),"load real nControl fixtures");
  for(auto fixture:fixtures.as<JsonArray>()){
    std::string wire;serializeJson(fixture,wire);SignalMessage signal;check(parseSignal(wire.data(),wire.size(),signal),"every native nControl head and aspect accepted");
  }
  DynamicJsonDocument signalDoc(4096);signalDoc.set(fixtures[0]);std::string wire;SignalMessage signal;
  signalDoc["mode"]="simulation";serializeJson(signalDoc,wire);check(!parseSignal(wire.data(),wire.size(),signal),"simulation JSON cannot drive lamps");
  signalDoc.set(fixtures[0]);signalDoc["roles"][0]="red";wire.clear();serializeJson(signalDoc,wire);check(!parseSignal(wire.data(),wire.size(),signal),"invalid role order rejected");
  signalDoc.set(fixtures[0]);signalDoc["lamps"][0]="blink";wire.clear();serializeJson(signalDoc,wire);check(!parseSignal(wire.data(),wire.size(),signal),"lamps must match actual profile aspect");
  signalDoc.set(fixtures[0]);signalDoc["blink_period_ms"]=10;wire.clear();serializeJson(signalDoc,wire);check(!parseSignal(wire.data(),wire.size(),signal),"invalid JSON blink period rejected");
  DynamicJsonDocument p(512);p["source"]="Signal Łódź 1";check(signalTopic(p.as<JsonVariantConst>())=="nControl/signals/Signal%20%C5%81%C3%B3d%C5%BA%201/set","UTF8 URL-encoded default signal topic");
  bad.set(cfg);bad["mqtt"]["enabled"]=false;check(validate(bad.as<JsonVariantConst>()).empty(),"MQTT can be disabled without removing broker settings");
  Store offlineStore;offlineStore.begin(restored);check(offlineStore.save(bad.as<JsonVariantConst>()),"save disabled MQTT configuration");Store offlineReboot;check(offlineReboot.begin(restored)&&restored["mqtt"]["enabled"].is<bool>()&&!restored["mqtt"]["enabled"].as<bool>(),"MQTT remains disabled after reboot");
  bad["mqtt"]["enabled"]="false";check(!validate(bad.as<JsonVariantConst>()).empty(),"MQTT enable flag must be boolean");
  std::cout<<"PASS "<<checks<<" port/template/scheduler checks; drivers mocked\n";
}

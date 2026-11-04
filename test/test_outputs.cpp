#include "outputs.h"
#include <iostream>
#include <fstream>
#include <iterator>
using namespace hub;
uint32_t fakeMillis=0,duties[6]={};int writes=0,failLedc=-1,failRmt=-1;
std::vector<uint8_t> frames[2];bool rmtInstalled[2]={},rmtBusy[2]={};
int checks=0;
void check(bool x,const char* name){++checks;if(!x){std::cerr<<"FAIL: "<<name<<"\n";exit(1);}}
int main(int argc,char**argv){
  std::ifstream f(argv[1]);std::string json((std::istreambuf_iterator<char>(f)),{});DynamicJsonDocument cfg(JsonCapacity);check(!deserializeJson(cfg,json),"JSON");
  Outputs out;check(out.begin(cfg.as<JsonVariantConst>()),"allocate LEDC/RMT");check(writes==0&&duties[0]==0,"configuration emits no servo PWM");
  auto& servo=out.servos[0];check(!servo.known&&!servo.active,"boot unknown, detached");out.move(servo,60);check(servo.pos==60&&servo.active,"first explicit position");
  out.move(servo,120);fakeMillis+=50;out.tick();check(fabs(servo.pos-61.5)<.01,"30 degrees/s interpolation");
  out.move(servo,120);fakeMillis+=50;out.tick();check(fabs(servo.pos-63)<.01,"duplicate target keeps progress");
  out.freeze(false);check(servo.target==servo.pos&&!servo.moving,"freeze at last commanded point");
  cfg["servos"][0]["detachAfter"]=true;out.move(servo,64);fakeMillis+=50;out.tick();check(servo.pos==64&&servo.active,"arrival retains PWM briefly");fakeMillis+=99;out.tick();check(servo.active,"PWM retention 99 ms");fakeMillis+=1;out.tick();check(!servo.active&&duties[0]==0,"detach at 100 ms");
  cfg["servos"][0]["detachAfter"]=false;out.move(servo,100);fakeMillis+=50;out.tick();float held=servo.pos;out.freeze(true);check(servo.target==held&&!servo.moving&&servo.active,"offline hold policy");
  cfg["servos"][0]["offline"]="detach";out.freeze(true);check(!servo.active,"offline detach policy");cfg["servos"][0]["offline"]="safe";out.freeze(true);check(servo.target==90&&servo.moving,"explicit safe policy");
  servo.calibration=true;out.action(cfg["bindings"][0]["states"][0]["actions"][0],true);check(servo.target==90,"calibration wins over MQTT action");servo.calibration=false;
  out.action(cfg["bindings"][0]["states"][0]["actions"][0],false);check(servo.target==90,"closed motion gate");
  out.action(cfg["bindings"][1]["states"][0]["actions"][0],false);out.action(cfg["bindings"][0]["states"][0]["actions"][1],false);fakeMillis+=30;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][0]==64&&frames[0][9]==64,"GRB brightness and independent groups preserved");
  out.action(cfg["bindings"][1]["states"][4]["actions"][0],false);fakeMillis+=30;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][0]==0&&frames[0][9]==64,"OFF group preserves other pixels");
  out.action(cfg["bindings"][1]["states"][2]["actions"][0],false);fakeMillis+=30;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][0]==64,"blink ON");fakeMillis+=500;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][0]==0&&frames[0][9]==64,"blink OFF without clearing route");
  DynamicJsonDocument a(512);a["kind"]="led";a["target"]="semafor1";a["other"]="trasa1";a["effect"]="alternate";a["brightness"]=255;a["onMs"]=500;a["offMs"]=500;auto color=a.createNestedArray("color");for(int n:{255,0,0,0})color.add(n);out.action(a.as<JsonVariantConst>(),false);fakeMillis+=30;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][1]==64&&frames[0][10]==0,"alternate first group");fakeMillis+=500;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0][1]==0&&frames[0][10]==64,"alternate second group");
  auto before=frames[0];rmtBusy[0]=true;fakeMillis+=500;out.tick();fakeMillis+=1;out.tick();fakeMillis+=1;out.tick();check(frames[0]==before,"never touch in-flight RMT buffer");rmtBusy[0]=false;
  out.stopAll();check(out.servos.empty()&&out.chains.empty()&&!rmtInstalled[0],"release resources");
  failRmt=0;check(!out.begin(cfg.as<JsonVariantConst>())&&!out.error.empty()&&out.servos.empty(),"RMT allocation error stops all outputs");failRmt=-1;failLedc=0;check(!out.begin(cfg.as<JsonVariantConst>())&&!out.error.empty(),"LEDC allocation error");
  uint8_t b=0x80;rmt_item32_t items[8];size_t used=0,n=0;translateWS(&b,items,1,8,&used,&n);check(used==1&&n==8&&items[0].duration0==32&&items[1].duration0==16,"WS bit encoding");translateSK(&b,items,1,8,&used,&n);check(items[0].duration0==24&&items[1].duration0==12&&items[1].duration0+items[1].duration1==50,"SK bit encoding");
  std::cout<<"PASS "<<checks<<" output checks with mocked GPIO/LEDC/RMT; no hardware used\n";
}

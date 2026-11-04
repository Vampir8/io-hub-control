#include "store.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
using namespace hub;
int checks=0;
void check(bool v,const char* label){++checks;if(!v){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
int main(int argc,char** argv){
  check(argc==2,"example argument");
  std::ifstream f(argv[1]);std::string json((std::istreambuf_iterator<char>(f)),{});
  DynamicJsonDocument config(JsonCapacity),loaded(JsonCapacity);
  check(!deserializeJson(config,json),"parse config");
  Store s;check(!s.begin(loaded),"empty storage");
  check(s.save(config.as<JsonVariantConst>()),"first save");
  config["device"]="second";
  check(s.save(config.as<JsonVariantConst>()),"second save");
  check(s.backup(loaded)&&std::string(loaded["device"].as<const char*>())!="second","backup retained");
  Preferences::failWrite=true;check(!s.save(config.as<JsonVariantConst>()),"failed write rejected");Preferences::failWrite=false;
  Preferences::corruptWrite=true;config["device"]="third";
  check(!s.save(config.as<JsonVariantConst>()),"corrupt readback rejected");Preferences::corruptWrite=false;
  Store reboot;check(reboot.begin(loaded)&&std::string(loaded["device"].as<const char*>())=="second","previous valid slot survives failed save and reboot");
  check(reboot.save(config.as<JsonVariantConst>()),"retry succeeds");
  Store finalBoot;check(finalBoot.begin(loaded)&&std::string(loaded["device"].as<const char*>())=="third","saved config survives reboot");
  std::cout<<"PASS "<<checks<<" storage checks\n";
}

#pragma once
// Included by config.h inside namespace hub, after shared validation helpers.
inline JsonVariantConst findTemplate(JsonVariantConst c,const std::string& id) {
  for(auto t:c["templates"].as<JsonArrayConst>())if(id==t["id"].as<const char*>())return t;
  return JsonVariantConst();
}
inline JsonVariantConst portState(JsonVariantConst c,JsonVariantConst p,const Message& msg) {
  if(!p["enabled"].as<bool>()||msg.type!=p["type"].as<const char*>()[0]||msg.source!=p["source"].as<const char*>())return JsonVariantConst();
  auto t=findTemplate(c,p["template"].as<const char*>());
  for(auto s:t["states"].as<JsonArrayConst>())if(msg.state==s["value"].as<const char*>())return s;
  return JsonVariantConst();
}
inline std::string validatePorts(JsonVariantConst c) {
  if(!c["ports"].is<JsonArrayConst>()||c["ports"].size()>10||!c["templates"].is<JsonArrayConst>()||c["templates"].size()>24)return "At most 10 ports and 24 templates";
  std::set<std::string> ids;size_t states=0,cells=0;
  for(auto t:c["templates"].as<JsonArrayConst>()) {
    if(!text(t["id"],32)||!text(t["name"],64)||!ids.insert(t["id"].as<const char*>()).second||!oneOf(t["kind"],{"signal","servo"}))return "Invalid template ID/name/type";
    if(!t["states"].is<JsonArrayConst>()||t["states"].size()<1)return "Template requires states";
    bool signal=std::string(t["kind"].as<const char*>())=="signal";
    if(signal&&!integer(t["count"],1,5))return "Signal: 1 to 5 lamps";
    if(!t["head"].isNull()){if(!text(t["head"],32))return "Invalid nControl signal head";auto profile=signalProfile(t["head"].as<const char*>());if(!profile||t["count"].as<int>()!=profile->count)return "Lamp count must match the nControl signal head";}
    std::set<std::string> values;
    for(auto s:t["states"].as<JsonArrayConst>()) {
      if(++states>128||!text(s["value"],96)||!values.insert(s["value"].as<const char*>()).second)return "Invalid/duplicate state or more than 128 states";
      Message m;std::string wire="T|test|"+std::string(s["value"].as<const char*>());if(!parse(wire.data(),wire.size(),m))return "Invalid state code";
      if(!signal){if(!number(s["angle"],0,180))return "Invalid template angle";continue;}
      if(!s["pixels"].is<JsonArrayConst>()||s["pixels"].size()!=t["count"].as<size_t>())return "Every state must define all lamps";
      for(auto a:s["pixels"].as<JsonArrayConst>()) {
        if(++cells>320||!oneOf(a["effect"],{"off","solid","blink"})||!a["color"].is<JsonArrayConst>()||a["color"].size()!=4)return "Invalid lamp or more than 320 lamp definitions";
        for(auto x:a["color"].as<JsonArrayConst>())if(!integer(x,0,255))return "RGBW components: 0–255";
        if((!a["brightness"].isNull()&&!integer(a["brightness"],0,255))||(!a["onMs"].isNull()&&!integer(a["onMs"],20,60000))||(!a["offMs"].isNull()&&!integer(a["offMs"],20,60000))||(!a["phaseMs"].isNull()&&!integer(a["phaseMs"],0,120000)))return "Invalid lamp timing/brightness";
      }
    }
  }
  std::set<int> pins,frequencies;ids.clear();int servoCount=0;
  if(!c["ioPins"].isNull()){
    if(!c["ioPins"].is<JsonArrayConst>()||c["ioPins"].size()!=10)return "Exactly 10 I/O port GPIO assignments are required";
    std::set<int> layoutPins;
    for(auto pin:c["ioPins"].as<JsonArrayConst>())if(!integer(pin,0,21)||!pinAllowed(pin)||!layoutPins.insert(pin.as<int>()).second)return "Invalid/duplicate GPIO port map";
  }
  std::set<int> slots;
  for(auto p:c["ports"].as<JsonArrayConst>()) {
    if(!text(p["id"],32)||!text(p["name"],64)||!ids.insert(p["id"].as<const char*>()).second||!p["enabled"].is<bool>())return "Invalid/duplicate port";
    if(!integer(p["pin"],0,21)||!pinAllowed(p["pin"])||!pins.insert(p["pin"].as<int>()).second)return "GPIO conflict or disallowed GPIO";
    int pin=p["pin"];
    if(!c["ioPins"].isNull()&&(!integer(p["slot"],0,9)||!slots.insert(p["slot"].as<int>()).second||c["ioPins"][p["slot"].as<int>()].as<int>()!=pin))return "I/O port conflict or inconsistent GPIO map";
    if((pin==2||pin==8||pin==9)&&!c["allowStrapping"].as<bool>())return "Confirm use of GPIO2/8/9";
    if((pin==20||pin==21)&&c["uart0"].as<bool>())return "Disable UART0 for GPIO20/21";
    if(pin>=4&&pin<=7&&c["externalJtag"].as<bool>())return "Disable external JTAG for GPIO4–7";
    if(!text(p["template"],32)||!text(p["source"],96)||!oneOf(p["type"],{"T","S"}))return "Select a template and port source";
    auto t=findTemplate(c,p["template"].as<const char*>());if(t.isNull())return "Port references a missing template";
    for(auto st:t["states"].as<JsonArrayConst>()) {
      Message m;std::string wire=std::string(p["type"].as<const char*>())+"|"+p["source"].as<const char*>()+"|"+st["value"].as<const char*>();
      if(!parse(wire.data(),wire.size(),m))return "Source/state does not match T/S; use numeric state codes for S";
      if(m.type=='S'&&m.source!=p["source"].as<const char*>())return "S index must be canonical";
    }
    if(std::string(t["kind"].as<const char*>())=="signal") {
      auto w=p["ws"];
      if(!w["input"].isNull()&&!oneOf(w["input"],{"json","states"}))return "Invalid signal protocol";
      if(!w["topic"].isNull()&&(!text(w["topic"],128,true)||std::string(w["topic"].as<const char*>()).find_first_of("+#")!=std::string::npos))return "Invalid custom signal topic";
      if(!t["head"].isNull()&&std::string(w["input"]|"json")=="json"&&std::string(p["type"].as<const char*>())!="T")return "Signal JSON requires a T label; select advanced T/S for S";
      if(!oneOf(w["type"],{"WS2812B","SK6812RGB","SK6812RGBW"})||!integer(w["brightness"],0,255))return "Invalid WS type/brightness";
      bool rgbw=std::string(w["type"].as<const char*>())=="SK6812RGBW";
      if(rgbw?!oneOf(w["order"],{"RGBW","GRBW"}):!oneOf(w["order"],{"RGB","GRB"}))return "Color order does not match WS type";
    } else {
      auto s=p["servo"];++servoCount;
      if(!integer(s["hz"],40,333)||!integer(s["minUs"],500,2500)||!integer(s["maxUs"],500,2500)||s["minUs"].as<int>()>=s["maxUs"].as<int>()||s["maxUs"].as<int>()>=1000000/s["hz"].as<int>())return "Invalid servo pulses/frequency";
      if(servoCount<=6)frequencies.insert(s["hz"].as<int>());else if(s["hz"].as<int>()!=50)return "Servos 7–10 use software timer PWM: 50 Hz is required";
      if(frequencies.size()>4)return "First 6 servos: at most 4 LEDC frequencies";
      if(!number(s["minAngle"],0,180)||!number(s["maxAngle"],0,180)||s["minAngle"].as<float>()>=s["maxAngle"].as<float>()||!number(s["speed"],1,360)||!s["reverse"].is<bool>()||!s["detachAfter"].is<bool>()||!oneOf(s["boot"],{"none","safe"})||!oneOf(s["offline"],{"hold","safe","detach"})||!number(s["safeAngle"],s["minAngle"].as<double>(),s["maxAngle"].as<double>()))return "Invalid servo limits/behavior";
      for(auto st:t["states"].as<JsonArrayConst>())if(!number(st["angle"],s["minAngle"].as<double>(),s["maxAngle"].as<double>()))return "Template angle exceeds an assigned servo limit";
    }
  }
  return "";
}

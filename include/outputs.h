#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <driver/ledc.h>
#include <driver/rmt.h>
#include <driver/gpio.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "servo_extra.h"
#ifndef HUB_HOST_TEST
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#endif

namespace hub {
inline void releaseRmtPin(int pin) {
#ifndef HUB_HOST_TEST
  esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
#endif
  gpio_set_level((gpio_num_t)pin,0);gpio_set_direction((gpio_num_t)pin,GPIO_MODE_OUTPUT);
}
struct ServoRuntime {
  JsonVariantConst cfg; std::string id;int pin=0,channel=0,timer=0; float pos=90,target=90;
  bool known=false,active=false,moving=false,calibration=false; uint32_t updated=0,releaseAt=0;
};
struct ChainRuntime {
  JsonVariantConst cfg,state;std::string id;int pin=0,count=0,channel=-1;uint32_t epoch=0,periodOverride=0; bool installed=false;
  std::vector<uint8_t> data; uint32_t sent=0;
};
struct GroupRuntime {JsonVariantConst cfg; JsonVariantConst action; uint32_t epoch=0; bool inverse=false;};
// RMT APB 80 MHz / 2 = 40 MHz. Each bit is 1.25 us.
inline void IRAM_ATTR translateBits(const void* src,rmt_item32_t* dest,size_t bytes,size_t wanted,size_t* consumed,size_t* produced,bool sk) {
  const uint8_t* p=static_cast<const uint8_t*>(src);size_t n=0,j=0;
  while(n<bytes && j+8<=wanted) {
    uint8_t value=p[n++];
    for(int bit=7;bit>=0;--bit) {
      bool one=value&(1<<bit);rmt_item32_t item={};item.level0=1;item.level1=0;
      item.duration0=sk?(one?24:12):(one?32:16);item.duration1=50-item.duration0;
      dest[j++]=item;
    }
  }
  *consumed=n;*produced=j;
}
inline void IRAM_ATTR translateWS(const void* s,rmt_item32_t* d,size_t n,size_t w,size_t* c,size_t* p){translateBits(s,d,n,w,c,p,false);}
inline void IRAM_ATTR translateSK(const void* s,rmt_item32_t* d,size_t n,size_t w,size_t* c,size_t* p){translateBits(s,d,n,w,c,p,true);}
class Outputs {
  ExtraServos extra;bool portsMode=false;int owner[2]={-1,-1},cursor[2]={0,1};bool cooling[2]={};uint32_t readyAt[2]={};bool installed[2]={};
public:
  std::vector<ServoRuntime> servos;
  std::vector<ChainRuntime> chains;
  std::vector<GroupRuntime> groups;
  std::string error;
  void detach(ServoRuntime& s) {
    if(s.channel<6)ledc_stop(LEDC_LOW_SPEED_MODE,(ledc_channel_t)s.channel,0);else extra.pulse(s.channel-6,0);s.active=false;s.moving=false;
  }
  void stopAll() {
    extra.stop();
    for(auto& s:servos){detach(s);gpio_reset_pin((gpio_num_t)s.pin);}
    for(int ch=0;ch<2;++ch)if(installed[ch]){rmt_tx_stop((rmt_channel_t)ch);rmt_driver_uninstall((rmt_channel_t)ch);installed[ch]=false;}
    for(auto& ch:chains)gpio_reset_pin((gpio_num_t)ch.pin);
    servos.clear();chains.clear();groups.clear();for(int i=0;i<2;++i){owner[i]=-1;cursor[i]=i;cooling[i]=false;}
  }
  bool begin(JsonVariantConst cfg) {
    error.clear();portsMode=cfg["version"].as<int>()==2;std::map<int,int> timers;
    struct Resource {JsonVariantConst cfg;int pin,count;std::string id;};
    std::vector<Resource> servoCfg,chainCfg;
    if(portsMode){
      for(auto p:cfg["ports"].as<JsonArrayConst>()){
        auto t=findTemplate(cfg,p["template"].as<const char*>());
        if(std::string(t["kind"].as<const char*>())=="servo")servoCfg.push_back({p["servo"],p["pin"],0,p["id"].as<const char*>()});
        else chainCfg.push_back({p["ws"],p["pin"],t["count"],p["id"].as<const char*>()});
      }
    }else{
      for(auto sc:cfg["servos"].as<JsonArrayConst>())servoCfg.push_back({sc,sc["pin"],0,sc["id"].as<const char*>()});
      for(auto ch:cfg["chains"].as<JsonArrayConst>())chainCfg.push_back({ch,ch["pin"],ch["count"],ch["id"].as<const char*>()});
    }
    for(auto resource:servoCfg) {
      auto sc=resource.cfg;int hz=sc["hz"];
      ServoRuntime s;s.cfg=sc;s.id=resource.id;s.pin=resource.pin;s.channel=servos.size();s.updated=millis();
      if(s.channel<6){
        if(!timers.count(hz)) {
          int t=timers.size();ledc_timer_config_t tc={};tc.speed_mode=LEDC_LOW_SPEED_MODE;tc.duty_resolution=LEDC_TIMER_14_BIT;tc.timer_num=(ledc_timer_t)t;tc.freq_hz=hz;tc.clk_cfg=LEDC_AUTO_CLK;
          if(ledc_timer_config(&tc)!=ESP_OK){error="Cannot allocate LEDC timer";stopAll();return false;}timers[hz]=t;
        }
        s.timer=timers[hz];ledc_channel_config_t cc={};cc.gpio_num=s.pin;cc.speed_mode=LEDC_LOW_SPEED_MODE;cc.channel=(ledc_channel_t)s.channel;cc.timer_sel=(ledc_timer_t)s.timer;cc.duty=0;cc.intr_type=LEDC_INTR_DISABLE;
        if(ledc_channel_config(&cc)!=ESP_OK){error="Cannot allocate LEDC channel";stopAll();return false;}
      }else{
        gpio_reset_pin((gpio_num_t)s.pin);gpio_set_direction((gpio_num_t)s.pin,GPIO_MODE_OUTPUT);gpio_set_level((gpio_num_t)s.pin,0);extra.pin(s.channel-6,s.pin);
        if(!extra.begin()){error="No PWM timer for servos 7–10";stopAll();return false;}
      }
      servos.push_back(s);
    }
    for(auto resource:chainCfg) {
      ChainRuntime ch;ch.cfg=resource.cfg;ch.id=resource.id;ch.pin=resource.pin;ch.count=resource.count;
      bool rgbw=std::string(ch.cfg["type"].as<const char*>())=="SK6812RGBW";
      ch.data.resize(ch.count*(rgbw?4:3),0);gpio_reset_pin((gpio_num_t)ch.pin);releaseRmtPin(ch.pin);
      if(chains.size()<2){
        int slot=chains.size();rmt_config_t rc={};rc.rmt_mode=RMT_MODE_TX;rc.channel=(rmt_channel_t)slot;rc.gpio_num=(gpio_num_t)ch.pin;rc.mem_block_num=1;rc.clk_div=2;rc.tx_config.idle_output_en=true;rc.tx_config.idle_level=RMT_IDLE_LEVEL_LOW;
        if(rmt_config(&rc)!=ESP_OK){error="RMT configuration failed";stopAll();return false;}
        if(rmt_driver_install(rc.channel,0,ESP_INTR_FLAG_IRAM)!=ESP_OK){gpio_reset_pin(rc.gpio_num);error="No RMT TX driver resources";stopAll();return false;}
        installed[slot]=true;
        // Detach immediately: no RMT signal must fan out to previous GPIOs.
        releaseRmtPin(ch.pin);
      }
      chains.push_back(std::move(ch));
    }
    for(auto gc:cfg["groups"].as<JsonArrayConst>()){GroupRuntime g;g.cfg=gc;groups.push_back(g);}
    std::sort(groups.begin(),groups.end(),[](const GroupRuntime& a,const GroupRuntime& b){return a.cfg["priority"].as<int>()<b.cfg["priority"].as<int>();});
    return true;
  }
  ServoRuntime* servo(const std::string& id){for(auto& s:servos)if(id==s.id)return &s;return nullptr;}
  GroupRuntime* group(const std::string& id){for(auto& g:groups)if(id==g.cfg["id"].as<const char*>())return &g;return nullptr;}
  void pulse(ServoRuntime& s) {
    float angle=s.cfg["reverse"].as<bool>()?180-s.pos:s.pos;
    float us=s.cfg["minUs"].as<int>()+(s.cfg["maxUs"].as<int>()-s.cfg["minUs"].as<int>())*angle/180.f;
    if(s.channel>=6){extra.pulse(s.channel-6,lroundf(us));s.active=true;return;}
    uint32_t duty=lroundf(us*s.cfg["hz"].as<int>()*16384.f/1000000.f);
    if(ledc_set_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t)s.channel,duty)!=ESP_OK || ledc_update_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t)s.channel)!=ESP_OK){error="LEDC write failed";detach(s);return;}
    s.active=true;
  }
  void move(ServoRuntime& s,float angle) {
    angle=std::max(s.cfg["minAngle"].as<float>(),std::min(s.cfg["maxAngle"].as<float>(),angle));
    if(s.known && s.target==angle && (s.active || s.cfg["detachAfter"].as<bool>()))return;
    s.target=angle;s.updated=millis();s.releaseAt=0;
    if(!s.known){s.pos=angle;s.known=true;s.moving=false;pulse(s);if(s.cfg["detachAfter"].as<bool>())s.releaseAt=millis()+100;}
    else{s.moving=true;if(!s.active)pulse(s);}
  }
  void freeze(bool offline) {
    for(auto& s:servos) {
      if(s.calibration)continue;
      s.target=s.pos;s.moving=false;
      std::string policy=s.cfg["offline"];
      if(offline && policy=="safe")move(s,s.cfg["safeAngle"]);
      else if(offline && policy=="detach")detach(s);
    }
  }
  void action(JsonVariantConst a,bool allow) {
    std::string id=a["target"];
    if(std::string(a["kind"].as<const char*>())=="servo") {auto s=servo(id);if(s&&allow&&!s->calibration)move(*s,a["angle"]);return;}
    auto g=group(id);if(!g)return;
    g->action=a;g->epoch=millis();g->inverse=false;
    if(std::string(a["effect"].as<const char*>())=="alternate"){auto other=group(a["other"].as<const char*>());if(other){other->action=a;other->epoch=g->epoch;other->inverse=true;}}
  }
  void portAction(const std::string& id,JsonVariantConst state,bool allow,uint32_t period=0) {
    if(auto s=servo(id)){if(allow&&!s->calibration)move(*s,state["angle"]);return;}
    for(auto& ch:chains)if(ch.id==id){if(ch.state==state&&ch.periodOverride==period)return;ch.state=state;ch.periodOverride=period;ch.epoch=millis();return;}
  }
  void tick() {
    uint32_t now=millis();
    for(auto& s:servos) {
      uint32_t dt=now-s.updated;s.updated=now;
      if(!s.moving)continue;
      float step=s.cfg["speed"].as<float>()*std::min<uint32_t>(dt,50)/1000.f;
      if(fabsf(s.target-s.pos)<=step)s.pos=s.target;else s.pos+=s.target>s.pos?step:-step;
      pulse(s);
      if(s.pos==s.target){
        s.moving=false;
        if(s.cfg["detachAfter"].as<bool>())s.releaseAt=now+100;
      }
    }
    // Use a separate deadline for release, never delay the loop.
    for(auto& s:servos)if(s.active&&!s.moving&&s.cfg["detachAfter"].as<bool>()&&s.pos==s.target&&int32_t(now-s.releaseAt)>=0&&s.releaseAt!=0){detach(s);s.releaseAt=0;}
    // Each slot owns its buffer until hardware completion plus a latch interval.
    for(int slot=0;slot<2;++slot)if(installed[slot]&&owner[slot]>=0){
      if(rmt_wait_tx_done((rmt_channel_t)slot,0)!=ESP_OK)continue;
      if(!cooling[slot]){
        auto& previous=chains[owner[slot]];
        releaseRmtPin(previous.pin);
        cooling[slot]=true;readyAt[slot]=micros()+300;
      }
      if(int32_t(micros()-readyAt[slot])>=0){chains[owner[slot]].channel=-1;owner[slot]=-1;cooling[slot]=false;}
    }
    for(int slot=0;slot<2;++slot) {
      if(!installed[slot]||owner[slot]>=0||rmt_wait_tx_done((rmt_channel_t)slot,0)!=ESP_OK)continue;
      int chosen=-1;
      for(size_t n=0;n<chains.size();++n){int i=(cursor[slot]+n)%chains.size();if(chains[i].channel<0&&now-chains[i].sent>=30){chosen=i;break;}}
      if(chosen<0)continue;
      auto& ch=chains[chosen];cursor[slot]=(chosen+1)%chains.size();
      std::fill(ch.data.begin(),ch.data.end(),0);
      int bpp=std::string(ch.cfg["type"].as<const char*>())=="SK6812RGBW"?4:3;
      std::string order=ch.cfg["order"];
      if(portsMode&&!ch.state.isNull()){
        int pixel=0;
        for(auto a:ch.state["pixels"].as<JsonArrayConst>()){
          std::string effect=a["effect"];bool lit=effect!="off";
          if(effect=="blink"){uint32_t on=ch.periodOverride?ch.periodOverride/2:(a["onMs"]|500u),off=ch.periodOverride?ch.periodOverride-on:(a["offMs"]|500u);lit=((now-ch.epoch+(a["phaseMs"]|0u))%(on+off))<on;}
          for(int i=0;i<bpp;++i){int ci=order[i]=='R'?0:order[i]=='G'?1:order[i]=='B'?2:3;ch.data[pixel*bpp+i]=lit?(uint32_t(a["color"][ci].as<int>())*(a["brightness"]|255)*ch.cfg["brightness"].as<int>()+32512)/65025:0;}
          ++pixel;
        }
      }
      for(auto& g:groups) {
        if(g.action.isNull()||std::string(g.cfg["chain"].as<const char*>())!=ch.cfg["id"].as<const char*>())continue;
        auto a=g.action;std::string effect=a["effect"];bool lit=effect!="off";
        if(effect=="blink"||effect=="alternate") {
          uint32_t on=a["onMs"],off=a["offMs"];lit=((now-g.epoch)%(on+off))<on;if(g.inverse)lit=!lit;
        }
        uint8_t color[4]={};
        for(int i=0;i<4;++i)color[i]=lit?(uint32_t(a["color"][i].as<int>())*a["brightness"].as<int>()*ch.cfg["brightness"].as<int>()+32512)/65025:0;
        for(auto p:g.cfg["pixels"].as<JsonArrayConst>())for(int i=0;i<bpp;++i){int ci=order[i]=='R'?0:order[i]=='G'?1:order[i]=='B'?2:3;ch.data[p.as<int>()*bpp+i]=color[ci];}
      }
      rmt_channel_t channel=(rmt_channel_t)slot;
      if(rmt_set_gpio(channel,RMT_MODE_TX,(gpio_num_t)ch.pin,false)!=ESP_OK ||
         rmt_translator_init(channel,std::string(ch.cfg["type"].as<const char*>())=="WS2812B"?translateWS:translateSK)!=ESP_OK ||
         rmt_write_sample(channel,ch.data.data(),ch.data.size(),false)!=ESP_OK){error="RMT transmission failed";releaseRmtPin(ch.pin);continue;}
      ch.channel=slot;owner[slot]=chosen;
      ch.sent=now;
    }
  }
};
}

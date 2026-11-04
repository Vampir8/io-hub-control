#pragma once
#include <Arduino.h>
#ifndef HUB_HOST_TEST
#include <driver/timer.h>
#include <soc/gpio_struct.h>
#endif
namespace hub {
// Four extra PWM outputs. Their timing is interrupt-driven, unlike LEDC's six
// hardware channels. All extra outputs use 50 Hz. No task delays generate pulses.
class ExtraServos {
  volatile uint32_t desired[4]={};uint32_t masks[4]={},ends[4]={};
  uint32_t high=0;uint64_t frame=0;bool running=false,frameStarted=false;
public:
  struct Edges {uint32_t rise=0,fall=0;uint64_t next=0;};
private:
  Edges IRAM_ATTR advance(uint64_t now) {
    Edges e;
    if(!frameStarted||now>=frame+20000) {
      e.fall=high;frameStarted=true;frame=now;high=0;
      for(int i=0;i<4;++i){ends[i]=desired[i];if(ends[i])high|=masks[i];}
      e.rise=high;
    }else{
      for(int i=0;i<4;++i)if(ends[i]&&now-frame>=ends[i]){e.fall|=masks[i];ends[i]=0;}
      high&=~e.fall;
    }
    e.next=frame+20000;
    for(int i=0;i<4;++i)if(ends[i]&&frame+ends[i]<e.next)e.next=frame+ends[i];
    if(e.next<=now)e.next=now+10;
    return e;
  }
#ifndef HUB_HOST_TEST
  portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
  static bool IRAM_ATTR interrupt(void* arg){return static_cast<ExtraServos*>(arg)->tick();}
  bool IRAM_ATTR tick() {
    uint64_t now=timer_group_get_counter_value_in_isr(TIMER_GROUP_1,TIMER_0);
    portENTER_CRITICAL_ISR(&lock);
    auto e=advance(now);
    GPIO.out_w1tc.val=e.fall;GPIO.out_w1ts.val=e.rise;
    timer_group_set_alarm_value_in_isr(TIMER_GROUP_1,TIMER_0,e.next);
    timer_group_enable_alarm_in_isr(TIMER_GROUP_1,TIMER_0);
    portEXIT_CRITICAL_ISR(&lock);return false;
  }
#endif
public:
#ifdef HUB_HOST_TEST
  Edges simulateInterrupt(uint64_t now){return advance(now);}
#endif
  bool begin() {
    if(running)return true;
#ifndef HUB_HOST_TEST
    timer_config_t t={};t.divider=80;t.counter_dir=TIMER_COUNT_UP;t.counter_en=TIMER_PAUSE;t.alarm_en=TIMER_ALARM_EN;t.auto_reload=TIMER_AUTORELOAD_DIS;
    if(timer_init(TIMER_GROUP_1,TIMER_0,&t)!=ESP_OK)return false;
    if(timer_set_counter_value(TIMER_GROUP_1,TIMER_0,0)!=ESP_OK||timer_set_alarm_value(TIMER_GROUP_1,TIMER_0,100)!=ESP_OK||timer_enable_intr(TIMER_GROUP_1,TIMER_0)!=ESP_OK||timer_isr_callback_add(TIMER_GROUP_1,TIMER_0,interrupt,this,ESP_INTR_FLAG_IRAM|ESP_INTR_FLAG_LEVEL3)!=ESP_OK){timer_deinit(TIMER_GROUP_1,TIMER_0);return false;}
    timer_start(TIMER_GROUP_1,TIMER_0);
#endif
    running=true;return true;
  }
  void pin(int i,int pin){masks[i]=uint32_t(1)<<pin;}
  void pulse(int i,uint32_t us){
#ifndef HUB_HOST_TEST
    portENTER_CRITICAL(&lock);
#endif
    desired[i]=us;
    if(!us){
#ifndef HUB_HOST_TEST
      GPIO.out_w1tc.val=masks[i];
#endif
      high&=~masks[i];ends[i]=0;
    }
#ifndef HUB_HOST_TEST
    portEXIT_CRITICAL(&lock);
#endif
  }
  uint32_t commanded(int i)const{return desired[i];}
  void stop(){
#ifndef HUB_HOST_TEST
    if(running){timer_pause(TIMER_GROUP_1,TIMER_0);timer_disable_intr(TIMER_GROUP_1,TIMER_0);timer_isr_callback_remove(TIMER_GROUP_1,TIMER_0);timer_deinit(TIMER_GROUP_1,TIMER_0);}
    for(int i=0;i<4;++i)GPIO.out_w1tc.val=masks[i];
#endif
    for(int i=0;i<4;++i){desired[i]=0;ends[i]=0;masks[i]=0;}high=0;running=false;frameStarted=false;
  }
};
}

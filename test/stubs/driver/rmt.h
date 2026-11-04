#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include <vector>
#include <map>
inline std::map<int,std::vector<uint8_t>> delivered;
inline int txPins[2]={};
using rmt_channel_t=int;
enum rmt_mode_t{RMT_MODE_TX};enum rmt_idle_level_t{RMT_IDLE_LEVEL_LOW};
struct rmt_item32_t{uint16_t duration0=0,level0=0,duration1=0,level1=0;};
struct rmt_config_t{rmt_mode_t rmt_mode;rmt_channel_t channel;gpio_num_t gpio_num;int mem_block_num,clk_div;struct {bool idle_output_en;rmt_idle_level_t idle_level;}tx_config;};
using sample_to_rmt_t=void(*)(const void*,rmt_item32_t*,size_t,size_t,size_t*,size_t*);
extern std::vector<uint8_t> frames[2];extern bool rmtInstalled[2],rmtBusy[2];extern int failRmt;
inline int rmt_config(const rmt_config_t*){return ESP_OK;}
inline int rmt_driver_install(rmt_channel_t ch,int,int){if(ch==failRmt)return ESP_FAIL;rmtInstalled[ch]=true;return ESP_OK;}
inline int rmt_driver_uninstall(rmt_channel_t ch){rmtInstalled[ch]=false;return ESP_OK;}
inline int rmt_tx_stop(rmt_channel_t){return ESP_OK;}
inline int rmt_translator_init(rmt_channel_t,sample_to_rmt_t){return ESP_OK;}
inline int rmt_wait_tx_done(rmt_channel_t ch,int){return rmtBusy[ch]?ESP_FAIL:ESP_OK;}
inline int rmt_write_sample(rmt_channel_t ch,const uint8_t* p,size_t n,bool){frames[ch].assign(p,p+n);delivered[txPins[ch]]=frames[ch];return ESP_OK;}

inline int rmt_set_gpio(rmt_channel_t ch,rmt_mode_t,gpio_num_t pin,bool){txPins[ch]=pin;return ESP_OK;}

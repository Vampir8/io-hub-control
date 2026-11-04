#pragma once
#include <Arduino.h>
enum ledc_mode_t{LEDC_LOW_SPEED_MODE};
enum ledc_timer_bit_t{LEDC_TIMER_14_BIT=14};
enum ledc_timer_t{LEDC_TIMER_0,LEDC_TIMER_1,LEDC_TIMER_2,LEDC_TIMER_3};
enum ledc_channel_t{LEDC_CHANNEL_0,LEDC_CHANNEL_1,LEDC_CHANNEL_2,LEDC_CHANNEL_3,LEDC_CHANNEL_4,LEDC_CHANNEL_5};
enum ledc_clk_cfg_t{LEDC_AUTO_CLK};
enum ledc_intr_type_t{LEDC_INTR_DISABLE};
struct ledc_timer_config_t{ledc_mode_t speed_mode;ledc_timer_bit_t duty_resolution;ledc_timer_t timer_num;int freq_hz;ledc_clk_cfg_t clk_cfg;};
struct ledc_channel_config_t{int gpio_num;ledc_mode_t speed_mode;ledc_channel_t channel;ledc_timer_t timer_sel;uint32_t duty;ledc_intr_type_t intr_type;};
extern uint32_t duties[6];extern int writes,failLedc;
inline int ledc_timer_config(const ledc_timer_config_t*){return ESP_OK;}
inline int ledc_channel_config(const ledc_channel_config_t* c){duties[c->channel]=c->duty;return int(c->channel)==failLedc?ESP_FAIL:ESP_OK;}
inline int ledc_stop(ledc_mode_t,ledc_channel_t c,int){duties[c]=0;return ESP_OK;}
inline int ledc_set_duty(ledc_mode_t,ledc_channel_t c,uint32_t n){duties[c]=n;++writes;return ESP_OK;}
inline int ledc_update_duty(ledc_mode_t,ledc_channel_t){return ESP_OK;}

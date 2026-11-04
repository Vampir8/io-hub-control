#pragma once
using gpio_num_t=int;
inline int gpio_reset_pin(gpio_num_t){return 0;}

enum gpio_mode_t{GPIO_MODE_OUTPUT};
inline int gpio_set_direction(gpio_num_t,gpio_mode_t){return 0;}
inline int gpio_set_level(gpio_num_t,int){return 0;}

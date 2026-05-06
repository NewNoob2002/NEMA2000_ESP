#include "HAL.h"
#include "HAL_Config.h"
#include "driver/gpio.h"

namespace HAL {
void
Power_Init() {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << POWER_ON_PIN);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    // TODO: Initialize the power module.
}

void
Power_OnCheck() {
    // TODO: Check the power module status.
    gpio_set_level(POWER_ON_PIN, 1);
}
} // namespace HAL

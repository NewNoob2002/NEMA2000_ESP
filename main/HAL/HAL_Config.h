#pragma once

#define HAL_UPDATE_STACK_SIZE    4096
#define HAL_UPDATE_PROI          tskIDLE_PRIORITY + 2

//POWER Config
#define POWER_ON_PIN             (gpio_num_t)26

//KEY Config
#define powerKey_PIN             (gpio_num_t)39
#define functionKey_PIN          (gpio_num_t)12

//LED Config
#define batteryStatusLed_PIN     (gpio_num_t)27
#define chargerStatusLed_PIN     (gpio_num_t)14
#define fucntionkeyStatusLed_PIN (gpio_num_t)13
#define gnssStatusLed_PIN        (gpio_num_t)15
#define dataStatusLed_PIN        (gpio_num_t)2

//I2C Config
#define I2C_SDA_PIN              (gpio_num_t)21
#define I2C_SCL_PIN              (gpio_num_t)22
#define I2C_CLOCK_HZ             100000
#define I2C_TIMEOUT_MS           200

// GNSS Module Config
#define GNSS_POWER_PIN           (gpio_num_t)5
#define GNSS_TX_PIN              (gpio_num_t)10
#define GNSS_RX_PIN              (gpio_num_t)9

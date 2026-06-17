#include "CompileConfig.h"

#ifdef COMPILE_I2C

#include "HAL.h"
#include "HAL_Config.h"
#include "Support.h"
#include "driver/i2c_master.h"
#include "mcu_settings.h"

namespace {

class EspIdfI2CBus : public HAL::I2CBus {
  public:
    bool
    beginBus(gpio_num_t sda, gpio_num_t scl) {
        if (bus_handle_ != nullptr) {
            return true;
        }

        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = sda;
        bus_cfg.scl_io_num = scl;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.intr_priority = 1;
        bus_cfg.trans_queue_depth = 0;
        bus_cfg.flags.enable_internal_pullup = 1;
        bus_cfg.flags.allow_pd = 0;

        esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle_);
        if (ret != ESP_OK) {
            online_devices.i2c = false;
            systemPrintf("ERROR: I2C bus initialization failed: 0x%X", ret);
            return false;
        }

        online_devices.i2c = true;
        systemPrintf("I2C bus initialized");
        return true;
    }

    bool
    init(uint8_t address, uint32_t clk_hz) override {
        return getDeviceHandle(address, clk_hz);
    }

    bool
    probe(uint8_t address, uint32_t timeoutMs) override {
        return i2c_master_probe(bus_handle_, address, timeoutMs) == ESP_OK;
    }

    bool
    write(uint8_t address, const uint8_t* data, size_t length) override {
        i2c_master_dev_handle_t device_handle = device_handles_[address];
        if (device_handle == nullptr) {
            systemPrintf("ERROR: I2C device 0x%02X not initialized", address);
            return false;
        }
        return i2c_master_transmit(device_handle, data, length, I2C_TIMEOUT_MS) == ESP_OK;
    }

    bool
    writeRead(uint8_t address, const uint8_t* writeData, size_t writeLength, uint8_t* readData,
              size_t readLength) override {
        i2c_master_dev_handle_t device_handle = device_handles_[address];
        if (device_handle == nullptr) {
            systemPrintf("ERROR: I2C device 0x%02X not initialized", address);
            return false;
        }

        return i2c_master_transmit_receive(device_handle, writeData, writeLength, readData, readLength, I2C_TIMEOUT_MS)
               == ESP_OK;
    }

  private:
    bool
    getDeviceHandle(uint8_t address, uint32_t clock_hz = I2C_CLOCK_HZ) {
        if (address >= 128 || bus_handle_ == nullptr) {
            return false;
        }

        if (device_handles_[address] != nullptr) {
            return true;
        }

        i2c_device_config_t device_cfg = {};
        device_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_cfg.device_address = address;
        device_cfg.scl_speed_hz = clock_hz;
        device_cfg.scl_wait_us = 0;
        device_cfg.flags.disable_ack_check = 0;

        esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &device_cfg, &device_handles_[address]);
        if (ret != ESP_OK) {
            systemPrintf("ERROR: I2C device 0x%02X initialization failed: 0x%X", address, ret);
            return false;
        }
        return true;
    }

    i2c_master_bus_handle_t bus_handle_ = nullptr;
    i2c_master_dev_handle_t device_handles_[128] = {};
};

EspIdfI2CBus g_i2c_bus;

bool
i2cBusInitialization(gpio_num_t sda, gpio_num_t scl) {
    bool ret = g_i2c_bus.beginBus(sda, scl);
    ret = g_i2c_bus.init(0x0B, 100000);
    ret = g_i2c_bus.init(0x5C, 400000);
    return ret;
}

} // namespace

namespace HAL {

bool
I2C_Init() {
    return i2cBusInitialization(I2C_SDA_PIN, I2C_SCL_PIN);
}

int
I2C_Scan(void (*callback)(const uint8_t* address)) {
    int nDevice = 0;
    for (uint8_t i = 0x01; i < 0x7F; i++) {
        if (I2C_GetBus().probe(i, 100)) {
            if (callback) {
                callback(&i);
            } else {
                systemPrintf("Found device address: %02x", i);
            }
            nDevice++;
        }
    }
    return nDevice;
}

I2CBus&
I2C_GetBus() {
    return g_i2c_bus;
}

} // namespace HAL

#endif

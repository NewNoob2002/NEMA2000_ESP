#pragma once

#include <stdint.h>
#include "HAL/HAL_I2C_Interface.h"

#ifdef __cplusplus

/**
 * @brief Driver for the MP2762A charger and power-management IC.
 */
class MP2762A {
  public:
    static constexpr uint8_t kDefaultAddress = 0x5C;

    /**
     * @brief Create an MP2762A driver bound to an abstract I2C bus.
     */
    explicit MP2762A(HAL::I2CBus& bus, uint8_t address = kDefaultAddress);

    /**
     * @brief Probe the charger at its configured I2C address.
     */
    bool begin();

    /**
     * @brief Read an 8-bit register.
     */
    bool readRegister8(uint8_t reg, uint8_t& value) const;

    /**
     * @brief Read a 16-bit little-endian register.
     */
    bool readRegister16LE(uint8_t reg, uint16_t& value) const;

    /**
     * @brief Write an 8-bit register.
     */
    bool writeRegister8(uint8_t reg, uint8_t value);

  private:
    HAL::I2CBus& bus_;
    uint8_t address_;
};

#endif // __cplusplus

#pragma once

#include <cstddef>
#include <cstdint>

namespace HAL {

/**
 * @brief Platform-independent I2C bus interface used by reusable device drivers.
 */
class I2CBus {
  public:
    /**
     * @brief Destroy the I2C bus interface.
     */
    virtual ~I2CBus() = default;

    /**
     * @brief Initialize or attach a 7-bit I2C device address before transactions.
     */
    virtual bool init(uint8_t address, uint32_t clk_hz) = 0;

    /**
     * @brief Probe a 7-bit I2C address.
     */
    virtual bool probe(uint8_t address, uint32_t timeoutMs) = 0;

    /**
     * @brief Write raw bytes to a 7-bit I2C address.
     */
    virtual bool write(uint8_t address, const uint8_t* data, size_t length) = 0;

    /**
     * @brief Write bytes, then read bytes from the same 7-bit I2C address.
     */
    virtual bool writeRead(uint8_t address, const uint8_t* writeData, size_t writeLength, uint8_t* readData,
                           size_t readLength) = 0;

    /**
     * @brief Read bytes from an 8-bit register address.
     */
    bool
    readRegister(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
        return writeRead(address, &reg, 1, data, length);
    }

    /**
     * @brief Read one byte from an 8-bit register address.
     */
    bool
    readRegister8(uint8_t address, uint8_t reg, uint8_t& value) {
        return readRegister(address, reg, &value, 1);
    }

    /**
     * @brief Read a 16-bit little-endian value from an 8-bit register address.
     */
    bool
    readRegister16LE(uint8_t address, uint8_t reg, uint16_t& value) {
        uint8_t data[2] = {0xFF, 0xFF};
        if (!readRegister(address, reg, data, sizeof(data))) {
            value = 0xFFFF;
            return false;
        }

        value = static_cast<uint16_t>((static_cast<uint16_t>(data[1]) << 8) | data[0]);
        return true;
    }

    /**
     * @brief Write one byte to an 8-bit register address.
     */
    bool
    writeRegister8(uint8_t address, uint8_t reg, uint8_t value) {
        const uint8_t data[2] = {reg, value};
        return write(address, data, sizeof(data));
    }
};

} // namespace HAL

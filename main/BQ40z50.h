#pragma once

#include <stdint.h>
#include "HAL/HAL_I2C_Interface.h"

#ifdef __cplusplus

/**
 * @brief Driver for the TI BQ40Z50 smart battery fuel gauge.
 */
class BQ40Z50 {
  public:
    static constexpr uint8_t kDefaultAddress = 0x0B;

    /**
     * @brief Create a BQ40Z50 driver bound to an abstract I2C bus.
     */
    explicit BQ40Z50(HAL::I2CBus& bus, uint8_t address = kDefaultAddress);

    /**
     * @brief Probe the fuel gauge at its configured I2C address.
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
     * @brief Get pack temperature in degrees Celsius.
     */
    float getTemperatureCelsius() const;

    /**
     * @brief Get raw temperature value.
     */
    uint16_t getTemperatureRaw() const;

    /**
     * @brief Get pack temperature in degrees Fahrenheit.
     */
    float getTemperatureFahrenheit() const;

    /**
     * @brief Get pack voltage in millivolts.
     */
    uint16_t getVoltageMillivolts() const;

    /**
     * @brief Get instantaneous current in milliamps.
     */
    int16_t getCurrentMilliamps() const;

    /**
     * @brief Get average current in milliamps.
     */
    int16_t getAverageCurrentMilliamps() const;

    /**
     * @brief Get the maximum expected state-of-charge error in percent.
     */
    uint8_t getMaxError() const;

    /**
     * @brief Get relative state of charge from the gauge in percent.
     */
    uint8_t getRelativeStateOfChargePercent() const;

    /**
     * @brief Calculate relative state of charge from remaining and full capacity.
     */
    float calculateRelativeStateOfChargePercent() const;

    /**
     * @brief Get absolute state of charge in percent.
     */
    uint8_t getAbsoluteStateOfChargePercent() const;

    /**
     * @brief Get remaining capacity in milliamp-hours.
     */
    uint16_t getRemainingCapacityMilliampHours() const;

    /**
     * @brief Get full charge capacity in milliamp-hours.
     */
    uint16_t getFullChargeCapacityMilliampHours() const;

    /**
     * @brief Get predicted runtime to empty in minutes.
     */
    uint16_t getRunTimeToEmptyMinutes() const;

    /**
     * @brief Get average predicted runtime to empty in minutes.
     */
    uint16_t getAverageTimeToEmptyMinutes() const;

    /**
     * @brief Get average predicted time to full in minutes.
     */
    uint16_t getAverageTimeToFullMinutes() const;

    /**
     * @brief Get requested charging current in milliamps.
     */
    uint16_t getChargingCurrentMilliamps() const;

    /**
     * @brief Get requested charging voltage in millivolts.
     */
    uint16_t getChargingVoltageMillivolts() const;

    /**
     * @brief Get the battery cycle count.
     */
    uint16_t getCycleCount() const;

    /**
     * @brief Get cell 1 voltage in millivolts.
     */
    uint16_t getCell1VoltageMillivolts() const;

    /**
     * @brief Get cell 2 voltage in millivolts.
     */
    uint16_t getCell2VoltageMillivolts() const;

    /**
     * @brief Get cell 3 voltage in millivolts.
     */
    uint16_t getCell3VoltageMillivolts() const;

    /**
     * @brief Get cell 4 voltage in millivolts.
     */
    uint16_t getCell4VoltageMillivolts() const;

  private:
    HAL::I2CBus& bus_;
    uint8_t address_;
};

#endif // __cplusplus

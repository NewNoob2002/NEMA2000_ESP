#include "BQ40z50.h"

namespace {

constexpr uint8_t kTemperatureReg = 0x08;
constexpr uint8_t kVoltageReg = 0x09;
constexpr uint8_t kCurrentReg = 0x0A;
constexpr uint8_t kAverageCurrentReg = 0x0B;
constexpr uint8_t kMaxErrorReg = 0x0C;
constexpr uint8_t kRelativeStateOfChargeReg = 0x0D;
constexpr uint8_t kAbsoluteStateOfChargeReg = 0x0E;
constexpr uint8_t kRemainingCapacityReg = 0x0F;
constexpr uint8_t kFullChargeCapacityReg = 0x10;
constexpr uint8_t kRunTimeToEmptyReg = 0x11;
constexpr uint8_t kAverageTimeToEmptyReg = 0x12;
constexpr uint8_t kAverageTimeToFullReg = 0x13;
constexpr uint8_t kChargingCurrentReg = 0x14;
constexpr uint8_t kChargingVoltageReg = 0x15;
constexpr uint8_t kCycleCountReg = 0x17;
constexpr uint8_t kCellVoltage4Reg = 0x3C;
constexpr uint8_t kCellVoltage3Reg = 0x3D;
constexpr uint8_t kCellVoltage2Reg = 0x3E;
constexpr uint8_t kCellVoltage1Reg = 0x3F;

uint16_t
ReadU16OrDefault(const BQ40Z50& battery, uint8_t reg) {
    uint16_t value = 0xFFFF;
    battery.readRegister16LE(reg, value);
    return value;
}

uint8_t
ReadU8OrDefault(const BQ40Z50& battery, uint8_t reg) {
    uint8_t value = 0xFF;
    battery.readRegister8(reg, value);
    return value;
}

} // namespace

BQ40Z50::BQ40Z50(HAL::I2CBus& bus, uint8_t address) : bus_(bus), address_(address) {}

bool
BQ40Z50::begin() {
    return bus_.probe(address_, 100);
}

bool
BQ40Z50::readRegister8(uint8_t reg, uint8_t& value) const {
    return bus_.readRegister8(address_, reg, value);
}

bool
BQ40Z50::readRegister16LE(uint8_t reg, uint16_t& value) const {
    return bus_.readRegister16LE(address_, reg, value);
}

float
BQ40Z50::getTemperatureCelsius() const {
    const uint16_t temperatureDeciKelvin = ReadU16OrDefault(*this, kTemperatureReg);
    return (static_cast<float>(temperatureDeciKelvin) * 0.1f) - 273.15f;
}

uint16_t
BQ40Z50::getTemperatureRaw() const {
    return ReadU16OrDefault(*this, kTemperatureReg);
}

float
BQ40Z50::getTemperatureFahrenheit() const {
    return (getTemperatureCelsius() * 9.0f / 5.0f) + 32.0f;
}

uint16_t
BQ40Z50::getVoltageMillivolts() const {
    return ReadU16OrDefault(*this, kVoltageReg);
}

int16_t
BQ40Z50::getCurrentMilliamps() const {
    return static_cast<int16_t>(ReadU16OrDefault(*this, kCurrentReg));
}

int16_t
BQ40Z50::getAverageCurrentMilliamps() const {
    return static_cast<int16_t>(ReadU16OrDefault(*this, kAverageCurrentReg));
}

uint8_t
BQ40Z50::getMaxError() const {
    return ReadU8OrDefault(*this, kMaxErrorReg);
}

uint8_t
BQ40Z50::getRelativeStateOfChargePercent() const {
    return ReadU8OrDefault(*this, kRelativeStateOfChargeReg);
}

float
BQ40Z50::calculateRelativeStateOfChargePercent() const {
    const uint16_t remainingCapacity = getRemainingCapacityMilliampHours();
    const uint16_t fullChargeCapacity = getFullChargeCapacityMilliampHours();
    if (remainingCapacity == 0xFFFF || fullChargeCapacity == 0 || fullChargeCapacity == 0xFFFF) {
        return -1.0f;
    }

    return (static_cast<float>(remainingCapacity) * 100.0f) / static_cast<float>(fullChargeCapacity);
}

uint8_t
BQ40Z50::getAbsoluteStateOfChargePercent() const {
    return ReadU8OrDefault(*this, kAbsoluteStateOfChargeReg);
}

uint16_t
BQ40Z50::getRemainingCapacityMilliampHours() const {
    return ReadU16OrDefault(*this, kRemainingCapacityReg);
}

uint16_t
BQ40Z50::getFullChargeCapacityMilliampHours() const {
    return ReadU16OrDefault(*this, kFullChargeCapacityReg);
}

uint16_t
BQ40Z50::getRunTimeToEmptyMinutes() const {
    return ReadU16OrDefault(*this, kRunTimeToEmptyReg);
}

uint16_t
BQ40Z50::getAverageTimeToEmptyMinutes() const {
    return ReadU16OrDefault(*this, kAverageTimeToEmptyReg);
}

uint16_t
BQ40Z50::getAverageTimeToFullMinutes() const {
    return ReadU16OrDefault(*this, kAverageTimeToFullReg);
}

uint16_t
BQ40Z50::getChargingCurrentMilliamps() const {
    return ReadU16OrDefault(*this, kChargingCurrentReg);
}

uint16_t
BQ40Z50::getChargingVoltageMillivolts() const {
    return ReadU16OrDefault(*this, kChargingVoltageReg);
}

uint16_t
BQ40Z50::getCycleCount() const {
    return ReadU16OrDefault(*this, kCycleCountReg);
}

uint16_t
BQ40Z50::getCell1VoltageMillivolts() const {
    return ReadU16OrDefault(*this, kCellVoltage1Reg);
}

uint16_t
BQ40Z50::getCell2VoltageMillivolts() const {
    return ReadU16OrDefault(*this, kCellVoltage2Reg);
}

uint16_t
BQ40Z50::getCell3VoltageMillivolts() const {
    return ReadU16OrDefault(*this, kCellVoltage3Reg);
}

uint16_t
BQ40Z50::getCell4VoltageMillivolts() const {
    return ReadU16OrDefault(*this, kCellVoltage4Reg);
}

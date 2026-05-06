#include "MP2762A.h"

MP2762A::MP2762A(HAL::I2CBus& bus, uint8_t address) : bus_(bus), address_(address) {}

bool
MP2762A::begin() {
    return bus_.probe(address_, 100);
}

bool
MP2762A::readRegister8(uint8_t reg, uint8_t& value) const {
    return bus_.readRegister8(address_, reg, value);
}

bool
MP2762A::readRegister16LE(uint8_t reg, uint16_t& value) const {
    return bus_.readRegister16LE(address_, reg, value);
}

bool
MP2762A::writeRegister8(uint8_t reg, uint8_t value) {
    return bus_.writeRegister8(address_, reg, value);
}

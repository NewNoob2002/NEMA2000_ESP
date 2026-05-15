#pragma once
#include "mcu_typedef.h"

//----------------------------------------
// Types
//----------------------------------------
#if defined(__cplusplus)
class HardwareSerial;
class UnicoreUM980;

// ----------------------------------------
// Prototypes
//----------------------------------------
void gnssBegin(HardwareSerial* pGnssSerial, UnicoreUM980* pUm980);
void gnssUpdate(UnicoreUM980* pUm980);
#endif // __cplusplus
void gnssConfigure(uint32_t configureBit);

// Given a bit to configure, clear that bit from the overall bitfield
void gnssConfigureClear(uint32_t configureBit);
// Return true if a given bit is set
bool gnssConfigureRequested(uint32_t configureBit);

bool gnssConfigureComplete();

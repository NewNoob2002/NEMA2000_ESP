#ifndef __HAL_H
#define __HAL_H

#include <stdint.h>
#include "HAL_I2C_Interface.h"
#include "Support.h"

#ifdef __cplusplus
namespace HAL {
/**
 * @brief Initialize the hardware abstraction layer.
 */
void HAL_Init();

/**
 * @brief Deinitialize the hardware abstraction layer.
 */
void HAL_Deinit();

/**
 * @brief Run periodic HAL maintenance.
 */
void HAL_Update(void* e);
/* I2C */

/**
 * @brief Initialize the platform I2C bus.
 */
bool I2C_Init();

/**
 * @brief Scan the I2C bus and print found addresses to the serial buffer.
 * @return int number of devices found
 */
int I2C_Scan(void (*callback)(const uint8_t* address));

/**
 * @brief Get the platform I2C bus implementation.
 */
I2CBus& I2C_GetBus();

/* Power */
/**
 * @brief Initialize the power module.
 */
void Power_Init();
/**
 * @brief Check the power module status.
 */
void Power_OnCheck();
/**
 * @brief Set the power module shutdown state.
 * @param en True to shutdown right now, False to shutdown after config file is saved.
 */
void Power_Shutdown(bool en);

/*FILE SYSTEM*/
void FileSystem_Init();

/*SYSTEM*/
void System_Update();

/* GNSS */
/**
 * @brief Initialize the GNSS module.
 * 
 */
void GNSS_Init();

/**
 * @brief Configure the GNSS module, such as setting message output rates and dynamic model.
 * 
 */
void GNSS_Configure();

/* Bluetooth */
/**
 * @brief Initialize the Bluetooth module.
 */
void Bluetooth_Init();

} // namespace HAL

#endif // __cplusplus
#endif // __HAL_H

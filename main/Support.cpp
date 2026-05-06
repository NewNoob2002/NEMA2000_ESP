#include <cstdarg>
#include <memory>
#include <stdio.h>
#include "HardwareSerial.h"
#include "Support.h"

/**
 * @brief Check if data is available for reading
 * 
 * @return int bytes available to read
 */
int
systemAvailable() {
    // if (printEndpoint == PRINT_ENDPOINT_BLUETOOTH || printEndpoint ==
    // PRINT_ENDPOINT_ALL) return (bluetoothRxDataAvailable());
    return (Serial.available());
}

/**
 * @brief Flush the serial buffer
 */
void
systemFlush() {
    Serial.flush();
}

/**
 * @brief Read a byte from the serial buffer
 * 
 * @return int the byte read, or -1 if no data is available
 */
int
systemRead() {
    return Serial.read();
}

/**
 * @brief Write data to the serial buffer
 * 
 * @param buffer pointer to the data to write, must be null-terminated
 * @param length of the data to write
 */
void
systemWrite(const uint8_t* buffer, uint16_t length) {
    // printf("%.*s", length, buffer);
    Serial.write(buffer, length);
}

/**
 * @brief Write a byte to the serial buffer
 * 
 * @param value the byte to write
 */
void
systemWrite(uint8_t value) {
    systemWrite(&value, 1);
}

/**
 * @brief Print a string to the serial buffer
 * 
 * @param string pointer to the string to print, must be null-terminated
 */
void
systemPrint(const char* string) {
    systemWrite((const uint8_t*)string, strlen(string));
}

/**
 * @brief Print a string to the serial buffer, followed by a newline
 * 
 * @param value pointer to the string to print, must be null-terminated
 */
void
systemPrintln(const char* value) {
    systemPrint(value);
    systemPrintln();
}

/**
 * @brief Print a newline to the serial buffer
 */
void
systemPrintln() {
    systemPrint("\r\n");
}

/**
 * @brief Print a formatted string to the serial buffer
 * 
 * @param format pointer to the format string, must be null-terminated
 * @param ... variable arguments to format
 */
void
systemPrintf(const char* format, ...) {
    if (format == nullptr) {
        systemPrint("Error: systemPrintf received null format string");
        return;
    }

    // Define maximum buffer size to prevent stack overflow
    const size_t MAX_BUFFER_SIZE = 1024;

    va_list args;
    va_start(args, format);

    // First pass: calculate required buffer size
    va_list args2;
    va_copy(args2, args);
    int required_size = vsnprintf(nullptr, 0, format, args);

    if (required_size < 0) {
        systemPrint("Error: systemPrintf format string is invalid");
        va_end(args);
        va_end(args2);
        return;
    }

    // Check if the required size exceeds our maximum buffer size
    if (required_size >= MAX_BUFFER_SIZE) {
        systemPrint("Error: systemPrintf output would exceed maximum buffer size");
        va_end(args);
        va_end(args2);
        return;
    }

    // Allocate buffer with exact size needed plus null terminator.
    const size_t buffer_size = static_cast<size_t>(required_size) + 1;
    std::unique_ptr<char[]> buf(new char[buffer_size]);

    // Second pass: format the string
    int result = vsnprintf(buf.get(), buffer_size, format, args2);

    if (result < 0 || static_cast<size_t>(result) >= buffer_size) {
        systemPrint("Error: systemPrintf failed to format string");
        va_end(args);
        va_end(args2);
        return;
    }

    // Ensure null termination
    buf[buffer_size - 1] = '\0';

    // Print the formatted string
    systemPrint(buf.get());

    va_end(args);
    va_end(args2);
}

/**
 * @brief Print an integer to the serial buffer
 * 
 * @param value the integer to print
 */
void
systemPrint(int value) {
    char temp[20];
    snprintf(temp, sizeof(temp), "%d", value);
    systemPrint(temp);
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 */
void
systemPrintln(int value) {
    systemPrint(value);
    systemPrintln();
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrint(int value, uint8_t printType) {
    char temp[100];

    if (printType == HEX) {
        snprintf(temp, sizeof(temp), "%08X", value);
    } else if (printType == DEC) {
        snprintf(temp, sizeof(temp), "%d", value);
    }

    systemPrint(temp);
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrintln(int value, uint8_t printType) {
    systemPrint(value, printType);
    systemPrintln();
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrint(uint8_t value, uint8_t printType) {
    char temp[20];

    if (printType == HEX) {
        snprintf(temp, sizeof(temp), "%02X", value);
    } else if (printType == DEC) {
        snprintf(temp, sizeof(temp), "%d", value);
    }

    systemPrint(temp);
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrintln(uint8_t value, uint8_t printType) {
    systemPrint(value, printType);
    systemPrintln();
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrint(uint16_t value, uint8_t printType) {
    char temp[20];

    if (printType == HEX) {
        snprintf(temp, sizeof(temp), "%04X", value);
    } else if (printType == DEC) {
        snprintf(temp, sizeof(temp), "%d", value);
    }

    systemPrint(temp);
}

/**
 * @brief Print an integer to the serial buffer, followed by a newline
 * 
 * @param value the integer to print
 * @param printType the print type, HEX or DEC
 */
void
systemPrintln(uint16_t value, uint8_t printType) {
    systemPrint(value, printType);
    systemPrintln();
}

/**
 * @brief Print a floating point value to the serial buffer
 * 
 * @param value the floating point value to print
 * @param decimals the number of decimal places to print
 */
void
systemPrint(float value, uint8_t decimals) {
    char temp[20];
    snprintf(temp, sizeof(temp), "%.*f", decimals, value);
    systemPrint(temp);
}

/**
 * @brief Print a floating point value to the serial buffer, followed by a newline
 * 
 * @param value the floating point value to print
 * @param decimals the number of decimal places to print
 */
void
systemPrintln(float value, uint8_t decimals) {
    systemPrint(value, decimals);
    systemPrintln();
}

/**
 * @brief Print a double precision floating point value to the serial buffer
 * 
 * @param value the double precision floating point value to print
 * @param decimals the number of decimal places to print
 */
void
systemPrint(double value, uint8_t decimals) {
    char temp[30];
    snprintf(temp, sizeof(temp), "%.*f", decimals, value);
    systemPrint(temp);
}

/**
 * @brief Print a double precision floating point value to the serial buffer, followed by a newline
 * 
 * @param value the double precision floating point value to print
 * @param decimals the number of decimal places to print
 */
void
systemPrintln(double value, uint8_t decimals) {
    systemPrint(value, decimals);
    systemPrintln();
}

/**
 * @brief Print a string to the serial buffer
 * 
 * @param myString the string to print
 */
void
systemPrint(String myString) {
    systemPrint(myString.c_str());
}

/**
 * @brief Print a string to the serial buffer, followed by a newline
 * 
 * @param myString the string to print
 */
void
systemPrintln(String myString) {
    systemPrint(myString);
    systemPrintln();
}

/**
 * @brief Clear the serial buffer
 * 
 * @note This function waits for any incoming characters to be processed before clearing the buffer.
 */
void
clearBuffer() {
    systemFlush();
    delay(20); // Wait for any incoming chars to hit buffer
    while (systemAvailable() > 0) {
        systemRead(); // Clear buffer
    }
}

// void
// geodeticToEcef(double lat, double lon, double alt, double* x, double* y, double* z) {
//     double clat = cos(lat * DEG_TO_RAD);
//     double slat = sin(lat * DEG_TO_RAD);
//     double clon = cos(lon * DEG_TO_RAD);
//     double slon = sin(lon * DEG_TO_RAD);

//     double N = WGS84_A / sqrt(1.0 - WGS84_E * WGS84_E * slat * slat);

//     *x = (N + alt) * clat * clon;
//     *y = (N + alt) * clat * slon;
//     *z = (N * (1.0 - WGS84_E * WGS84_E) + alt) * slat;
// }

// // Convert ECEF to LLH (geodetic)
// // From:
// // https://danceswithcode.net/engineeringnotes/geodetic_to_ecef/geodetic_to_ecef.html
// void
// ecefToGeodetic(double x, double y, double z, double* lat, double* lon, double* alt) {
//     double a = 6378137.0;              // WGS-84 semi-major axis
//     double e2 = 6.6943799901377997e-3; // WGS-84 first eccentricity squared
//     double a1 = 4.2697672707157535e+4; // a1 = a*e2
//     double a2 = 1.8230912546075455e+9; // a2 = a1*a1
//     double a3 = 1.4291722289812413e+2; // a3 = a1*e2/2
//     double a4 = 4.5577281365188637e+9; // a4 = 2.5*a2
//     double a5 = 4.2840589930055659e+4; // a5 = a1+a3
//     double a6 = 9.9330562000986220e-1; // a6 = 1-e2

//     double zp, w2, w, r2, r, s2, c2, s, c, ss;
//     double g, rg, rf, u, v, m, f, p;

//     zp = abs(z);
//     w2 = x * x + y * y;
//     w = sqrt(w2);
//     r2 = w2 + z * z;
//     r = sqrt(r2);
//     *lon = atan2(y, x); // Lon (final)

//     s2 = z * z / r2;
//     c2 = w2 / r2;
//     u = a2 / r;
//     v = a3 - a4 / r;
//     if (c2 > 0.3) {
//         s = (zp / r) * (1.0 + c2 * (a1 + u + s2 * v) / r);
//         *lat = asin(s); // Lat
//         ss = s * s;
//         c = sqrt(1.0 - ss);
//     } else {
//         c = (w / r) * (1.0 - s2 * (a5 - u - c2 * v) / r);
//         *lat = acos(c); // Lat
//         ss = 1.0 - c * c;
//         s = sqrt(ss);
//     }

//     g = 1.0 - e2 * ss;
//     rg = a / sqrt(g);
//     rf = a6 * rg;
//     u = w - rg * c;
//     v = zp - rf * s;
//     f = c * u + s * v;
//     m = c * v - s * u;
//     p = m / (rf / g + f);
//     *lat = *lat + p;        // Lat
//     *alt = f + m * p / 2.0; // Altitude
//     if (z < 0.0) {
//         *lat *= -1.0; // Lat
//     }

//     *lat *= RAD_TO_DEG; // Convert to degrees
//     *lon *= RAD_TO_DEG;
// }

// Make size of files human readable
void
stringHumanReadableSize(String& returnText, uint64_t bytes) {
    char suffix[5] = {'\0'};
    char readableSize[50] = {'\0'};
    float cardSize = 0.0;

    if (bytes < 1024) {
        strcpy(suffix, "B");
    } else if (bytes < (1024 * 1024)) {
        strcpy(suffix, "KB");
    } else if (bytes < (1024 * 1024 * 1024)) {
        strcpy(suffix, "MB");
    } else {
        strcpy(suffix, "GB");
    }

    if (bytes < (1024)) {
        cardSize = bytes; // B
    } else if (bytes < (1024 * 1024)) {
        cardSize = bytes / 1024.0; // KB
    } else if (bytes < (1024 * 1024 * 1024)) {
        cardSize = bytes / 1024.0 / 1024.0; // MB
    } else {
        cardSize = bytes / 1024.0 / 1024.0 / 1024.0; // GB
    }

    if (strcmp(suffix, "GB") == 0) {
        snprintf(readableSize, sizeof(readableSize), "%0.1f %s", cardSize,
                 suffix); // Print decimal portion
    } else if (strcmp(suffix, "MB") == 0) {
        snprintf(readableSize, sizeof(readableSize), "%0.1f %s", cardSize,
                 suffix); // Print decimal portion
    } else if (strcmp(suffix, "KB") == 0) {
        snprintf(readableSize, sizeof(readableSize), "%0.1f %s", cardSize,
                 suffix); // Print decimal portion
    } else {
        snprintf(readableSize, sizeof(readableSize), "%.0f %s", cardSize,
                 suffix); // Don't print decimal portion
    }

    returnText = String(readableSize);
}

// void
// verifyTable() {
//     // Verify the measurement scales
//     if (measurementScaleEntries != MEASUREMENT_UNITS_MAX) {
//         reportFatalError("Fix measurementScaleTable to match measurementUnits");
//     }

//     tasksValidateTables();

//     present.button_function = true;
//     present.antennaPhaseCenter_mm = 116.5;
//     present.gnss_um980 = true;
//     present.charger_mp2762a = true;
//     present.fuelgauge_bq40z50 = true;
//     present.microSd = true;
//     present.radio_lora = true;
//     present.galileoHasCapable = true;
//     present.laraPowerControl = true;
//     present.gnss_to_uart1 = true;
//     present.gnss_to_uart3 = true;
//     present.minCno = true;
//     present.minElevation = true;
//     present.dynamicModel = true;
//     // if (CORR_NUM >= (int)('h' - 'a'))
//     //   reportFatalError("Too many correction sources");
// }

// void
// checkGNSSArrayDefaults() {
//     bool defaultsApplied = false;

//     if (present.gnss_um980) {
//         if (settings.dynamicModel == 254) {
//             settings.dynamicModel = UM980_DYN_MODEL_SURVEY;
//         }

//         if (settings.um980Constellations[0] == 254) {
//             defaultsApplied = true;
//             systemPrintf("UM980 Constellations\r\n");
//             // Reset constellations to defaults
//             for (int x = 0; x < MAX_UM980_CONSTELLATIONS; x++) {
//                 settings.um980Constellations[x] = 1;
//             }
//         }

//         if (settings.um980MessageRatesNMEA[0] == 254) {
//             defaultsApplied = true;
//             systemPrintf("UM980 um980MessageRatesNMEA\r\n");
//             // Reset rates to defaults
//             for (int x = 0; x < MAX_UM980_NMEA_MSG; x++) {
//                 settings.um980MessageRatesNMEA[x] = umMessagesNMEA[x].msgDefaultRate;
//             }
//         }

//         if (settings.um980MessageRatesRTCMRover[0] == 254) {
//             defaultsApplied = true;
//             systemPrintf("UM980 um980MessageRatesRTCMRover\r\n");
//             // For rovers, RTCM should be zero by default.
//             for (int x = 0; x < MAX_UM980_RTCM_MSG; x++) {
//                 settings.um980MessageRatesRTCMRover[x] = 0;
//             }
//         }

//         if (settings.um980MessageRatesRTCMBase[0] == 254) {
//             defaultsApplied = true;
//             systemPrintf("UM980 um980MessageRatesRTCMBase\r\n");
//             // Reset RTCM rates to defaults
//             for (int x = 0; x < MAX_UM980_RTCM_MSG; x++) {
//                 settings.um980MessageRatesRTCMBase[x] = umMessagesRTCM[x].msgDefaultRate;
//             }
//         }
//     }

//     if (defaultsApplied == true) {
//         if (present.gnss_um980) {
//             systemPrintf("UM980 Defaults Applied\r\n");
//             settings.minCNO = 10;                    // Default 10 degrees
//             settings.surveyInStartingAccuracy = 2.0; // Default 2m
//                                                      // settings.measurementRateMs = 500;        // Default 2Hz.
//         }
//     }
// }

// void
// checkTaskStatus() {
//     char InfoBuffer[512] = {0};
//     memset(InfoBuffer, 0, 512); // 信息缓冲区清零
//     vTaskList((char*)&InfoBuffer);
//     systemPrintf("任务名  任务状态  优先级  剩余栈  任务序号  cpu核\r\n");
//     systemPrintf("\r\n%s\r\n", InfoBuffer);

// #if 1
//     char CPU_RunInfo[400] = {0}; // 保存任务运行时间信息
//     memset(CPU_RunInfo, 0, 400); // 信息缓冲区清零
//     vTaskGetRunTimeStats((char*)&CPU_RunInfo);
//     systemPrintf("任务名       运行计数         使用率\r\n");
//     systemPrintf("%s", CPU_RunInfo);
//     systemPrintf("---------------------------------------------\r\n\n");
// #endif

//     // 打印剩余ram和堆容量
//     systemPrintf("IDLE: ****free internal ram %d  all heap size: %d Bytes****\n",
//                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_8BIT));
//     systemPrintf("IDLE: ****free SPIRAM size: %d Bytes****\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
// }

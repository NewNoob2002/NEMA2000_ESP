#pragma once

#ifndef COMPILE_WEBSERVER
#define COMPILE_WEBSERVER
#endif

#ifndef COMPILE_WIFI
#if defined(COMPILE_WEBSERVER)
#define COMPILE_WIFI
#define COMPILE_NETWORK
#endif
#endif

#ifndef COMPILE_BT
#define COMPILE_BT
#endif

#ifndef COMPILE_NTP
// #define COMPILE_NTP
#endif

#define COMPILE_UM980

// #ifndef COMPILE_I2C
// #define COMPILE_I2C
// #endif

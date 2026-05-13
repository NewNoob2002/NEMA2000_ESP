#pragma once

#ifndef COMPILE_WEBSERVER
// #define COMPILE_WEBSERVER
#endif

#ifdef COMPILE_WEBSERVER

void webServerStart();
void webServerStop();
void webServerUpdate();
bool webServerIsRunning();
bool webServerIsConnected();
void webServerSendString(const char* stringToSend);
void webServerSendSettings();
void webServerSendFirmwareVersion();
void webServerVerifyTables();

#endif // COMPILE_WEBSERVER

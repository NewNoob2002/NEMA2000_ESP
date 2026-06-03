#pragma once

#include "CompileConfig.h"

#ifdef COMPILE_WEBSERVER

#include <cstddef>
#include <stdbool.h>
#include "esp_http_server.h"

//----------------------------------------
// Macros
//----------------------------------------

#define PAGE_HANDLER(index, page, httpMethod, type, routine)                                                           \
    {                                                                                                                  \
        {                                                                                                              \
            .uri = page,                                                                                               \
            .method = httpMethod,                                                                                      \
            .handler = routine,                                                                                        \
            .user_ctx = (void*)index,                                                                                  \
        },                                                                                                             \
        &type,                                                                                                         \
        nullptr,                                                                                                       \
        0,                                                                                                             \
    }

#define WEB_PAGE(index, page, type, data)                                                                              \
    {                                                                                                                  \
        {                                                                                                              \
            .uri = page,                                                                                               \
            .method = HTTP_GET,                                                                                        \
            .handler = webServerHandlerGetPage,                                                                        \
            .user_ctx = (void*)index,                                                                                  \
        },                                                                                                             \
        &type,                                                                                                         \
        (void*)data,                                                                                                   \
        sizeof(data),                                                                                                  \
    }

typedef struct _GET_PAGE_HANDLER {
    httpd_uri_t _page;
    const char* const* _type;
    void* _data;
    size_t _length;
} GET_PAGE_HANDLER;

typedef struct _WEB_SOCKETS_CLIENT {
    struct _WEB_SOCKETS_CLIENT* _flink;
    struct _WEB_SOCKETS_CLIENT* _blink;
    httpd_req_t* _request;
    int _socketFD;
} WEB_SOCKETS_CLIENT;

// State machine to allow web server access to network layer
typedef enum WebServerState {
    WEBSERVER_STATE_OFF = 0,
    WEBSERVER_STATE_WAIT_FOR_NETWORK,
    WEBSERVER_STATE_NETWORK_CONNECTED,
    WEBSERVER_STATE_RUNNING,

    // Add new states here
    WEBSERVER_STATE_MAX
} WebServerState;

void webServerStart();
void webServerStop();
void webServerUpdate();
bool webServerIsRunning();
bool webServerIsConnected();
void webServerSendString(const char* stringToSend);
void webServerSendSettings();
void webServerSendFirmwareVersion();
void webServerVerifyTables();

void webServerHttpdDisplayConfig(struct httpd_config* config);
#endif // COMPILE_WEBSERVER

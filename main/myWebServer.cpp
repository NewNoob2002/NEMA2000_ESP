#include "myWebServer.h"

#ifdef COMPILE_WEBSERVER

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_http_server.h>
#include <string.h>
#include "Support.h"
#include "form.h"
#include "mcu_settings.h"
#include "myWIFI.h"

//----------------------------------------
// Constants
//----------------------------------------

static const size_t webServerStackSize = 1024 * 20;
static const size_t firmwareBufferLength = 16 * 1024;

static const char* const image_png = "image/png";
static const char* const text_css = "text/css";
static const char* const text_html = "text/html";
static const char* const text_javascript = "text/javascript";
static const char* const text_plain = "text/plain";

#define UPLOAD_FIRMWARE "/uploadFirmware"

//----------------------------------------
// Locals
//----------------------------------------

static httpd_handle_t webServerHandle;
static WebServerState webServerState = WEBSERVER_STATE_OFF;

static const char* const webServerStateNames[] = {
    "WEBSERVER_STATE_OFF",
    "WEBSERVER_STATE_WAIT_FOR_NETWORK",
    "WEBSERVER_STATE_NETWORK_CONNECTED",
    "WEBSERVER_STATE_RUNNING",
};

//----------------------------------------
// Forward routines
//----------------------------------------

static esp_err_t webServerHandlerFirmwareUpload(httpd_req_t* req);
static esp_err_t webServerHandlerGetPage(httpd_req_t* req);
static esp_err_t webServerHandlerPageNotFound(httpd_req_t* req, httpd_err_code_t error);
static esp_err_t webServerHandlerWebSockets(httpd_req_t* req);
static void webServerHandleClientMessage(const char* message);

//----------------------------------------
// Web page descriptions
//----------------------------------------

const GET_PAGE_HANDLER webServerPages[] = {
    // Page shell and branding
    WEB_PAGE(0, "/", text_html, index_html),
    WEB_PAGE(1, "/favicon.ico", image_png, favicon_ico),
    WEB_PAGE(2, "/singularxyz.png", image_png, singularxyz_png),

    // JavaScript and style sheets
    WEB_PAGE(3, "/src/main.js", text_javascript, main_js),
    WEB_PAGE(4, "/src/style.css", text_css, style_css),

    // OTA
    PAGE_HANDLER(5, UPLOAD_FIRMWARE, HTTP_POST, text_plain, webServerHandlerFirmwareUpload),
};

const int webServerTotalPages = sizeof(webServerPages) / sizeof(webServerPages[0]);

static const httpd_uri_t webServerWebSocketPage = {.uri = "/ws",
                                                   .method = HTTP_GET,
                                                   .handler = webServerHandlerWebSockets,
                                                   .user_ctx = NULL,
                                                   .is_websocket = true,
                                                   .handle_ws_control_frames = true,
                                                   .supported_subprotocol = NULL};

//----------------------------------------
// Multipart helpers
//----------------------------------------

static int recvByte(httpd_req_t* req, char* value, size_t* received) {
    const int bytes = httpd_req_recv(req, value, 1);
    if (bytes == 1) {
        (*received)++;
    }
    return bytes;
}

static bool extractMultipartBoundary(httpd_req_t* req, char* boundary, size_t boundaryLength) {
    const size_t contentTypeLength = httpd_req_get_hdr_value_len(req, "Content-Type");
    if ((contentTypeLength == 0) || (contentTypeLength >= 160)) {
        return false;
    }

    char contentType[160] = {};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", contentType, sizeof(contentType)) != ESP_OK) {
        return false;
    }

    const char* marker = strstr(contentType, "boundary=");
    if (marker == nullptr) {
        return false;
    }

    marker += strlen("boundary=");
    snprintf(boundary, boundaryLength, "\r\n--%s", marker);
    return true;
}

static bool readMultipartHeader(httpd_req_t* req, char* header, size_t headerLength, size_t* received) {
    size_t index = 0;
    uint8_t matched = 0;

    while (index + 1 < headerLength) {
        char value;
        if (recvByte(req, &value, received) != 1) {
            return false;
        }

        header[index++] = value;
        switch (matched) {
            case 0: matched = (value == '\r') ? 1 : 0; break;
            case 1: matched = (value == '\n') ? 2 : ((value == '\r') ? 1 : 0); break;
            case 2: matched = (value == '\r') ? 3 : 0; break;
            case 3:
                if (value == '\n') {
                    header[index] = 0;
                    return true;
                }
                matched = 0;
                break;
        }
    }

    header[headerLength - 1] = 0;
    return false;
}

static int findBytes(const uint8_t* data, size_t dataLength, const char* needle, size_t needleLength) {
    if ((needleLength == 0) || (dataLength < needleLength)) {
        return -1;
    }

    for (size_t index = 0; index <= dataLength - needleLength; index++) {
        if (memcmp(&data[index], needle, needleLength) == 0) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

//----------------------------------------
// Handlers
//----------------------------------------

static esp_err_t webServerHandlerGetPage(httpd_req_t* req) {
    const uintptr_t index = reinterpret_cast<uintptr_t>(req->user_ctx);
    if (index >= static_cast<uintptr_t>(webServerTotalPages)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid page index");
    }

    const GET_PAGE_HANDLER* page = &webServerPages[index];
    if (settings.debugWebServer) {
        systemPrintf("WebServer GET %s (%u bytes)\r\n", req->uri, static_cast<unsigned>(page->_length));
    }

    httpd_resp_set_type(req, *page->_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, reinterpret_cast<const char*>(page->_data), page->_length);
}

static esp_err_t webServerHandlerFirmwareUpload(httpd_req_t* req) {
    char boundary[96] = {};
    char header[768] = {};
    uint8_t* buffer = nullptr;
    const char* errorMessage = nullptr;
    bool updateRunning = false;
    size_t received = 0;
    size_t firmwareBytes = 0;

    if (settings.debugWebServer) {
        systemPrintf("WebServer POST %s, content length %u\r\n", req->uri, static_cast<unsigned>(req->content_len));
    }

    do {
        if (!extractMultipartBoundary(req, boundary, sizeof(boundary))) {
            errorMessage = "Invalid multipart boundary";
            break;
        }
        if (!readMultipartHeader(req, header, sizeof(header), &received)) {
            errorMessage = "Invalid multipart header";
            break;
        }

        const char* fileName = strstr(header, "filename=\"");
        if (fileName == nullptr) {
            errorMessage = "Missing firmware filename";
            break;
        }
        fileName += strlen("filename=\"");
        const char* fileNameEnd = strchr(fileName, '"');
        if ((fileNameEnd == nullptr) || (fileNameEnd <= fileName) || (strstr(fileName, ".bin") == nullptr)) {
            errorMessage = "Firmware must be a .bin file";
            break;
        }

        buffer = static_cast<uint8_t*>(rtkMalloc(firmwareBufferLength, "OTA upload buffer"));
        if (buffer == nullptr) {
            errorMessage = "Failed to allocate OTA upload buffer";
            break;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            errorMessage = "Failed to start OTA update";
            break;
        }
        updateRunning = true;

        const size_t boundaryLength = strlen(boundary);
        const size_t overlapLength = boundaryLength - 1;
        size_t buffered = 0;

        while (received < req->content_len) {
            const size_t remainingRequest = req->content_len - received;
            size_t requestBytes = firmwareBufferLength - buffered;
            if (requestBytes > remainingRequest) {
                requestBytes = remainingRequest;
            }

            const int bytesRead = httpd_req_recv(req, reinterpret_cast<char*>(&buffer[buffered]), requestBytes);
            if (bytesRead <= 0) {
                errorMessage = "Failed to receive firmware data";
                break;
            }
            received += bytesRead;
            buffered += bytesRead;

            const int boundaryIndex = findBytes(buffer, buffered, boundary, boundaryLength);
            if (boundaryIndex >= 0) {
                if (boundaryIndex > 0) {
                    const size_t written = Update.write(buffer, boundaryIndex);
                    if (written != static_cast<size_t>(boundaryIndex)) {
                        errorMessage = "Failed to write final OTA data";
                        break;
                    }
                    firmwareBytes += written;
                }
                buffered = 0;
                break;
            }

            if (buffered > overlapLength) {
                const size_t writable = buffered - overlapLength;
                const size_t written = Update.write(buffer, writable);
                if (written != writable) {
                    errorMessage = "Failed to write OTA data";
                    break;
                }
                firmwareBytes += written;
                memmove(buffer, &buffer[writable], overlapLength);
                buffered = overlapLength;
            }
        }

        if (errorMessage != nullptr) {
            break;
        }

        while (received < req->content_len) {
            const size_t remainingRequest = req->content_len - received;
            const size_t requestBytes = (remainingRequest > firmwareBufferLength) ? firmwareBufferLength : remainingRequest;
            const int bytesRead = httpd_req_recv(req, reinterpret_cast<char*>(buffer), requestBytes);
            if (bytesRead <= 0) {
                errorMessage = "Failed to drain OTA request";
                break;
            }
            received += bytesRead;
        }
        if (errorMessage != nullptr) {
            break;
        }

        if (!Update.end(true)) {
            errorMessage = Update.errorString();
            break;
        }

        systemPrintf("Firmware update complete: %u bytes. Restarting\r\n", static_cast<unsigned>(firmwareBytes));
        httpd_resp_set_type(req, text_plain);
        httpd_resp_sendstr(req, "Firmware uploaded successfully. Restarting.");
        delay(500);
        ESP.restart();
        return ESP_OK;
    } while (0);

    if (updateRunning) {
        Update.abort();
        Update.printError(Serial);
    }
    if (buffer != nullptr) {
        rtkFree(buffer, "OTA upload buffer");
    }

    systemPrintf("ERROR: Firmware upload failed: %s\r\n", errorMessage ? errorMessage : "unknown error");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errorMessage ? errorMessage : "Firmware upload failed");
    return ESP_FAIL;
}

static esp_err_t webServerHandlerWebSockets(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        systemPrintln("WebServer WS connected");
        return ESP_OK;
    }

    httpd_ws_frame_t packet = {};
    const esp_err_t status = httpd_ws_recv_frame(req, &packet, 0);
    if (status != ESP_OK) {
        return status;
    }

    if (packet.len == 0) {
        return ESP_OK;
    }

    uint8_t* payload = static_cast<uint8_t*>(rtkMalloc(packet.len + 1, "WebSocket payload"));
    if (payload == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    packet.payload = payload;
    const esp_err_t readStatus = httpd_ws_recv_frame(req, &packet, packet.len);
    if (readStatus == ESP_OK) {
        payload[packet.len] = 0;
        webServerHandleClientMessage(reinterpret_cast<const char*>(payload));

        char ack[160];
        snprintf(ack, sizeof(ack), "ack,%s", reinterpret_cast<const char*>(payload));

        httpd_ws_frame_t response = {};
        response.type = HTTPD_WS_TYPE_TEXT;
        response.payload = reinterpret_cast<uint8_t*>(ack);
        response.len = strlen(ack);
        httpd_ws_send_frame(req, &response);
    }
    rtkFree(payload, "WebSocket payload");
    return readStatus;
}

static void webServerHandleClientMessage(const char* message) {
    systemPrintf("WebServer WS RX: %s\r\n", message);

    const char* cursor = message;
    while ((cursor != nullptr) && (*cursor != 0)) {
        const char* comma = strchr(cursor, ',');
        if (comma == nullptr) {
            break;
        }

        const char* value = comma + 1;
        const char* next = strchr(value, ',');
        if (next == nullptr) {
            break;
        }

        char key[64] = {};
        char settingValue[96] = {};
        const size_t keyLength = (static_cast<size_t>(comma - cursor) >= sizeof(key)) ? sizeof(key) - 1
                                                                                       : static_cast<size_t>(comma - cursor);
        const size_t valueLength = (static_cast<size_t>(next - value) >= sizeof(settingValue))
                                       ? sizeof(settingValue) - 1
                                       : static_cast<size_t>(next - value);
        memcpy(key, cursor, keyLength);
        memcpy(settingValue, value, valueLength);

        // Placeholder dispatch point. Implement setting handlers here as each feature is wired up.
        systemPrintf("  action: %s = %s\r\n", key, settingValue);
        cursor = next + 1;
    }
}

static esp_err_t webServerHandlerPageNotFound(httpd_req_t* req, httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, nullptr, 0);
}

//----------------------------------------
// Registration
//----------------------------------------

static bool webServerRegisterErrorHandler(httpd_err_code_t error, httpd_err_handler_func_t handler) {
    const esp_err_t status = httpd_register_err_handler(webServerHandle, error, handler);
    if (settings.debugWebServer) {
        systemPrintf("WebServer %s %d error handler\r\n", (status == ESP_OK) ? "registered" : "failed to register",
                     error);
    }
    return status == ESP_OK;
}

static bool webServerRegisterPageHandler(const httpd_uri_t* page) {
    const esp_err_t status = httpd_register_uri_handler(webServerHandle, page);
    if (settings.debugWebServer) {
        systemPrintf("WebServer %s %s handler\r\n", (status == ESP_OK) ? "registered" : "failed to register",
                     page->uri);
    }
    return status == ESP_OK;
}

static bool webServerAssignResources() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = settings.httpPort ? settings.httpPort : 80;
    config.stack_size = webServerStackSize;
    config.max_uri_handlers = webServerTotalPages + 4;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (settings.debugWebServer) {
        webServerHttpdDisplayConfig(&config);
        systemPrintf("Web server starting on port: %d\r\n", config.server_port);
    }

    if (MDNS.begin(&settings.mdnsHostName[0]) && settings.mdnsEnable) {
        MDNS.addService("http", "tcp", config.server_port);
    }

    esp_err_t status = httpd_start(&webServerHandle, &config);
    if (status != ESP_OK) {
        systemPrintf("ERROR: Web server failed to start: %s\r\n", esp_err_to_name(status));
        return false;
    }

    bool success = webServerRegisterErrorHandler(HTTPD_404_NOT_FOUND, webServerHandlerPageNotFound)
                   && webServerRegisterPageHandler(&webServerWebSocketPage);
    for (int index = 0; success && (index < webServerTotalPages); index++) {
        success = webServerRegisterPageHandler(&webServerPages[index]._page);
    }

    if (!success) {
        webServerStop();
        return false;
    }

    webServerState = WEBSERVER_STATE_RUNNING;
    systemPrintln("Web Server Started");
    return true;
}

//----------------------------------------
// State machine
//----------------------------------------

void webServerStart() {
    if (!wifiSoftApRunning()) {
        wifiSoftApOn(__FILE__, __LINE__);
    }
    if (webServerState == WEBSERVER_STATE_OFF) {
        webServerState = WEBSERVER_STATE_WAIT_FOR_NETWORK;
    }
}

void webServerStop() {
    if (webServerHandle != nullptr) {
        httpd_stop(webServerHandle);
        webServerHandle = nullptr;
    }
    webServerState = WEBSERVER_STATE_OFF;
    systemPrintln("Web Server Stopped");
}

void webServerUpdate() {
    if (!wifiSoftApRunning()) {
        if (webServerState == WEBSERVER_STATE_RUNNING) {
            webServerStop();
        }
        return;
    }

    if ((webServerState == WEBSERVER_STATE_WAIT_FOR_NETWORK) || (webServerState == WEBSERVER_STATE_NETWORK_CONNECTED)) {
        webServerState = WEBSERVER_STATE_NETWORK_CONNECTED;
        webServerAssignResources();
    }
}

bool webServerIsRunning() {
    return webServerState == WEBSERVER_STATE_RUNNING;
}

bool webServerIsConnected() {
    return webServerIsRunning();
}

void webServerSendString(const char* stringToSend) {
    (void)stringToSend;
}

void webServerSendSettings() {}

void webServerSendFirmwareVersion() {}

void webServerVerifyTables() {
    const int webServerStateEntries = sizeof(webServerStateNames) / sizeof(webServerStateNames[0]);
    if (webServerStateEntries != WEBSERVER_STATE_MAX) {
        reportFatalError("Fix webServerStateNames to match WebServerState");
    }
}

void webServerHttpdDisplayConfig(struct httpd_config* config) {
    systemPrintf("httpd_config: port=%d stack=%d handlers=%d sockets=%d\r\n", config->server_port, config->stack_size,
                 config->max_uri_handlers, config->max_open_sockets);
}

#endif // COMPILE_WEBSERVER

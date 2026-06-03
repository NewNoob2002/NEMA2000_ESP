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

static const size_t webServerStackSize = 1024 * 20;
static const size_t firmwareBufferLength = 16384;

static httpd_handle_t webServerHandle;
static SemaphoreHandle_t webServerMutex;
static WEB_SOCKETS_CLIENT* webServerClientListHead;
static WEB_SOCKETS_CLIENT* webServerClientListTail;
static WebServerState webServerStarted = WEBSERVER_STATE_OFF;

static const char* const webServerStateNames[] = {
    "WEBSERVER_STATE_OFF",
    "WEBSERVER_STATE_WAIT_FOR_NETWORK",
    "WEBSERVER_STATE_NETWORK_CONNECTED",
    "WEBSERVER_STATE_RUNNING",
};
static const int webServerStateEntries = sizeof(webServerStateNames) / sizeof(webServerStateNames[0]);

// These are the various files or endpoints that browsers will attempt to
// access to see if internet access is available.  If one is requested,
// redirect user to captive portal (main page "/").
const char* webServerCaptiveUrls[] = {
    "canonical.html", "check_network_status.txt", "chrome-variations/seed",    "connecttest.txt",
    "generate_204",   "hotspot-detect.html",      "library/test/success.html", "ncsi.txt",
    "success.txt",
};
const uint8_t webServerCaptiveUrlCount = sizeof(webServerCaptiveUrls) / sizeof(webServerCaptiveUrls[0]);

//----------------------------------------
// Forward routines
//----------------------------------------
static esp_err_t webServerHandlerWebSockets(httpd_req_t* req);
//----------------------------------------
// Web page descriptions
//----------------------------------------
const char* const image_png = "image/png";
const char* const text_css = "text/css";
const char* const text_html = "text/html";
const char* const text_javascript = "text/javascript";
const char* const text_plain = "text/plain";

#define UPLOAD_FIRMWARE "/uploadFirmware"
#define UPLOAD_PATH     "/uploadFile"
const char* fileNameParameter = "filename=\"";

const GET_PAGE_HANDLER webServerPages[] = {
    WEB_PAGE(0, "/src/sparkpnt_device_setup.png", image_png, sparkpnt_device_setup_png),
    WEB_PAGE(1, "/src/sparkfun_device_setup.png", image_png, sparkfun_device_setup_png),

    // Page icon
    WEB_PAGE(2, "/favicon.ico", text_plain, favicon_ico),

    // Fonts
    WEB_PAGE(3, "/src/fonts/icomoon.eot", text_plain, icomoon_eot),
    WEB_PAGE(4, "/src/fonts/icomoon.svg", text_plain, icomoon_svg),
    WEB_PAGE(5, "/src/fonts/icomoon.ttf", text_plain, icomoon_ttf),
    WEB_PAGE(6, "/src/fonts/icomoon.woof", text_plain, icomoon_woof),

    // Bootstrap
    WEB_PAGE(16, "/src/bootstrap.bundle.min.js", text_javascript, bootstrap_bundle_min_js),
    WEB_PAGE(17, "/src/bootstrap.min.js", text_javascript, bootstrap_min_js),

    // Java script
    WEB_PAGE(18, "/src/jquery-3.6.0.min.js", text_javascript, jquery_js),
    WEB_PAGE(19, "/src/main.js", text_javascript, main_js),

    // Style sheets
    WEB_PAGE(20, "/src/bootstrap.min.css", text_css, bootstrap_min_css),
    WEB_PAGE(21, "/src/style.css", text_css, style_css),

    // File pages
    PAGE_HANDLER(22, "/listfiles", HTTP_GET, text_plain, webServerHandlerFileList),
    PAGE_HANDLER(23, "/file", HTTP_GET, text_plain, webServerHandlerFileManager),
    PAGE_HANDLER(24, UPLOAD_FIRMWARE, HTTP_POST, text_plain, webServerHandlerFirmwareUpload),

    // Message handlers
    PAGE_HANDLER(25, "/listMessages", HTTP_GET, text_plain, webServerHandlerListMessages),
    PAGE_HANDLER(26, "/listMessagesBase", HTTP_GET, text_plain, webServerHandlerListBaseMessages),
    PAGE_HANDLER(27, UPLOAD_PATH, HTTP_POST, text_plain, webServerHandlerFileUpload),

    // Add pages above this line
    WEB_PAGE(28, "/", text_html, index_html),
};

const int webServerTotalPages = (sizeof(webServerPages) / sizeof(GET_PAGE_HANDLER));

static const httpd_uri_t webServerPage = {.uri = "/ws",
                                          .method = HTTP_GET,
                                          .handler = webServerHandlerWebSockets,
                                          .user_ctx = NULL,
                                          .is_websocket = true,
                                          .handle_ws_control_frames = true,
                                          .supported_subprotocol = NULL};

static esp_err_t
webServerHandlerGetStatic(httpd_req_t* req) {
    const STATIC_PAGE* page = static_cast<const STATIC_PAGE*>(req->user_ctx);

    if (settings.debugWebServer) {
        systemPrintf("WebServer GET %s (%u bytes)\r\n", req->uri, static_cast<unsigned>(page->length));
    }

    httpd_resp_set_type(req, page->contentType);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, reinterpret_cast<const char*>(page->data), page->length);
}

//----------------------------------------
// Handler for file uploads (firmware or general files)
//----------------------------------------
static esp_err_t
webServerHandlerFirmwareUpload(httpd_req_t* req) {}

//----------------------------------------
// Handler for web sockets requests
//----------------------------------------
static esp_err_t
webServerHandlerWebSockets(httpd_req_t* req) {}

static esp_err_t
webServerHandlerNotFound(httpd_req_t* req, httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, nullptr, 0);
}

//----------------------------------------
// Register an error handler
//----------------------------------------
bool
webServerRegisterErrorHandler(httpd_err_code_t error, httpd_err_handler_func_t handler) {
    esp_err_t status;

    // Register the error handler
    status = httpd_register_err_handler(webServerHandle, error, handler);
    if (settings.debugWebServer == true) {
        if (status == ESP_OK) {
            systemPrintf("WebServer registered %d error handler\r\n", error);
        } else {
            systemPrintf("WebServer Failed to register %d error handler!\r\n", error);
        }
    }
    return (status == ESP_OK);
}

//----------------------------------------
// Register a webpage handler
//----------------------------------------
bool
webServerRegisterPageHandler(const httpd_uri_t* page) {
    esp_err_t status;

    // Register the handler
    status = httpd_register_uri_handler(webServerHandle, page);
    if (settings.debugWebServer == true) {
        if (status == ESP_OK) {
            systemPrintf("WebServer registered %s handler\r\n", page->uri);
        } else {
            systemPrintf("WebServer Failed to register %s handler!\r\n", page->uri);
        }
    }
    return (status == ESP_OK);
}

static bool
webServerAssignResources() {
    if (settings.debugWebServer) {
        systemPrintln("Assigning web server resources");
    }
    do {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = settings.httpPort ? settings.httpPort : 80;
        config.stack_size = webServerStackSize;
        config.max_uri_handlers = (sizeof(staticPages) / sizeof(staticPages[0])) + 4;
        config.max_open_sockets = 4;
        config.lru_purge_enable = true;

        if (settings.debugWebServer == true) {
            webServerHttpdDisplayConfig(&config);
            reportHeapNow(true);
        }

        if (MDNS.begin(&settings.mdnsHostName[0]) && settings.mdnsEnable) {
            MDNS.addService("http", "tcp", config.server_port);
            if (settings.debugNetworkLayer) {
                systemPrintf("mDNS started as %s.local\r\n", settings.mdnsHostName);
            }
        }

        // Allocate the mutex
        if (webServerMutex == nullptr) {
            webServerMutex = xSemaphoreCreateMutex();
            if (webServerMutex == nullptr) {
                if (settings.debugWebServer) {
                    systemPrintf("ERROR: Web server failed to allocate the mutex!\r\n");
                }
                break;
            }
        }

        // Start the web server
        if (settings.debugWebServer == true) {
            systemPrintf("Web server starting on port: %d\r\n", config.server_port);
        }
        esp_err_t status = httpd_start(&webServerHandle, &config);
        if (status != ESP_OK) {
            systemPrintf("ERROR: Web server failed to start: %s\r\n", esp_err_to_name(status));
            return false;
        }

        if (settings.debugWebServer == true) {
            systemPrintln("WebServer registering page handlers");
        }

        // Register the page not found (404) error handler
        httpd_register_err_handler(webServerHandle, HTTPD_404_NOT_FOUND, webServerHandlerNotFound);

        // Register the web socket handler
        if (!webServerRegisterPageHandler(&webServerPage)) {
            break;
        }

        // Register the main pages
        for (i = 0; i < webServerTotalPages; i++) {
            if (!webServerRegisterPageHandler(&webServerPages[i]._page)) {
                break;
            }
        }
        if (i < webServerTotalPages) {
            break;
        }

        if (settings.debugWebServer == true) {
            systemPrintln("Web Server Started");
            reportHeapNow(true);
        }
        online_devices.webServer = true;
        webServerStarted = true;
        return true;
    } while (0);

    // Release the resources
    if (settings.debugWebServer == true) {
        reportHeapNow(true);
    }
    // webServerReleaseResources();
    return false;
}

void
webServerStart() {
    if (!wifiSoftApRunning()) {
        wifiSoftApOn(__FILE__, __LINE__);
    }
}

void
webServerStop() {
    if (webServerHandle != nullptr) {
        httpd_stop(webServerHandle);
        webServerHandle = nullptr;
        webServerStarted = false;
        systemPrintln("Web Server Stopped");
    }
}

void
webServerUpdate() {
    if (!wifiSoftApRunning()) {
        if (webServerStarted) {
            webServerStop();
        }
        return;
    }

    if (!webServerStarted) {
        webServerAssignResources();
    }
}

bool
webServerIsRunning() {
    return webServerStarted;
}

bool
webServerIsConnected() {
    return webServerStarted;
}

void
webServerSendString(const char* stringToSend) {
    (void)stringToSend;
}

void
webServerSendSettings() {}

void
webServerSendFirmwareVersion() {}

void
webServerVerifyTables() {}

//----------------------------------------
// Display the HTTPD configuration
//----------------------------------------
void
webServerHttpdDisplayConfig(struct httpd_config* config) {
    /*
    httpd_config object:
            5: task_priority
        20480: stack_size
    2147483647: core_id
            81: server_port
        32768: ctrl_port
            7: max_open_sockets
            8: max_uri_handlers
            8: max_resp_headers
            5: backlog_conn
        false: lru_purge_enable
            5: recv_wait_timeout
            5: send_wait_timeout
    0x0: global_user_ctx
    0x0: global_user_ctx_free_fn
    0x0: global_transport_ctx
    0x0: global_transport_ctx_free_fn
        false: enable_so_linger
            0: linger_timeout
        false: keep_alive_enable
            0: keep_alive_idle
            0: keep_alive_interval
            0: keep_alive_count
    0x0: open_fn
    0x0: close_fn
    0x0: uri_match_fn
    */
    systemPrintf("httpd_config object:\r\n");
    systemPrintf("%10d: task_priority\r\n", config->task_priority);
    systemPrintf("%10d: stack_size\r\n", config->stack_size);
    systemPrintf("%10d: core_id\r\n", config->core_id);
    systemPrintf("%10d: server_port\r\n", config->server_port);
    systemPrintf("%10d: ctrl_port\r\n", config->ctrl_port);
    systemPrintf("%10d: max_open_sockets\r\n", config->max_open_sockets);
    systemPrintf("%10d: max_uri_handlers\r\n", config->max_uri_handlers);
    systemPrintf("%10d: max_resp_headers\r\n", config->max_resp_headers);
    systemPrintf("%10d: backlog_conn\r\n", config->backlog_conn);
    systemPrintf("%10s: lru_purge_enable\r\n", config->lru_purge_enable ? "true" : "false");
    systemPrintf("%10d: recv_wait_timeout\r\n", config->recv_wait_timeout);

    systemPrintf("%10d: send_wait_timeout\r\n", config->send_wait_timeout);
    systemPrintf("%p: global_user_ctx\r\n", config->global_user_ctx);
    systemPrintf("%p: global_user_ctx_free_fn\r\n", config->global_user_ctx_free_fn);
    systemPrintf("%p: global_transport_ctx\r\n", config->global_transport_ctx);
    systemPrintf("%p: global_transport_ctx_free_fn\r\n", (void*)config->global_transport_ctx_free_fn);
    systemPrintf("%10s: enable_so_linger\r\n", config->enable_so_linger ? "true" : "false");
    systemPrintf("%10d: linger_timeout\r\n", config->linger_timeout);
    systemPrintf("%10s: keep_alive_enable\r\n", config->keep_alive_enable ? "true" : "false");
    systemPrintf("%10d: keep_alive_idle\r\n", config->keep_alive_idle);
    systemPrintf("%10d: keep_alive_interval\r\n", config->keep_alive_interval);
    systemPrintf("%10d: keep_alive_count\r\n", config->keep_alive_count);
    systemPrintf("%p: open_fn\r\n", (void*)config->open_fn);
    systemPrintf("%p: close_fn\r\n", (void*)config->close_fn);
    systemPrintf("%p: uri_match_fn\r\n", (void*)config->uri_match_fn);
}

#endif // COMPILE_WEBSERVER

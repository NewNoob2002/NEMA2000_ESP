#include "myWebServer.h"

#ifdef COMPILE_WEBSERVER

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <cstring>
#include "Support.h"
#include "mcu_settings.h"
#include "myWIFI.h"

namespace {

constexpr size_t kMaxFormBodyBytes = 512;
constexpr uint16_t kDefaultHttpPort = 80;

enum WebServerState : uint8_t {
    WEBSERVER_STATE_OFF = 0,
    WEBSERVER_STATE_WAIT_FOR_AP,
    WEBSERVER_STATE_RUNNING,
};

httpd_handle_t webServerHandle = nullptr;
WebServerState webServerState = WEBSERVER_STATE_OFF;
bool webServerRequested = false;
bool webServerMdnsStarted = false;

const char* webServerStateName() {
    switch (webServerState) {
        case WEBSERVER_STATE_OFF: return "OFF";
        case WEBSERVER_STATE_WAIT_FOR_AP: return "WAIT_FOR_AP";
        case WEBSERVER_STATE_RUNNING: return "RUNNING";
    }
    return "UNKNOWN";
}

void sendNoCacheHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
}

String htmlEscape(const char* value) {
    String escaped;
    if (value == nullptr) {
        return escaped;
    }

    for (const char* cursor = value; *cursor; cursor++) {
        switch (*cursor) {
            case '&': escaped += F("&amp;"); break;
            case '<': escaped += F("&lt;"); break;
            case '>': escaped += F("&gt;"); break;
            case '"': escaped += F("&quot;"); break;
            case '\'': escaped += F("&#39;"); break;
            default: escaped += *cursor; break;
        }
    }
    return escaped;
}

String jsonEscape(const char* value) {
    String escaped;
    if (value == nullptr) {
        return escaped;
    }

    for (const char* cursor = value; *cursor; cursor++) {
        switch (*cursor) {
            case '\\': escaped += F("\\\\"); break;
            case '"': escaped += F("\\\""); break;
            case '\b': escaped += F("\\b"); break;
            case '\f': escaped += F("\\f"); break;
            case '\n': escaped += F("\\n"); break;
            case '\r': escaped += F("\\r"); break;
            case '\t': escaped += F("\\t"); break;
            default: escaped += *cursor; break;
        }
    }
    return escaped;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

String urlDecode(const String& value) {
    String decoded;
    decoded.reserve(value.length());

    for (size_t index = 0; index < value.length(); index++) {
        const char current = value[index];
        if (current == '+') {
            decoded += ' ';
        } else if ((current == '%') && ((index + 2) < value.length())) {
            const int high = hexDigit(value[index + 1]);
            const int low = hexDigit(value[index + 2]);
            if ((high >= 0) && (low >= 0)) {
                decoded += static_cast<char>((high << 4) | low);
                index += 2;
            } else {
                decoded += current;
            }
        } else {
            decoded += current;
        }
    }
    return decoded;
}

String formValue(const String& body, const char* key) {
    const String token = String(key) + "=";
    int start = body.indexOf(token);
    if (start < 0) {
        return "";
    }

    start += token.length();
    int end = body.indexOf('&', start);
    if (end < 0) {
        end = body.length();
    }
    return urlDecode(body.substring(start, end));
}

bool readRequestBody(httpd_req_t* req, String& body) {
    if (req->content_len > kMaxFormBodyBytes) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form body is too large");
        return false;
    }

    body = "";
    body.reserve(req->content_len + 1);

    char buffer[128];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const int received = httpd_req_recv(req, buffer, chunk);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request body");
            return false;
        }

        body.concat(buffer, received);
        remaining -= static_cast<size_t>(received);
    }
    return true;
}

void copySettingString(char* destination, size_t destinationBytes, const String& source) {
    if (destinationBytes == 0) {
        return;
    }

    const size_t bytesToCopy = source.length() < (destinationBytes - 1) ? source.length() : (destinationBytes - 1);
    memcpy(destination, source.c_str(), bytesToCopy);
    destination[bytesToCopy] = '\0';
}

String buildIndexPage() {
    const IPAddress apIp = wifiSoftApGetIpAddress();
    const char* apSsid = wifiSoftApGetSsid();
    const bool apOnline = online_devices.wifi.wifiSoftApOnline;
    const bool clientConnected = online_devices.wifi.wifiSoftApConnected;

    String page;
    page.reserve(4096);
    page += F("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
    page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    page += F("<title>RTK WiFi AP</title><style>");
    page += F("body{margin:0;font-family:Arial,sans-serif;background:#f5f7f9;color:#18212b}");
    page += F("main{max-width:760px;margin:0 auto;padding:28px 18px}");
    page += F("h1{font-size:26px;margin:0 0 18px}h2{font-size:18px;margin:28px 0 12px}");
    page += F(".panel{background:#fff;border:1px solid #d9e0e7;border-radius:6px;padding:18px;margin:14px 0}");
    page += F(".grid{display:grid;grid-template-columns:160px 1fr;gap:10px 14px}");
    page += F("label{font-weight:600}input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #b9c3cc;border-radius:4px}");
    page += F("button{padding:10px 14px;border:0;border-radius:4px;background:#005d8f;color:#fff;font-weight:700}");
    page += F(".ok{color:#146c2e}.warn{color:#9a4b00}.hint{color:#5c6873;font-size:14px}");
    page += F("</style></head><body><main><h1>RTK WiFi AP</h1>");

    page += F("<section class=\"panel\"><h2>Access Point</h2><div class=\"grid\">");
    page += F("<label>Status</label><div class=\"");
    page += apOnline ? F("ok\">Online") : F("warn\">Starting");
    page += F("</div><label>SSID</label><div>");
    page += htmlEscape(apSsid);
    page += F("</div><label>IP</label><div>");
    page += apIp.toString();
    page += F("</div><label>Client</label><div>");
    page += clientConnected ? F("Connected") : F("Not connected");
    page += F("</div></div></section>");

    page += F("<section class=\"panel\"><h2>WiFi Network</h2>");
    page += F("<form method=\"post\" action=\"/api/wifi\"><div class=\"grid\">");
    page += F("<label for=\"ssid\">SSID</label><input id=\"ssid\" name=\"ssid\" maxlength=\"31\" value=\"");
    page += htmlEscape(settings.wifiNetworks[0].ssid);
    page += F("\"><label for=\"password\">Password</label><input id=\"password\" name=\"password\" maxlength=\"31\" type=\"password\" value=\"");
    page += htmlEscape(settings.wifiNetworks[0].password);
    page += F("\"></div><p><button type=\"submit\">Save</button></p>");
    page += F("<p class=\"hint\">Saved values are applied to the in-memory WiFi settings. Station provisioning can be wired later.</p>");
    page += F("</form></section>");

    page += F("</main></body></html>");
    return page;
}

esp_err_t sendRedirect(httpd_req_t* req, const char* location) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "", 0);
}

esp_err_t handleRoot(httpd_req_t* req) {
    const String page = buildIndexPage();
    sendNoCacheHeaders(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.c_str(), page.length());
}

esp_err_t handleStatus(httpd_req_t* req) {
    const IPAddress apIp = wifiSoftApGetIpAddress();
    const char* apSsid = wifiSoftApGetSsid();
    String response;
    response.reserve(256);
    response += F("{\"apOnline\":");
    response += online_devices.wifi.wifiSoftApOnline ? F("true") : F("false");
    response += F(",\"apRunning\":");
    response += online_devices.wifi.wifiSoftApRunning ? F("true") : F("false");
    response += F(",\"clientConnected\":");
    response += online_devices.wifi.wifiSoftApConnected ? F("true") : F("false");
    response += F(",\"ssid\":\"");
    response += jsonEscape(apSsid);
    response += F("\",\"ip\":\"");
    response += apIp.toString();
    response += F("\",\"serverState\":\"");
    response += webServerStateName();
    response += F("\"}");

    sendNoCacheHeaders(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.length());
}

esp_err_t handleWifiPost(httpd_req_t* req) {
    String body;
    if (!readRequestBody(req, body)) {
        return ESP_FAIL;
    }

    const String ssid = formValue(body, "ssid");
    const String password = formValue(body, "password");

    copySettingString(settings.wifiNetworks[0].ssid, sizeof(settings.wifiNetworks[0].ssid), ssid);
    copySettingString(settings.wifiNetworks[0].password, sizeof(settings.wifiNetworks[0].password), password);
    wifiUpdateSettings();

    if (settings.debugWebServer) {
        systemPrintf("WebServer: updated WiFi network 1 SSID to '%s'\r\n", settings.wifiNetworks[0].ssid);
    }

    return sendRedirect(req, "/");
}

esp_err_t handleCaptive(httpd_req_t* req) {
    return sendRedirect(req, "/");
}

esp_err_t handleNotFound(httpd_req_t* req, httpd_err_code_t) {
    if (settings.enableCaptivePortal) {
        return sendRedirect(req, "/");
    }

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

bool registerUri(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t descriptor = {};
    descriptor.uri = uri;
    descriptor.method = method;
    descriptor.handler = handler;
    descriptor.user_ctx = nullptr;

    const esp_err_t status = httpd_register_uri_handler(webServerHandle, &descriptor);
    if ((status != ESP_OK) && settings.debugWebServer) {
        systemPrintf("WebServer: failed to register %s, status: %s\r\n", uri, esp_err_to_name(status));
    }
    return status == ESP_OK;
}

bool startHttpServer() {
    if (webServerHandle != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = settings.httpPort ? settings.httpPort : kDefaultHttpPort;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 10;

    esp_err_t status = httpd_start(&webServerHandle, &config);
    if (status != ESP_OK) {
        if (settings.debugWebServer) {
            systemPrintf("WebServer: failed to start on port %u, status: %s\r\n", config.server_port,
                         esp_err_to_name(status));
        }
        webServerHandle = nullptr;
        return false;
    }

    if (!registerUri("/", HTTP_GET, handleRoot) || !registerUri("/api/status", HTTP_GET, handleStatus)
        || !registerUri("/api/wifi", HTTP_POST, handleWifiPost) || !registerUri("/generate_204", HTTP_GET, handleCaptive)
        || !registerUri("/hotspot-detect.html", HTTP_GET, handleCaptive)
        || !registerUri("/connecttest.txt", HTTP_GET, handleCaptive)
        || (httpd_register_err_handler(webServerHandle, HTTPD_404_NOT_FOUND, handleNotFound) != ESP_OK)) {
        httpd_stop(webServerHandle);
        webServerHandle = nullptr;
        return false;
    }

    if (settings.mdnsEnable && settings.mdnsHostName[0]) {
        if (MDNS.begin(settings.mdnsHostName)) {
            MDNS.addService("http", "tcp", config.server_port);
            webServerMdnsStarted = true;
        } else if (settings.debugWebServer) {
            systemPrintf("WebServer: failed to start mDNS for %s.local\r\n", settings.mdnsHostName);
        }
    }

    if (settings.debugWebServer) {
        systemPrintf("WebServer: running at http://%s:%u/ on AP '%s'\r\n", WiFi.AP.localIP().toString().c_str(),
                     config.server_port, wifiSoftApGetSsid());
    }
    return true;
}

void stopHttpServer() {
    if (webServerMdnsStarted) {
        MDNS.end();
        webServerMdnsStarted = false;
    }

    if (webServerHandle != nullptr) {
        const esp_err_t status = httpd_stop(webServerHandle);
        if ((status != ESP_OK) && settings.debugWebServer) {
            systemPrintf("WebServer: failed to stop, status: %s\r\n", esp_err_to_name(status));
        }
        webServerHandle = nullptr;
    }
}

} // namespace

void webServerStart() {
    webServerRequested = true;
    if (webServerState == WEBSERVER_STATE_OFF) {
        webServerState = WEBSERVER_STATE_WAIT_FOR_AP;
    }

    if (!online_devices.wifi.wifiSoftApRunning) {
        wifiSoftApOn(__FILE__, __LINE__);
    }
}

void webServerStop() {
    webServerRequested = false;
    stopHttpServer();

    if (online_devices.wifi.wifiSoftApRunning) {
        wifiSoftApOff(__FILE__, __LINE__);
    }
    webServerState = WEBSERVER_STATE_OFF;
}

void webServerUpdate() {
    wifiUpdate();

    if (!webServerRequested) {
        return;
    }

    if (!online_devices.wifi.wifiSoftApRunning) {
        wifiSoftApOn(__FILE__, __LINE__);
        webServerState = WEBSERVER_STATE_WAIT_FOR_AP;
        return;
    }

    if (!online_devices.wifi.wifiSoftApOnline) {
        webServerState = WEBSERVER_STATE_WAIT_FOR_AP;
        return;
    }

    if (webServerHandle == nullptr) {
        if (startHttpServer()) {
            webServerState = WEBSERVER_STATE_RUNNING;
        }
        return;
    }

    webServerState = WEBSERVER_STATE_RUNNING;
}

bool webServerIsRunning() {
    return webServerHandle != nullptr;
}

bool webServerIsConnected() {
    return webServerIsRunning() && online_devices.wifi.wifiSoftApConnected;
}

void webServerSendString(const char* stringToSend) {
    if (settings.debugWebServer) {
        systemPrintf("WebServer: websocket output is not enabled in AP-only mode (%u bytes ignored)\r\n",
                     stringToSend ? strlen(stringToSend) : 0);
    }
}

void webServerSendSettings() {
}

void webServerSendFirmwareVersion() {
}

void webServerVerifyTables() {
}

#endif // COMPILE_WEBSERVER

#include "myWebServer.h"

#ifdef COMPILE_WEBSERVER

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <ctype.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "HAL.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "form.h"
#include "mcu_settings.h"
#include "myNetwork.h"
#include "myWIFI.h"

namespace HAL {
extern UnicoreUM980* gUm980;
}

//----------------------------------------
// Constants
//----------------------------------------

static const size_t webServerStackSize = 1024 * 20;
static const size_t firmwareBufferLength = 8 * 1024;
static const size_t profileUploadBufferLength = 1024;
static const size_t profileMaxFileSize = 8 * 1024;
static const size_t profileMaxNameLength = 31;
static const uint8_t espImageMagic = 0xE9;
static const uint32_t webServerStartRetryMs = 1000;
static const uint32_t webServerConfigSessionGraceMs = 5000;

static const char* const TAG = "WebServer";
static const char* const image_png = "image/png";
static const char* const text_css = "text/css";
static const char* const text_html = "text/html";
static const char* const text_javascript = "text/javascript";
static const char* const text_plain = "text/plain";

#define UPLOAD_FIRMWARE     "/uploadFirmware"
#define PROFILE_LIST        "/profile/list"
#define PROFILE_DOWNLOAD    "/profile/download"
#define PROFILE_UPLOAD      "/profile/upload"
#define PROFILE_ACTIVATE    "/profile/activate"
#define PROFILE_DELETE      "/profile/delete"
#define PROFILE_DIR         "/littlefs/profiles"
#define PROFILE_ACTIVE_FILE "/littlefs/profiles/active.txt"

//----------------------------------------
// Locals
//----------------------------------------

static httpd_handle_t webServerHandle;
static int webServerClientSocket = -1;
static WebServerState webServerState = WEBSERVER_STATE_OFF;
static uint32_t webServerLastStartAttemptMs = 0;
static uint32_t webServerLastStatusPushMs = 0;
static uint32_t webServerLastConfigActivityMs = 0;
static uint32_t webServerLastWsActivityMs = 0;

static const char* webServerStateNames[] = {
    "WEBSERVER_STATE_OFF",
    "WEBSERVER_STATE_WAIT_FOR_NETWORK",
    "WEBSERVER_STATE_NETWORK_CONNECTED",
    "WEBSERVER_STATE_RUNNING",
};

//----------------------------------------
// Forward routines
//----------------------------------------

static esp_err_t webServerHandlerFirmwareUpload(httpd_req_t* req);
static esp_err_t webServerHandlerCaptivePortalProbe(httpd_req_t* req);
static esp_err_t webServerHandlerGetPage(httpd_req_t* req);
static esp_err_t webServerHandlerHead(httpd_req_t* req);
static esp_err_t webServerHandlerMethodNotFound(httpd_req_t* req);
static esp_err_t webServerHandlerPageNotFound(httpd_req_t* req, httpd_err_code_t error);
static esp_err_t webServerHandlerProfileActivate(httpd_req_t* req);
static esp_err_t webServerHandlerProfileDelete(httpd_req_t* req);
static esp_err_t webServerHandlerProfileDownload(httpd_req_t* req);
static esp_err_t webServerHandlerProfileList(httpd_req_t* req);
static esp_err_t webServerHandlerProfileUpload(httpd_req_t* req);
static esp_err_t webServerHandlerWebSockets(httpd_req_t* req);
static bool webServerAppendField(char* buffer, size_t bufferLength, const char* key, const char* value);
static bool webServerApplyField(const char* key, const char* value);
static bool webServerBuildSettingsCsv(char* buffer, size_t bufferLength);
static void webServerSendProfileList();
static void webServerSendLiveStatus();
static void webServerHandleClientMessage(const char* message);
static void webServerClearClientSession();
static void webServerMarkConfigActivity();

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
    PAGE_HANDLER(6, PROFILE_LIST, HTTP_GET, text_plain, webServerHandlerProfileList),
    PAGE_HANDLER(7, PROFILE_DOWNLOAD, HTTP_GET, text_plain, webServerHandlerProfileDownload),
    PAGE_HANDLER(8, PROFILE_UPLOAD, HTTP_POST, text_plain, webServerHandlerProfileUpload),
    PAGE_HANDLER(9, PROFILE_ACTIVATE, HTTP_POST, text_plain, webServerHandlerProfileActivate),
    PAGE_HANDLER(10, PROFILE_DELETE, HTTP_POST, text_plain, webServerHandlerProfileDelete),
};

const int webServerTotalPages = sizeof(webServerPages) / sizeof(webServerPages[0]);

static const httpd_uri_t webServerWebSocketPage = {.uri = "/ws",
                                                   .method = HTTP_GET,
                                                   .handler = webServerHandlerWebSockets,
                                                   .user_ctx = NULL,
                                                   .is_websocket = true,
                                                   .handle_ws_control_frames = true,
                                                   .supported_subprotocol = NULL};

static const httpd_uri_t webServerHeadPage = {.uri = "/*",
                                              .method = HTTP_HEAD,
                                              .handler = webServerHandlerHead,
                                              .user_ctx = NULL,
                                              .is_websocket = false,
                                              .handle_ws_control_frames = false,
                                              .supported_subprotocol = NULL};

static const httpd_uri_t webServerPostNotFoundPage = {.uri = "/*",
                                                      .method = HTTP_POST,
                                                      .handler = webServerHandlerMethodNotFound,
                                                      .user_ctx = NULL,
                                                      .is_websocket = false,
                                                      .handle_ws_control_frames = false,
                                                      .supported_subprotocol = NULL};

static const httpd_uri_t webServerCaptivePortalProbePages[] = {
    {.uri = "/connecttest.txt",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/generate_204",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/gen_204",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/hotspot-detect.html",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/library/test/success.html",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/ncsi.txt",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
    {.uri = "/fwlink",
     .method = static_cast<httpd_method_t>(HTTP_ANY),
     .handler = webServerHandlerCaptivePortalProbe,
     .user_ctx = NULL,
     .is_websocket = false,
     .handle_ws_control_frames = false,
     .supported_subprotocol = NULL},
};

static const int webServerCaptivePortalProbePageCount =
    sizeof(webServerCaptivePortalProbePages) / sizeof(webServerCaptivePortalProbePages[0]);

//----------------------------------------
// Web settings field API
//----------------------------------------

typedef bool (*WebFieldGetter)(char* value, size_t valueLength);
typedef bool (*WebFieldSetter)(const char* value);

typedef struct WebFieldBinding {
    const char* section;
    const char* id;
    WebFieldGetter getter;
    WebFieldSetter setter;
} WebFieldBinding;

static bool
textToBool(const char* value) {
    return (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0) || (strcmp(value, "on") == 0);
}

static bool
getBool(char* value, size_t valueLength, bool setting) {
    snprintf(value, valueLength, "%s", setting ? "true" : "false");
    return true;
}

static bool
getInt(char* value, size_t valueLength, int setting) {
    snprintf(value, valueLength, "%d", setting);
    return true;
}

static bool
getDouble(char* value, size_t valueLength, double setting, int decimals) {
    snprintf(value, valueLength, "%.*f", decimals, setting);
    return true;
}

static bool
getString(char* value, size_t valueLength, const char* setting) {
    snprintf(value, valueLength, "%s", setting ? setting : "");
    return true;
}

static bool
getProfileName(char* value, size_t valueLength) {
    return getString(value, valueLength, settings.profileName);
}

static bool
setProfileName(const char* value) {
    snprintf(settings.profileName, sizeof(settings.profileName), "%s", value);
    return true;
}

static bool
getMeasurementRateHz(char* value, size_t valueLength) {
    if (settings.measurementRateMs == 0) {
        return getDouble(value, valueLength, 0.0, 3);
    }
    return getDouble(value, valueLength, static_cast<double>(MILLISECONDS_IN_A_SECOND) / settings.measurementRateMs, 3);
}

static bool
setMeasurementRateHz(const char* value) {
    const double hz = strtod(value, nullptr);
    if (hz <= 0.0) {
        return false;
    }
    settings.measurementRateMs = static_cast<uint16_t>(lround(static_cast<double>(MILLISECONDS_IN_A_SECOND) / hz));
    return true;
}

static bool
getMinCN0(char* value, size_t valueLength) {
    return getInt(value, valueLength, settings.minCN0);
}

static bool
setMinCN0(const char* value) {
    settings.minCN0 = static_cast<int16_t>(strtol(value, nullptr, 10));
    return true;
}

static bool
getBaseTypeSurveyIn(char* value, size_t valueLength) {
    return getBool(value, valueLength, !settings.fixedBase);
}

static bool
setBaseTypeSurveyIn(const char* value) {
    if (textToBool(value) || (strcmp(value, "0") == 0)) {
        settings.fixedBase = false;
    }
    return true;
}

static bool
getBaseTypeFixed(char* value, size_t valueLength) {
    return getBool(value, valueLength, settings.fixedBase);
}

static bool
setBaseTypeFixed(const char* value) {
    if (textToBool(value) || (strcmp(value, "1") == 0)) {
        settings.fixedBase = true;
    }
    return true;
}

static bool
getCoordinateTypeECEF(char* value, size_t valueLength) {
    return getBool(value, valueLength, settings.fixedBaseCoordinateType == COORD_TYPE_ECEF);
}

static bool
setCoordinateTypeECEF(const char* value) {
    if (textToBool(value) || (strcmp(value, "0") == 0)) {
        settings.fixedBaseCoordinateType = COORD_TYPE_ECEF;
    }
    return true;
}

static bool
getCoordinateTypeGeo(char* value, size_t valueLength) {
    return getBool(value, valueLength, settings.fixedBaseCoordinateType == COORD_TYPE_GEODETIC);
}

static bool
setCoordinateTypeGeo(const char* value) {
    if (textToBool(value) || (strcmp(value, "1") == 0)) {
        settings.fixedBaseCoordinateType = COORD_TYPE_GEODETIC;
    }
    return true;
}

static bool
getObservationSeconds(char* value, size_t valueLength) {
    return getInt(value, valueLength, settings.observationSeconds);
}

static bool
setObservationSeconds(const char* value) {
    settings.observationSeconds = static_cast<int>(strtol(value, nullptr, 10));
    return true;
}

static bool
getObservationAccuracy(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.observationPositionAccuracy, 2);
}

static bool
setObservationAccuracy(const char* value) {
    settings.observationPositionAccuracy = static_cast<float>(strtod(value, nullptr));
    return true;
}

static bool
getFixedEcefX(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedEcefX, 3);
}

static bool
setFixedEcefX(const char* value) {
    settings.fixedEcefX = strtod(value, nullptr);
    return true;
}

static bool
getFixedEcefY(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedEcefY, 3);
}

static bool
setFixedEcefY(const char* value) {
    settings.fixedEcefY = strtod(value, nullptr);
    return true;
}

static bool
getFixedEcefZ(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedEcefZ, 3);
}

static bool
setFixedEcefZ(const char* value) {
    settings.fixedEcefZ = strtod(value, nullptr);
    return true;
}

static bool
getFixedLat(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedLat, 8);
}

static bool
setFixedLat(const char* value) {
    settings.fixedLat = strtod(value, nullptr);
    return true;
}

static bool
getFixedLong(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedLong, 8);
}

static bool
setFixedLong(const char* value) {
    settings.fixedLong = strtod(value, nullptr);
    return true;
}

static bool
getFixedAltitude(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.fixedAltitude, 3);
}

static bool
setFixedAltitude(const char* value) {
    settings.fixedAltitude = strtod(value, nullptr);
    return true;
}

static bool
getFixedHAEAPC(char* value, size_t valueLength) {
    const double totalHeight =
        settings.fixedAltitude + ((settings.antennaHeight_mm + settings.antennaPhaseCenter_mm) / 1000.0);
    return getDouble(value, valueLength, totalHeight, 3);
}

static bool
setReadOnlyField(const char* value) {
    (void)value;
    return false;
}

static bool
getAntennaPhaseCenter(char* value, size_t valueLength) {
    return getDouble(value, valueLength, settings.antennaPhaseCenter_mm, 1);
}

static bool
setAntennaPhaseCenter(const char* value) {
    settings.antennaPhaseCenter_mm = static_cast<float>(strtod(value, nullptr));
    return true;
}

static bool
getAntennaHeightM(char* value, size_t valueLength) {
    return getDouble(value, valueLength, static_cast<double>(settings.antennaHeight_mm) / 1000.0, 3);
}

static bool
setAntennaHeightM(const char* value) {
    settings.antennaHeight_mm = static_cast<int16_t>(lround(strtod(value, nullptr) * 1000.0));
    return true;
}

static SystemState_t
webUiLastStateToSystemState(uint32_t value) {
    switch (value) {
        case 0: return STATE_ROVER_NOT_STARTED;
        case 1: return STATE_BASE_NOT_STARTED;
#ifdef COMPILE_NTP
        case 2: return STATE_NTPSERVER_NOT_STARTED;
#endif
        case 3: return STATE_BASE_CASTER_NOT_STARTED;
        default: return STATE_NOT_SET;
    }
}

static uint32_t
systemStateToWebUiLastState(SystemState_t value) {
    switch (value) {
        case STATE_ROVER_NOT_STARTED: return 0;
        case STATE_BASE_NOT_STARTED: return 1;
#ifdef COMPILE_NTP
        case STATE_NTPSERVER_NOT_STARTED: return 2;
#endif
        case STATE_BASE_CASTER_NOT_STARTED: return 3;
        default: return 0;
    }
}

static bool
getLastState(char* value, size_t valueLength) {
    return getInt(value, valueLength, systemStateToWebUiLastState(settings.lastState));
}

static bool
setLastState(const char* value) {
    settings.lastState = webUiLastStateToSystemState(strtoul(value, nullptr, 10));
    return true;
}

static const WebFieldBinding webFieldBindings[] = {
    {"Profile Configuration", "profileName", getProfileName, setProfileName},
    {"GNSS Configuration", "measurementRateHz", getMeasurementRateHz, setMeasurementRateHz},
    {"GNSS Configuration", "minCN0", getMinCN0, setMinCN0},
    {"Base Configuration", "baseTypeSurveyIn", getBaseTypeSurveyIn, setBaseTypeSurveyIn},
    {"Base Configuration", "baseTypeFixed", getBaseTypeFixed, setBaseTypeFixed},
    {"Base Configuration", "observationSeconds", getObservationSeconds, setObservationSeconds},
    {"Base Configuration", "observationPositionAccuracy", getObservationAccuracy, setObservationAccuracy},
    {"Base Configuration", "fixedBaseCoordinateTypeECEF", getCoordinateTypeECEF, setCoordinateTypeECEF},
    {"Base Configuration", "fixedBaseCoordinateTypeGeo", getCoordinateTypeGeo, setCoordinateTypeGeo},
    {"Base Configuration", "fixedEcefX", getFixedEcefX, setFixedEcefX},
    {"Base Configuration", "fixedEcefY", getFixedEcefY, setFixedEcefY},
    {"Base Configuration", "fixedEcefZ", getFixedEcefZ, setFixedEcefZ},
    {"Base Configuration", "fixedLatText", getFixedLat, setFixedLat},
    {"Base Configuration", "fixedLongText", getFixedLong, setFixedLong},
    {"Base Configuration", "fixedAltitude", getFixedAltitude, setFixedAltitude},
    {"Base Configuration", "fixedHAEAPC", getFixedHAEAPC, setReadOnlyField},
    {"Base Configuration", "antennaPhaseCenter", getAntennaPhaseCenter, setAntennaPhaseCenter},
    {"Base Configuration", "antennaHeightM", getAntennaHeightM, setAntennaHeightM},
    {"System Configuration", "lastState", getLastState, setLastState},
};

static const int webFieldBindingsCount = sizeof(webFieldBindings) / sizeof(webFieldBindings[0]);

static bool
webServerAppendField(char* buffer, size_t bufferLength, const char* key, const char* value) {
    const size_t used = strlen(buffer);
    if (used >= bufferLength) {
        return false;
    }

    const int written = snprintf(&buffer[used], bufferLength - used, "%s,%s,", key, value ? value : "");
    return (written > 0) && (static_cast<size_t>(written) < (bufferLength - used));
}

static const char*
webServerGetRtkFirmwareVersion() {
    const esp_app_desc_t* appDescription = esp_app_get_description();
    if ((appDescription == nullptr) || (appDescription->version[0] == 0)) {
        return "v0.0";
    }
    return appDescription->version;
}

static const char*
webServerGetGnssFirmwareVersion() {
    const UnicoreUM980* gnss = HAL::gUm980;
    const char* version = gnss ? gnss->getFirmwareVersion() : nullptr;
    return (version && version[0]) ? version : "v0.0";
}

static bool
webServerAppendFirmwareFields(char* buffer, size_t bufferLength) {
    bool ok = webServerAppendField(buffer, bufferLength, "rtkFirmwareVersion", webServerGetRtkFirmwareVersion());
    ok = webServerAppendField(buffer, bufferLength, "gnssFirmwareVersion", webServerGetGnssFirmwareVersion()) && ok;
    return ok;
}

static bool
webServerBuildSettingsCsv(char* buffer, size_t bufferLength) {
    buffer[0] = 0;

    char value[96];
    for (int index = 0; index < webFieldBindingsCount; index++) {
        value[0] = 0;
        if ((webFieldBindings[index].getter != nullptr) && webFieldBindings[index].getter(value, sizeof(value))) {
            if (!webServerAppendField(buffer, bufferLength, webFieldBindings[index].id, value)) {
                ESP_LOGW(TAG, "Settings CSV buffer full at field %s", webFieldBindings[index].id);
                return false;
            }
        }
    }

    const char* displayName = productPropertiesTable[productType].displayName[0]
                                  ? productPropertiesTable[productType].displayName
                                  : productPropertiesTable[productType].name;
    webServerAppendField(buffer, bufferLength, "hostMessage", "settings-synced");
    webServerAppendField(buffer, bufferLength, "productBrand", "SingularXYZ");
    webServerAppendField(buffer, bufferLength, "platformPrefix", displayName);
    if (!webServerAppendFirmwareFields(buffer, bufferLength)) {
        ESP_LOGW(TAG, "Settings CSV buffer full at firmware fields");
        return false;
    }
    return true;
}

//----------------------------------------
// Profile file API
//----------------------------------------

static bool
webServerUrlDecode(const char* source, char* destination, size_t destinationLength) {
    if ((source == nullptr) || (destination == nullptr) || (destinationLength == 0)) {
        return false;
    }

    size_t out = 0;
    for (size_t in = 0; source[in] != 0; in++) {
        if (out + 1 >= destinationLength) {
            return false;
        }

        if ((source[in] == '%') && isxdigit(static_cast<unsigned char>(source[in + 1]))
            && isxdigit(static_cast<unsigned char>(source[in + 2]))) {
            char hex[3] = {source[in + 1], source[in + 2], 0};
            destination[out++] = static_cast<char>(strtol(hex, nullptr, 16));
            in += 2;
        } else if (source[in] == '+') {
            destination[out++] = ' ';
        } else {
            destination[out++] = source[in];
        }
    }

    destination[out] = 0;
    return true;
}

static bool
webServerProfileNameIsSafe(const char* name) {
    if ((name == nullptr) || (name[0] == 0)) {
        return false;
    }

    const size_t length = strlen(name);
    if ((length > profileMaxNameLength) || (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        const char c = name[index];
        const bool valid = isalnum(static_cast<unsigned char>(c)) || (c == '-') || (c == '_') || (c == '.');
        if (!valid) {
            return false;
        }
    }

    return (strstr(name, "..") == nullptr) && (strchr(name, '/') == nullptr) && (strchr(name, '\\') == nullptr);
}

static bool
webServerGetProfileNameFromQuery(httpd_req_t* req, char* name, size_t nameLength) {
    char query[160] = {};
    char encodedName[96] = {};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, "name", encodedName, sizeof(encodedName)) != ESP_OK) {
        return false;
    }
    if (!webServerUrlDecode(encodedName, name, nameLength)) {
        return false;
    }
    return webServerProfileNameIsSafe(name);
}

static bool
webServerBuildProfilePath(const char* name, char* path, size_t pathLength) {
    if (!webServerProfileNameIsSafe(name)) {
        return false;
    }
    const int written = snprintf(path, pathLength, "%s/%s", PROFILE_DIR, name);
    return (written > 0) && (static_cast<size_t>(written) < pathLength);
}

static bool
webServerEnsureProfileDir() {
    if (!online_devices.littlefs) {
        return false;
    }
    if (LittleFS.exists(PROFILE_DIR)) {
        return true;
    }
    return LittleFS.mkdir(PROFILE_DIR);
}

static bool
webServerReadActiveProfile(char* name, size_t nameLength) {
    name[0] = 0;
    if (!online_devices.littlefs || !LittleFS.exists(PROFILE_ACTIVE_FILE)) {
        return false;
    }

    File file = LittleFS.open(PROFILE_ACTIVE_FILE, "r");
    if (!file) {
        return false;
    }

    const size_t bytes = file.readBytes(name, nameLength - 1);
    name[bytes] = 0;
    file.close();

    char* newline = strpbrk(name, "\r\n");
    if (newline != nullptr) {
        *newline = 0;
    }
    return webServerProfileNameIsSafe(name);
}

static bool
webServerWriteActiveProfile(const char* name) {
    if (!webServerEnsureProfileDir()) {
        return false;
    }

    char path[96] = {};
    if (!webServerBuildProfilePath(name, path, sizeof(path)) || !LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(PROFILE_ACTIVE_FILE, "w");
    if (!file) {
        return false;
    }
    const size_t written = file.print(name);
    file.close();
    return written == strlen(name);
}

static const char*
webServerBaseName(const char* path) {
    const char* slash = strrchr(path, '/');
    return (slash == nullptr) ? path : slash + 1;
}

static bool
webServerAppendJsonEscaped(char* buffer, size_t bufferLength, const char* value) {
    size_t used = strlen(buffer);
    if (used >= bufferLength) {
        return false;
    }

    for (size_t index = 0; (value != nullptr) && (value[index] != 0); index++) {
        if (used + 3 >= bufferLength) {
            return false;
        }
        if ((value[index] == '"') || (value[index] == '\\')) {
            buffer[used++] = '\\';
        }
        buffer[used++] = value[index];
        buffer[used] = 0;
    }
    return true;
}

static bool
webServerBuildProfileListJson(char* buffer, size_t bufferLength) {
    char activeProfile[profileMaxNameLength + 1] = {};
    int count = 0;

    buffer[0] = 0;
    snprintf(buffer, bufferLength, "{\"status\":\"%s\",\"current\":\"",
             online_devices.littlefs ? "ok" : "littlefs-offline");
    if (!webServerAppendJsonEscaped(buffer, bufferLength, settings.profileName)) {
        return false;
    }
    strlcat(buffer, "\",\"active\":\"", bufferLength);

    if (online_devices.littlefs && webServerEnsureProfileDir()
        && webServerReadActiveProfile(activeProfile, sizeof(activeProfile))) {
        if (!webServerAppendJsonEscaped(buffer, bufferLength, activeProfile)) {
            return false;
        }
    }
    strlcat(buffer, "\",\"files\":[", bufferLength);

    if (!online_devices.littlefs || !webServerEnsureProfileDir()) {
        strlcat(buffer, "]}", bufferLength);
        return true;
    }

    File root = LittleFS.open(PROFILE_DIR, "r");
    if (!root || !root.isDirectory()) {
        strlcat(buffer, "]}", bufferLength);
        return true;
    }

    File file = root.openNextFile();
    while (file && (count < 12)) {
        if (!file.isDirectory()) {
            const char* name = webServerBaseName(file.name());
            if (webServerProfileNameIsSafe(name)) {
                char entry[64] = {};
                snprintf(entry, sizeof(entry), "%s{\"name\":\"", (count == 0) ? "" : ",");
                strlcat(buffer, entry, bufferLength);
                if (!webServerAppendJsonEscaped(buffer, bufferLength, name)) {
                    return false;
                }
                snprintf(entry, sizeof(entry), "\",\"size\":%u}", static_cast<unsigned>(file.size()));
                strlcat(buffer, entry, bufferLength);
                count++;
            }
        }
        file = root.openNextFile();
    }

    strlcat(buffer, "]}", bufferLength);
    return true;
}

static void
webServerSendProfileList() {
    char packet[1400] = {};
    char activeProfile[profileMaxNameLength + 1] = {};
    char value[96] = {};
    int count = 0;

    webServerAppendField(packet, sizeof(packet), "profileListStatus",
                         online_devices.littlefs ? "ok" : "littlefs-offline");
    webServerAppendField(packet, sizeof(packet), "profileCurrent", settings.profileName);

    if (!online_devices.littlefs || !webServerEnsureProfileDir()) {
        webServerAppendField(packet, sizeof(packet), "profileFileCount", "0");
        webServerSendString(packet);
        return;
    }

    if (webServerReadActiveProfile(activeProfile, sizeof(activeProfile))) {
        webServerAppendField(packet, sizeof(packet), "profileActiveFile", activeProfile);
    } else {
        webServerAppendField(packet, sizeof(packet), "profileActiveFile", "");
    }

    File root = LittleFS.open(PROFILE_DIR, "r");
    if (!root || !root.isDirectory()) {
        webServerAppendField(packet, sizeof(packet), "profileFileCount", "0");
        webServerSendString(packet);
        return;
    }

    File file = root.openNextFile();
    while (file && (count < 12)) {
        if (!file.isDirectory()) {
            const char* name = webServerBaseName(file.name());
            if (webServerProfileNameIsSafe(name)) {
                char key[32] = {};
                snprintf(key, sizeof(key), "profileFile%dName", count);
                webServerAppendField(packet, sizeof(packet), key, name);
                snprintf(key, sizeof(key), "profileFile%dSize", count);
                snprintf(value, sizeof(value), "%u", static_cast<unsigned>(file.size()));
                webServerAppendField(packet, sizeof(packet), key, value);
                count++;
            }
        }
        file = root.openNextFile();
    }

    snprintf(value, sizeof(value), "%d", count);
    webServerAppendField(packet, sizeof(packet), "profileFileCount", value);
    webServerSendString(packet);
}

static const char*
webServerFixText(const UnicoreUM980* gnss) {
    if (gnss == nullptr) {
        return "GNSS offline";
    }
    if (gnss->isRTKFix()) {
        return "RTK Fix";
    }
    if (gnss->isRTKFloat()) {
        return "RTK Float";
    }
    if (gnss->isDgpsFixed()) {
        return "DGPS";
    }
    if (gnss->isFixed()) {
        return "Fixed";
    }
    return "No fix";
}

static void
webServerFormatPosition(char* buffer, size_t bufferLength) {
    const UnicoreUM980* gnss = HAL::gUm980;
    if (gnss == nullptr) {
        snprintf(buffer, bufferLength, "GNSS offline");
        return;
    }

    const char* fixText = webServerFixText(gnss);
    if (!gnss->isFixed()) {
        snprintf(buffer, bufferLength, "%s | waiting for valid position", fixText);
        return;
    }

    snprintf(buffer, bufferLength, "%s | Lat %.8f Lon %.8f Alt %.3f m", fixText, gnss->getLatitude(),
             gnss->getLongitude(), gnss->getAltitude());
}

static void
webServerFormatUtcTime(char* buffer, size_t bufferLength) {
    const UnicoreUM980* gnss = HAL::gUm980;
    if (gnss == nullptr) {
        snprintf(buffer, bufferLength, "GNSS offline");
        return;
    }

    if (!gnss->isValidTime()) {
        snprintf(buffer, bufferLength, "Waiting for GNSS");
        return;
    }

    if (gnss->isValidDate()) {
        snprintf(buffer, bufferLength, "%04u-%02u-%02u %02u:%02u:%02u.%03u", gnss->getYear(), gnss->getMonth(),
                 gnss->getDay(), gnss->getHour(), gnss->getMinute(), gnss->getSecond(), gnss->getMillisecond());
    } else {
        snprintf(buffer, bufferLength, "%02u:%02u:%02u.%03u", gnss->getHour(), gnss->getMinute(), gnss->getSecond(),
                 gnss->getMillisecond());
    }
}

static void
webServerSendLiveStatus() {
    if ((webServerHandle == nullptr) || (webServerClientSocket < 0)) {
        return;
    }

    const uint32_t now = millis();
    if ((webServerLastStatusPushMs != 0U) && ((now - webServerLastStatusPushMs) < 5000U)) {
        return;
    }
    webServerLastStatusPushMs = now;

    const UnicoreUM980* gnss = HAL::gUm980;
    char position[64] = {};
    char utcTime[32] = {};
    webServerFormatPosition(position, sizeof(position));
    webServerFormatUtcTime(utcTime, sizeof(utcTime));

    char packet[192] = {};
    char satellitesInViewText[16] = {};
    char satellitesUsedText[16] = {};
    snprintf(satellitesInViewText, sizeof(satellitesInViewText), "%u", gnss ? gnss->getSatellitesInView() : 0U);
    snprintf(satellitesUsedText, sizeof(satellitesUsedText), "%u", gnss ? gnss->getSatellitesUsed() : 0U);

    webServerAppendField(packet, sizeof(packet), "utcTime", utcTime);
    webServerAppendField(packet, sizeof(packet), "satellitesInView", satellitesInViewText);
    webServerAppendField(packet, sizeof(packet), "satellitesUsed", satellitesUsedText);
    webServerAppendField(packet, sizeof(packet), "rtkPosition", position);

    webServerSendString(packet);
}

static bool
webServerApplyAction(const char* key, const char* value) {
    if (strcmp(key, "clientReady") == 0) {
        webServerSendSettings();
        webServerSendFirmwareVersion();
        webServerLastStatusPushMs = 0;
        webServerSendLiveStatus();
        return true;
    }

    if (strcmp(key, "refreshProfiles") == 0) {
        webServerSendField("profileActionStatus", "use-http-list");
        return true;
    }

    if (strcmp(key, "resetProfile") == 0) {
        (void)value;
        webServerSendSettings();
        webServerSendProfileList();
        return true;
    }

    if (strcmp(key, "deleteProfile") == 0) {
        (void)value;
        webServerSendField("profileActionStatus", "use-http-delete");
        return true;
    }

    if (strcmp(key, "activateProfile") == 0) {
        (void)value;
        webServerSendField("profileActionStatus", "use-http-activate");
        return true;
    }

    if (strcmp(key, "uploadProfile") == 0) {
        ESP_LOGI(TAG, "Deprecated WS uploadProfile ignored: %s", value);
        webServerSendField("profileActionStatus", "use-http-upload");
        return true;
    }

    if (strcmp(key, "profileUploadName") == 0) {
        ESP_LOGI(TAG, "Deprecated WS profileUploadName ignored: %s", value);
        return true;
    }

    if (strcmp(key, "profileUploadData") == 0) {
        ESP_LOGI(TAG, "Deprecated WS profileUploadData ignored, length=%u", static_cast<unsigned>(strlen(value)));
        return true;
    }

    if (strcmp(key, "factoryDefaultReset") == 0) {
        ESP_LOGI(TAG, "TODO action factoryDefaultReset = %s", value);
        return true;
    }

    if (strcmp(key, "exitAndReset") == 0) {
        ESP_LOGI(TAG, "TODO action exitAndReset = %s", value);
        return true;
    }

    if (strcmp(key, "profileNumber") == 0) {
        ESP_LOGI(TAG, "Profile radio selection = %s", value);
        return true;
    }

    if (strcmp(key, "enableFactoryDefaults") == 0) {
        return true;
    }

    if (strncmp(key, "constellation_", strlen("constellation_")) == 0) {
        ESP_LOGI(TAG, "TODO constellation field %s = %s", key, value);
        return true;
    }

    return false;
}

static bool
webServerApplyField(const char* key, const char* value) {
    for (int index = 0; index < webFieldBindingsCount; index++) {
        if (strcmp(key, webFieldBindings[index].id) == 0) {
            if ((webFieldBindings[index].setter != nullptr) && webFieldBindings[index].setter(value)) {
                ESP_LOGI(TAG, "%s updated: %s = %s", webFieldBindings[index].section, key, value);
                return true;
            }

            ESP_LOGW(TAG, "Rejected field update: %s = %s", key, value);
            return false;
        }
    }

    if (webServerApplyAction(key, value)) {
        return true;
    }

    ESP_LOGW(TAG, "Unhandled web field: %s = %s", key, value);
    return false;
}

//----------------------------------------
// Multipart helpers
//----------------------------------------

static int
recvByte(httpd_req_t* req, char* value, size_t* received) {
    const int bytes = httpd_req_recv(req, value, 1);
    if (bytes == 1) {
        (*received)++;
    }
    return bytes;
}

static bool
extractMultipartBoundary(httpd_req_t* req, char* boundary, size_t boundaryLength) {
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

static bool
readMultipartHeader(httpd_req_t* req, char* header, size_t headerLength, size_t* received) {
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

static int
findBytes(const uint8_t* data, size_t dataLength, const char* needle, size_t needleLength) {
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

static bool
webServerFirmwareFileNameIsSafeBin(const char* fileName, const char* fileNameEnd) {
    if ((fileName == nullptr) || (fileNameEnd == nullptr) || (fileNameEnd <= fileName)) {
        return false;
    }

    const size_t fileNameLength = fileNameEnd - fileName;
    if ((fileNameLength < 5) || (fileNameLength > 63)) {
        return false;
    }

    for (const char* cursor = fileName; cursor < fileNameEnd; cursor++) {
        const unsigned char value = static_cast<unsigned char>(*cursor);
        if ((value <= 0x20) || (*cursor == '/') || (*cursor == '\\') || (*cursor == ':')) {
            return false;
        }
    }

    const char* extension = fileNameEnd - 4;
    return (extension[0] == '.') && (tolower(static_cast<unsigned char>(extension[1])) == 'b')
           && (tolower(static_cast<unsigned char>(extension[2])) == 'i')
           && (tolower(static_cast<unsigned char>(extension[3])) == 'n');
}

//----------------------------------------
// Handlers
//----------------------------------------

static bool
dataIsGzipCompressed(const uint8_t* data, size_t length) {
    return (length >= 2) && (data[0] == 0x1F) && (data[1] == 0x8B);
}

static esp_err_t
webServerHandlerGetPage(httpd_req_t* req) {
    webServerMarkConfigActivity();

    const uintptr_t index = reinterpret_cast<uintptr_t>(req->user_ctx);
    if (index >= static_cast<uintptr_t>(webServerTotalPages)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid page index");
    }

    const GET_PAGE_HANDLER* page = &webServerPages[index];
    if (settings.debugWebServer) {
        ESP_LOGI(TAG, "GET %s (%u bytes)", req->uri, static_cast<unsigned>(page->_length));
    }

    httpd_resp_set_type(req, *page->_type);
    if (dataIsGzipCompressed(reinterpret_cast<const uint8_t*>(page->_data), page->_length)) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    return httpd_resp_send(req, reinterpret_cast<const char*>(page->_data), page->_length);
}

static esp_err_t
webServerHandlerCaptivePortalProbe(httpd_req_t* req) {
    httpd_resp_set_type(req, text_plain);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_status(req, "404 Not Found");

    if (req->method == HTTP_HEAD) {
        return httpd_resp_send(req, nullptr, 0);
    }

    return httpd_resp_sendstr(req, "Not found");
}

static esp_err_t
webServerHandlerHead(httpd_req_t* req) {
    webServerMarkConfigActivity();

    httpd_resp_set_type(req, text_html);
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t
webServerHandlerMethodNotFound(httpd_req_t* req) {
    webServerMarkConfigActivity();
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

static esp_err_t
webServerHandlerProfileList(httpd_req_t* req) {
    char response[1400] = {};
    if (!webServerBuildProfileListJson(response, sizeof(response))) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build profile list");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t
webServerHandlerProfileActivate(httpd_req_t* req) {
    char name[profileMaxNameLength + 1] = {};
    if (!webServerGetProfileNameFromQuery(req, name, sizeof(name))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile name");
    }
    if (!webServerWriteActiveProfile(name)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to activate profile");
    }

    httpd_resp_set_type(req, text_plain);
    return httpd_resp_sendstr(req, "Profile activated");
}

static esp_err_t
webServerHandlerProfileDelete(httpd_req_t* req) {
    char name[profileMaxNameLength + 1] = {};
    char path[96] = {};
    char activeProfile[profileMaxNameLength + 1] = {};

    if (!webServerGetProfileNameFromQuery(req, name, sizeof(name))
        || !webServerBuildProfilePath(name, path, sizeof(path))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile name");
    }
    if (!online_devices.littlefs || !LittleFS.exists(path)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
    }
    if (!LittleFS.remove(path)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete profile");
    }
    if (webServerReadActiveProfile(activeProfile, sizeof(activeProfile)) && (strcmp(activeProfile, name) == 0)) {
        LittleFS.remove(PROFILE_ACTIVE_FILE);
    }

    httpd_resp_set_type(req, text_plain);
    return httpd_resp_sendstr(req, "Profile deleted");
}

static esp_err_t
webServerHandlerProfileDownload(httpd_req_t* req) {
    char name[profileMaxNameLength + 1] = {};
    char path[96] = {};
    char buffer[profileUploadBufferLength] = {};

    if (!online_devices.littlefs) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "LittleFS is not mounted");
    }
    if (!webServerGetProfileNameFromQuery(req, name, sizeof(name))
        || !webServerBuildProfilePath(name, path, sizeof(path))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile name");
    }

    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Profile not found");
    }

    char disposition[128] = {};
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name);
    httpd_resp_set_type(req, text_plain);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    while (file.available()) {
        const size_t bytes = file.readBytes(buffer, sizeof(buffer));
        if (httpd_resp_send_chunk(req, buffer, bytes) != ESP_OK) {
            file.close();
            return ESP_FAIL;
        }
    }

    file.close();
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t
webServerHandlerProfileUpload(httpd_req_t* req) {
    char name[profileMaxNameLength + 1] = {};
    char path[96] = {};
    uint8_t* buffer = nullptr;
    size_t received = 0;
    const char* errorMessage = nullptr;

    do {
        if (!online_devices.littlefs || !webServerEnsureProfileDir()) {
            errorMessage = "LittleFS is not mounted";
            break;
        }
        if (!webServerGetProfileNameFromQuery(req, name, sizeof(name))
            || !webServerBuildProfilePath(name, path, sizeof(path))) {
            errorMessage = "Invalid profile name";
            break;
        }
        if ((req->content_len == 0) || (req->content_len > profileMaxFileSize)) {
            errorMessage = "Profile file is empty or too large";
            break;
        }

        buffer = static_cast<uint8_t*>(rtkMalloc(profileUploadBufferLength, "Profile upload buffer"));
        if (buffer == nullptr) {
            errorMessage = "Failed to allocate upload buffer";
            break;
        }

        File file = LittleFS.open(path, "w");
        if (!file) {
            errorMessage = "Failed to open profile for writing";
            break;
        }

        while (received < req->content_len) {
            const size_t remaining = req->content_len - received;
            const size_t requestBytes = (remaining > profileUploadBufferLength) ? profileUploadBufferLength : remaining;
            const int bytesRead = httpd_req_recv(req, reinterpret_cast<char*>(buffer), requestBytes);
            if (bytesRead <= 0) {
                errorMessage = "Failed to receive profile data";
                break;
            }

            received += bytesRead;
            const size_t written = file.write(buffer, bytesRead);
            if (written != static_cast<size_t>(bytesRead)) {
                errorMessage = "Failed to write profile data";
                break;
            }
        }

        file.close();
        if (errorMessage != nullptr) {
            LittleFS.remove(path);
            break;
        }

        httpd_resp_set_type(req, text_plain);
        httpd_resp_sendstr(req, "Profile uploaded");
        webServerSendField("profileActionStatus", "uploaded");
        webServerSendProfileList();
        rtkFree(buffer, "Profile upload buffer");
        return ESP_OK;
    } while (0);

    if (buffer != nullptr) {
        rtkFree(buffer, "Profile upload buffer");
    }

    ESP_LOGE(TAG, "Profile upload failed: %s", errorMessage ? errorMessage : "unknown error");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errorMessage ? errorMessage : "Profile upload failed");
    return ESP_FAIL;
}

static esp_err_t
webServerHandlerFirmwareUpload(httpd_req_t* req) {
    char boundary[96] = {};
    char header[768] = {};
    uint8_t* buffer = nullptr;
    const char* errorMessage = nullptr;
    bool updateRunning = false;
    size_t received = 0;
    size_t firmwareBytes = 0;

    if (settings.debugWebServer) {
        ESP_LOGI(TAG, "POST %s, content length %u", req->uri, static_cast<unsigned>(req->content_len));
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
        if (!webServerFirmwareFileNameIsSafeBin(fileName, fileNameEnd)) {
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
            if ((firmwareBytes == 0) && (buffered > 0) && (buffer[0] != espImageMagic)) {
                errorMessage = "Firmware image header is invalid";
                break;
            }

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
            const size_t requestBytes =
                (remainingRequest > firmwareBufferLength) ? firmwareBufferLength : remainingRequest;
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

        ESP_LOGI(TAG, "Firmware update complete: %u bytes. Restarting", static_cast<unsigned>(firmwareBytes));
        httpd_resp_set_type(req, text_plain);
        httpd_resp_sendstr(req, "Firmware uploaded successfully. Restarting.");
        delay(1500);
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

    ESP_LOGE(TAG, "Firmware upload failed: %s", errorMessage ? errorMessage : "unknown error");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, errorMessage ? errorMessage : "Firmware upload failed");
    return ESP_FAIL;
}

static esp_err_t
webServerHandlerWebSockets(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        webServerMarkConfigActivity();
        webServerClientSocket = httpd_req_to_sockfd(req);
        webServerLastWsActivityMs = millis();
        ESP_LOGW(TAG, "WS connected, socket=%d", webServerClientSocket);
        return ESP_OK;
    }

    webServerMarkConfigActivity();
    webServerLastWsActivityMs = millis();

    httpd_ws_frame_t packet = {};
    const esp_err_t status = httpd_ws_recv_frame(req, &packet, 0);
    if (status != ESP_OK) {
        return status;
    }

    if (packet.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGW(TAG, "WS closed, socket=%d", webServerClientSocket);
        webServerClientSocket = -1;
        return ESP_OK;
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
        const esp_err_t sendStatus = httpd_ws_send_frame(req, &response);
        if (sendStatus != ESP_OK) {
            ESP_LOGW(TAG, "WS ACK send failed: %s", esp_err_to_name(sendStatus));
        }
    }
    rtkFree(payload, "WebSocket payload");
    return readStatus;
}

static void
webServerHandleClientMessage(const char* message) {
    ESP_LOGI(TAG, "WS RX: %s", message);

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
        const size_t keyLength = (static_cast<size_t>(comma - cursor) >= sizeof(key))
                                     ? sizeof(key) - 1
                                     : static_cast<size_t>(comma - cursor);
        const size_t valueLength = (static_cast<size_t>(next - value) >= sizeof(settingValue))
                                       ? sizeof(settingValue) - 1
                                       : static_cast<size_t>(next - value);
        memcpy(key, cursor, keyLength);
        memcpy(settingValue, value, valueLength);

        ESP_LOGI(TAG, "action: %s = %s", key, settingValue);
        webServerApplyField(key, settingValue);
        cursor = next + 1;
    }
}

static esp_err_t
webServerHandlerPageNotFound(httpd_req_t* req, httpd_err_code_t error) {
    (void)error;
    webServerMarkConfigActivity();
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

//----------------------------------------
// Registration
//----------------------------------------

static bool
webServerRegisterErrorHandler(httpd_err_code_t error, httpd_err_handler_func_t handler) {
    const esp_err_t status = httpd_register_err_handler(webServerHandle, error, handler);
    if (settings.debugWebServer) {
        ESP_LOGI(TAG, "%s %d error handler", (status == ESP_OK) ? "registered" : "failed to register", error);
    }
    return status == ESP_OK;
}

static bool
webServerRegisterPageHandler(const httpd_uri_t* page) {
    const esp_err_t status = httpd_register_uri_handler(webServerHandle, page);
    if (settings.debugWebServer) {
        ESP_LOGI(TAG, "%s %s handler", (status == ESP_OK) ? "registered" : "failed to register", page->uri);
    }
    return status == ESP_OK;
}

static bool
webServerAssignResources() {
    const uint32_t now = millis();
    if ((webServerLastStartAttemptMs != 0U) && ((now - webServerLastStartAttemptMs) < webServerStartRetryMs)) {
        return false;
    }
    webServerLastStartAttemptMs = now;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = settings.httpPort ? settings.httpPort : 80;
    config.stack_size = webServerStackSize;
    config.max_uri_handlers = webServerTotalPages + webServerCaptivePortalProbePageCount + 4;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (settings.debugWebServer) {
        webServerHttpdDisplayConfig(&config);
        ESP_LOGI(TAG, "Starting on port: %d", config.server_port);
    }

    esp_err_t status = httpd_start(&webServerHandle, &config);
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start: %s", esp_err_to_name(status));
        return false;
    }

    bool success = webServerRegisterErrorHandler(HTTPD_404_NOT_FOUND, webServerHandlerPageNotFound);
    for (int index = 0; success && (index < webServerCaptivePortalProbePageCount); index++) {
        success = webServerRegisterPageHandler(&webServerCaptivePortalProbePages[index]);
    }
    success = success && webServerRegisterPageHandler(&webServerHeadPage)
              && webServerRegisterPageHandler(&webServerWebSocketPage);
    for (int index = 0; success && (index < webServerTotalPages); index++) {
        success = webServerRegisterPageHandler(&webServerPages[index]._page);
    }
    success = success && webServerRegisterPageHandler(&webServerPostNotFoundPage);

    if (!success) {
        webServerStop();
        return false;
    }

    webServerState = WEBSERVER_STATE_RUNNING;
    webServerLastStartAttemptMs = 0;
    ESP_LOGI(TAG, "Started");
    return true;
}

//----------------------------------------
// Get the webconfig state name
//----------------------------------------
const char*
webServerGetStateName(uint8_t state, char* string) {
    if (state < WEBSERVER_STATE_MAX) {
        return webServerStateNames[state];
    }
    sprintf(string, "Web Server: Unknown state (%d)", state);
    return string;
}

//----------------------------------------
// Set the next webconfig state
//----------------------------------------
void
webServerSetState(WebServerState newState) {
    char string1[40];
    char string2[40];
    const char* arrow = nullptr;
    const char* asterisk = nullptr;
    const char* initialState = nullptr;
    const char* endingState = nullptr;

    // Display the state transition
    if (settings.debugWebServer) {
        arrow = "";
        asterisk = "";
        initialState = "";
        if (newState == webServerState) {
            asterisk = "*";
        } else {
            initialState = webServerGetStateName(webServerState, string1);
            arrow = " --> ";
        }
    }

    // Set the new state
    webServerState = newState;
    if (settings.debugWebServer) {
        // Display the new firmware update state
        endingState = webServerGetStateName(newState, string2);
        if (!online_devices.rtc) {
            systemPrintf("%s%s%s%s\n", asterisk, initialState, arrow, endingState);
        } else {
            // Timestamp the state change
            systemPrintf("%s%s%s%s, %s\n", asterisk, initialState, arrow, endingState, getTimeStamp());
        }
    }

    // Validate the state
    if (newState >= WEBSERVER_STATE_MAX) {
        reportFatalError("Web Server: Invalid web config state");
    }
}

//----------------------------------------
// State machine
//----------------------------------------
void
webServerStart() {
    // Display the heap state
    reportHeapNow(settings.debugWebServer);

    if (webServerState != WEBSERVER_STATE_OFF) {
        if (settings.debugWebServer) {
            systemPrintln("Web Server: Already running!");
        }
    } else {
        if (settings.debugWebServer) {
            systemPrintln("Web Server: Starting");
        }
        do {
            // Start the network
            if (networkInterfaceHasInternet(NETWORK_ETHERNET)) {
                networkConsumerAdd(NETCONSUMER_WEB_CONFIG, NETWORK_ANY, __FILE__, __LINE__);
                break;
            }

            if ((settings.wifiConfigOverAP == false) || networkInterfaceHasInternet(NETWORK_WIFI_STATION)) {
                networkConsumerAdd(NETCONSUMER_WEB_CONFIG, NETWORK_ANY, __FILE__, __LINE__);
            }

            if (settings.wifiConfigOverAP) {
                networkSoftApConsumerAdd(NETCONSUMER_WEB_CONFIG, __FILE__, __LINE__);
            }
        } while (0);
        webServerSetState(WEBSERVER_STATE_WAIT_FOR_NETWORK);
    }
}

void
webServerStop() {
    if (webServerHandle != nullptr) {
        httpd_stop(webServerHandle);
        webServerHandle = nullptr;
    }
    webServerClearClientSession();
    networkUserRemove(NETCONSUMER_WEB_CONFIG, __FILE__, __LINE__);
    networkConsumerRemove(NETCONSUMER_WEB_CONFIG, NETWORK_ANY, __FILE__, __LINE__);
    networkSoftApConsumerRemove(NETCONSUMER_WEB_CONFIG, __FILE__, __LINE__);
    webServerLastStartAttemptMs = 0;
    webServerState = WEBSERVER_STATE_OFF;
    ESP_LOGI(TAG, "Stopped");
}

//----------------------------------------
// State machine to handle the starting/stopping of the web server
//----------------------------------------
void
webServerUpdate() {
    bool connected;

    // Determine if the network is connected
    connected = networkConsumerIsConnected(NETCONSUMER_WEB_CONFIG);

    // Walk the state machine
    switch (webServerState) {
        default:
            systemPrintf("ERROR: Unknown Web Server state (%d)\n", webServerState);

            // Stop the machine
            webServerStop();
            break;

        case WEBSERVER_STATE_OFF:
            // Wait until webServerStart() is called
            break;

        // Wait for connection to the network
        case WEBSERVER_STATE_WAIT_FOR_NETWORK:
            // Wait until the network is connected to the internet or has WiFi AP
            if (connected || wifiSoftApRunning()) {
                if (settings.debugWebServer) {
                    systemPrintln("Web Server connected to network");
                }

                networkUserAdd(NETCONSUMER_WEB_CONFIG, __FILE__, __LINE__);
                webServerSetState(WEBSERVER_STATE_NETWORK_CONNECTED);
            }
            break;

        // Start the web server
        case WEBSERVER_STATE_NETWORK_CONNECTED: {
            // Determine if the network has failed
            if (connected == false && wifiSoftApRunning() == false) {
                networkUserRemove(NETCONSUMER_WEB_CONFIG, __FILE__, __LINE__);
                webServerSetState(WEBSERVER_STATE_WAIT_FOR_NETWORK);
            }

            // Attempt to start the web server
            else if (webServerAssignResources() == true) {
                webServerSetState(WEBSERVER_STATE_RUNNING);
            }
        } break;

        // Allow web services
        case WEBSERVER_STATE_RUNNING:
            // Determine if the network has failed
            if (connected == false && wifiSoftApRunning() == false) {
                if (webServerHandle != nullptr) {
                    httpd_stop(webServerHandle);
                    webServerHandle = nullptr;
                }
                webServerClearClientSession();
                webServerSetState(WEBSERVER_STATE_WAIT_FOR_NETWORK);
            }
            webServerSendLiveStatus();
            // This state is exited when webServerStop() is called

            break;
    }

    // Display an alive message
    // if (PERIODIC_DISPLAY(PD_WEB_SERVER_STATE)) {
    //     systemPrintf("Web Server state: %s\r\n", webServerStateNames[webServerState]);
    //     PERIODIC_CLEAR(PD_WEB_SERVER_STATE);
    // }
}

bool
webServerIsRunning() {
    return webServerState == WEBSERVER_STATE_RUNNING;
}

bool
webServerIsConnected() {
    return webServerIsRunning() && (wifiSoftApClientCount() > 0);
}

bool
webServerHasActiveConfigSession() {
    if (!webServerIsRunning()) {
        return false;
    }

    if (wifiSoftApClientCount() == 0) {
        webServerClearClientSession();
        return false;
    }

    if (webServerClientSocket >= 0) {
        return true;
    }

    if (webServerLastWsActivityMs == 0U) {
        if (webServerLastConfigActivityMs == 0U) {
            return false;
        }
        return (millis() - webServerLastConfigActivityMs) < webServerConfigSessionGraceMs;
    }

    if ((millis() - webServerLastWsActivityMs) < webServerConfigSessionGraceMs) {
        return true;
    }

    return (webServerLastConfigActivityMs != 0U)
           && ((millis() - webServerLastConfigActivityMs) < webServerConfigSessionGraceMs);
}

static void
webServerClearClientSession() {
    webServerClientSocket = -1;
    webServerLastConfigActivityMs = 0;
    webServerLastWsActivityMs = 0;
}

static void
webServerMarkConfigActivity() {
    webServerLastConfigActivityMs = millis();
}

void
webServerSendString(const char* stringToSend) {
    if ((stringToSend == nullptr) || (webServerHandle == nullptr) || (webServerClientSocket < 0)) {
        return;
    }

    httpd_ws_frame_t packet = {};
    packet.type = HTTPD_WS_TYPE_TEXT;
    packet.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(stringToSend));
    packet.len = strlen(stringToSend);

    const esp_err_t status = httpd_ws_send_frame_async(webServerHandle, webServerClientSocket, &packet);
    if (status != ESP_OK) {
        ESP_LOGW(TAG, "WS TX failed on socket %d: %s", webServerClientSocket, esp_err_to_name(status));
        webServerClientSocket = -1;
    } else if (settings.debugWebServer) {
        webServerLastWsActivityMs = millis();
        //ESP_LOGI(TAG, "WS TX: %s", stringToSend);
    } else {
        webServerLastWsActivityMs = millis();
    }
}

void
webServerSendField(const char* fieldId, const char* value) {
    if ((fieldId == nullptr) || (value == nullptr)) {
        return;
    }

    char message[160] = {};
    if (webServerAppendField(message, sizeof(message), fieldId, value)) {
        webServerSendString(message);
    }
}

void
webServerSendFieldInt(const char* fieldId, int value) {
    char text[24];
    snprintf(text, sizeof(text), "%d", value);
    webServerSendField(fieldId, text);
}

void
webServerSendFieldDouble(const char* fieldId, double value, int decimals) {
    char text[40];
    snprintf(text, sizeof(text), "%.*f", decimals, value);
    webServerSendField(fieldId, text);
}

void
webServerSendStatus() {
    webServerSendLiveStatus();
}

void
webServerSendSettings() {
    char settingsCsv[2048];
    if (webServerBuildSettingsCsv(settingsCsv, sizeof(settingsCsv))) {
        webServerSendString(settingsCsv);
    }
}

void
webServerSendFirmwareVersion() {
    char message[128] = {};
    if (webServerAppendFirmwareFields(message, sizeof(message))) {
        webServerSendString(message);
    }
}

void
webServerVerifyTables() {
    const int webServerStateEntries = sizeof(webServerStateNames) / sizeof(webServerStateNames[0]);
    if (webServerStateEntries != WEBSERVER_STATE_MAX) {
        reportFatalError("Fix webServerStateNames to match WebServerState");
    }
}

void
webServerHttpdDisplayConfig(struct httpd_config* config) {
    ESP_LOGI(TAG, "httpd_config: port=%d stack=%d handlers=%d sockets=%d", config->server_port, config->stack_size,
             config->max_uri_handlers, config->max_open_sockets);
}

#endif // COMPILE_WEBSERVER

#include "App/Utils/ConfigManager.h"

#include <cstdio>

#include "App/Config/Config.h"

namespace {

constexpr const char* kActiveSection = "Config";
constexpr const char* kActiveIndexKey = "activeIndex";

} // namespace

ConfigManager::ConfigManager() : _storage(), _activeConfig(CONFIG_DEFAULT_INDEX), _lastError{} {}

bool
ConfigManager::loadActive() {
    uint8_t index = CONFIG_DEFAULT_INDEX;
    if (!loadActiveIndex(&index)) {
        index = CONFIG_DEFAULT_INDEX;
    }

    if (!loadConfig(index)) {
        return false;
    }

    _activeConfig = index;
    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::loadConfig(const uint8_t index) {
    char path[48] = {};
    if (!buildConfigPath(index, path, sizeof(path))) {
        return setError("invalid config path");
    }

    if (!_storage.loadFrom(path)) {
        snprintf(_lastError, sizeof(_lastError), "config load failed: %s", _storage.lastError());
        return false;
    }

    _activeConfig = index;
    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::saveActive() {
    return saveConfig(_activeConfig) && saveActiveIndex(_activeConfig);
}

bool
ConfigManager::saveConfig(const uint8_t index) {
    char path[48] = {};
    if (!buildConfigPath(index, path, sizeof(path))) {
        return setError("invalid config path");
    }

    if (!_storage.saveAs(path)) {
        snprintf(_lastError, sizeof(_lastError), "config save failed: %s", _storage.lastError());
        return false;
    }

    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::setActiveConfig(const uint8_t index) {
    if (!validConfigIndex(index)) {
        return setError("invalid active config index");
    }

    if (!configExists(index)) {
        return setError("active config file does not exist");
    }

    uint8_t currentActive = _activeConfig;
    if (loadActiveIndex(&currentActive)) {
        _activeConfig = currentActive;
    }

    if (!backupActive()) {
        return false;
    }

    if (!saveActiveIndex(index)) {
        return false;
    }

    _activeConfig = index;
    return true;
}

bool
ConfigManager::backupActive() {
    char activePath[48] = {};
    if (!buildConfigPath(_activeConfig, activePath, sizeof(activePath))) {
        return setError("invalid active config backup path");
    }

    if (!configExists(_activeConfig)) {
        _lastError[0] = 0;
        return true;
    }

    if (!_storage.copyFile(activePath, CONFIG_BACKUP_PATH)) {
        snprintf(_lastError, sizeof(_lastError), "config backup failed: %s", _storage.lastError());
        return false;
    }

    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::deleteConfig(const uint8_t index) {
    char path[48] = {};
    if (!buildConfigPath(index, path, sizeof(path))) {
        return setError("invalid config path");
    }

    if (!_storage.removeFile(path)) {
        snprintf(_lastError, sizeof(_lastError), "config delete failed: %s", _storage.lastError());
        return false;
    }

    if (_activeConfig == index) {
        _activeConfig = CONFIG_DEFAULT_INDEX;
        return saveActiveIndex(_activeConfig);
    }

    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::configExists(const uint8_t index) const {
    char path[48] = {};
    if (!buildConfigPath(index, path, sizeof(path))) {
        return false;
    }
    return _storage.fileExists(path);
}

uint8_t
ConfigManager::activeConfig() const {
    return _activeConfig;
}

StorageService&
ConfigManager::storage() {
    return _storage;
}

const StorageService&
ConfigManager::storage() const {
    return _storage;
}

const char*
ConfigManager::lastError() const {
    return _lastError;
}

bool
ConfigManager::buildConfigPath(const uint8_t index, char* out, const uint32_t outLength) {
    if (out == nullptr || outLength == 0 || index < 1 || index > CONFIG_MAX_INDEX) {
        return false;
    }

    const int written = snprintf(out, outLength, CONFIG_FILE_PATTERN, static_cast<unsigned>(index));
    return written > 0 && static_cast<uint32_t>(written) < outLength;
}

bool
ConfigManager::validConfigIndex(const uint8_t index) const {
    return index >= 1 && index <= CONFIG_MAX_INDEX;
}

bool
ConfigManager::saveActiveIndex(const uint8_t index) {
    StorageService activeStorage;
    if (!activeStorage.setInt(kActiveSection, kActiveIndexKey, index)) {
        snprintf(_lastError, sizeof(_lastError), "active config update failed: %s", activeStorage.lastError());
        return false;
    }
    if (!activeStorage.saveAs(CONFIG_ACTIVE_PATH)) {
        snprintf(_lastError, sizeof(_lastError), "active config save failed: %s", activeStorage.lastError());
        return false;
    }

    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::loadActiveIndex(uint8_t* index) {
    if (index == nullptr) {
        return setError("invalid active config pointer");
    }

    StorageService activeStorage;
    if (!activeStorage.loadFrom(CONFIG_ACTIVE_PATH)) {
        snprintf(_lastError, sizeof(_lastError), "active config load failed: %s", activeStorage.lastError());
        return false;
    }

    const long value = activeStorage.getInt(kActiveSection, kActiveIndexKey, CONFIG_DEFAULT_INDEX);
    if (value < 1 || value > CONFIG_MAX_INDEX) {
        return setError("active config index out of range");
    }

    *index = static_cast<uint8_t>(value);
    _lastError[0] = 0;
    return true;
}

bool
ConfigManager::setError(const char* message) {
    snprintf(_lastError, sizeof(_lastError), "%s", message ? message : "unknown config error");
    return false;
}

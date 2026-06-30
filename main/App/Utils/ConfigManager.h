#pragma once

#include <stdint.h>

#include "App/Utils/StorageService.h"

class ConfigManager {
  public:
    ConfigManager();

    bool loadActive();
    bool loadConfig(uint8_t index);
    bool saveActive();
    bool saveConfig(uint8_t index);
    bool setActiveConfig(uint8_t index);
    bool backupActive();
    bool deleteConfig(uint8_t index);
    bool configExists(uint8_t index) const;

    uint8_t activeConfig() const;
    StorageService& storage();
    const StorageService& storage() const;
    const char* lastError() const;

    static bool buildConfigPath(uint8_t index, char* out, uint32_t outLength);

  private:
    bool validConfigIndex(uint8_t index) const;
    bool saveActiveIndex(uint8_t index);
    bool loadActiveIndex(uint8_t* index);
    bool setError(const char* message);

    StorageService _storage;
    uint8_t _activeConfig;
    char _lastError[96];
};


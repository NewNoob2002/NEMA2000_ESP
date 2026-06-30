#pragma once

#include <stdint.h>

#include <SimpleIni.h>

typedef void (*StorageServiceEntryCallback)(const char* section, const char* key, const char* value, void* context);

class StorageService {
  public:
    StorageService();

    bool loadFrom(const char* path);
    bool saveAs(const char* path);
    bool copyFile(const char* sourcePath, const char* destinationPath);
    bool removeFile(const char* path);
    bool fileExists(const char* path) const;
    void clear();

    bool isDirty() const;
    const char* lastError() const;

    bool hasKey(const char* section, const char* key) const;
    bool removeKey(const char* section, const char* key);

    const char* getString(const char* section, const char* key, const char* defaultValue = "") const;
    long getInt(const char* section, const char* key, long defaultValue = 0) const;
    double getDouble(const char* section, const char* key, double defaultValue = 0.0) const;
    bool getBool(const char* section, const char* key, bool defaultValue = false) const;

    bool setString(const char* section, const char* key, const char* value);
    bool setInt(const char* section, const char* key, long value);
    bool setDouble(const char* section, const char* key, double value);
    bool setBool(const char* section, const char* key, bool value);

    void forEach(StorageServiceEntryCallback callback, void* context) const;

  private:
    static bool buildFsPath(const char* storagePath, char* out, uint32_t outLength);
    static bool removeFileIfExists(const char* path);

    bool setError(const char* message);
    bool setIniError(const char* operation, SI_Error error);
    void markDirty();

    CSimpleIniA _ini;
    bool _dirty;
    char _lastError[96];
};


#include "App/Utils/StorageService.h"

#include <cstdio>
#include <cstring>

#include <LittleFS.h>

#include "App/Config/Config.h"

namespace {

constexpr const char* kTempSuffix = ".tmp";
constexpr uint32_t kPathBufferLength = 96;
constexpr uint32_t kCopyBufferLength = 256;

bool
validName(const char* value) {
    return value != nullptr && value[0] != 0;
}

} // namespace

StorageService::StorageService() : _dirty(false), _lastError{} {
    _ini.SetUnicode(true);
}

bool
StorageService::loadFrom(const char* path) {
    clear();

    char fsPath[kPathBufferLength] = {};
    if (!buildFsPath(path, fsPath, sizeof(fsPath))) {
        return setError("storage path too long");
    }
    if (!LittleFS.exists(path)) {
        return setError("storage file not found");
    }

    const SI_Error error = _ini.LoadFile(fsPath);
    if (error < 0) {
        clear();
        return setIniError("load", error);
    }

    _dirty = false;
    _lastError[0] = 0;
    return true;
}

bool
StorageService::saveAs(const char* path) {
    char fsPath[kPathBufferLength] = {};
    char tempPath[kPathBufferLength] = {};
    if (!buildFsPath(path, fsPath, sizeof(fsPath))) {
        return setError("storage save path too long");
    }

    const int written = snprintf(tempPath, sizeof(tempPath), "%s%s", fsPath, kTempSuffix);
    if (written <= 0 || static_cast<uint32_t>(written) >= sizeof(tempPath)) {
        return setError("storage temp path too long");
    }

    const SI_Error error = _ini.SaveFile(tempPath, false);
    if (error < 0) {
        remove(tempPath);
        return setIniError("save", error);
    }

    if (!removeFileIfExists(fsPath)) {
        remove(tempPath);
        return setError("failed to replace storage file");
    }
    if (rename(tempPath, fsPath) != 0) {
        remove(tempPath);
        return setError("failed to commit storage file");
    }

    _dirty = false;
    _lastError[0] = 0;
    return true;
}

bool
StorageService::copyFile(const char* sourcePath, const char* destinationPath) {
    char sourceFsPath[kPathBufferLength] = {};
    char destinationFsPath[kPathBufferLength] = {};
    if (!buildFsPath(sourcePath, sourceFsPath, sizeof(sourceFsPath))
        || !buildFsPath(destinationPath, destinationFsPath, sizeof(destinationFsPath))) {
        return setError("storage copy path too long");
    }

    FILE* source = fopen(sourceFsPath, "rb");
    if (source == nullptr) {
        return setError("storage source file not found");
    }

    char tempPath[kPathBufferLength] = {};
    const int written = snprintf(tempPath, sizeof(tempPath), "%s%s", destinationFsPath, kTempSuffix);
    if (written <= 0 || static_cast<uint32_t>(written) >= sizeof(tempPath)) {
        fclose(source);
        return setError("storage copy temp path too long");
    }

    FILE* destination = fopen(tempPath, "wb");
    if (destination == nullptr) {
        fclose(source);
        return setError("failed to open storage destination");
    }

    uint8_t buffer[kCopyBufferLength] = {};
    bool ok = true;
    while (!feof(source)) {
        const size_t bytesRead = fread(buffer, 1, sizeof(buffer), source);
        if (bytesRead > 0 && fwrite(buffer, 1, bytesRead, destination) != bytesRead) {
            ok = false;
            break;
        }
        if (ferror(source)) {
            ok = false;
            break;
        }
    }

    ok = (fclose(destination) == 0) && ok;
    fclose(source);

    if (!ok) {
        remove(tempPath);
        return setError("failed to copy storage file");
    }

    remove(destinationFsPath);
    if (rename(tempPath, destinationFsPath) != 0) {
        remove(tempPath);
        return setError("failed to commit storage copy");
    }

    _lastError[0] = 0;
    return true;
}

bool
StorageService::removeFile(const char* path) {
    char fsPath[kPathBufferLength] = {};
    if (!buildFsPath(path, fsPath, sizeof(fsPath))) {
        return setError("storage remove path too long");
    }

    if (!LittleFS.exists(path)) {
        _lastError[0] = 0;
        return true;
    }
    if (remove(fsPath) != 0) {
        return setError("failed to remove storage file");
    }

    _lastError[0] = 0;
    return true;
}

bool
StorageService::fileExists(const char* path) const {
    return validName(path) && LittleFS.exists(path);
}

void
StorageService::clear() {
    _ini.Reset();
    _ini.SetUnicode(true);
    _dirty = false;
    _lastError[0] = 0;
}

bool
StorageService::isDirty() const {
    return _dirty;
}

const char*
StorageService::lastError() const {
    return _lastError;
}

bool
StorageService::hasKey(const char* section, const char* key) const {
    return validName(section) && validName(key) && (_ini.GetValue(section, key, nullptr) != nullptr);
}

bool
StorageService::removeKey(const char* section, const char* key) {
    if (!validName(section) || !validName(key)) {
        return setError("invalid section or key");
    }

    const bool removed = _ini.Delete(section, key, true);
    if (removed) {
        markDirty();
    }
    return removed;
}

const char*
StorageService::getString(const char* section, const char* key, const char* defaultValue) const {
    if (!validName(section) || !validName(key)) {
        return defaultValue;
    }
    return _ini.GetValue(section, key, defaultValue);
}

long
StorageService::getInt(const char* section, const char* key, const long defaultValue) const {
    if (!validName(section) || !validName(key)) {
        return defaultValue;
    }
    return _ini.GetLongValue(section, key, defaultValue);
}

double
StorageService::getDouble(const char* section, const char* key, const double defaultValue) const {
    if (!validName(section) || !validName(key)) {
        return defaultValue;
    }
    return _ini.GetDoubleValue(section, key, defaultValue);
}

bool
StorageService::getBool(const char* section, const char* key, const bool defaultValue) const {
    if (!validName(section) || !validName(key)) {
        return defaultValue;
    }
    return _ini.GetBoolValue(section, key, defaultValue);
}

bool
StorageService::setString(const char* section, const char* key, const char* value) {
    if (!validName(section) || !validName(key) || value == nullptr) {
        return setError("invalid string setting");
    }

    const SI_Error error = _ini.SetValue(section, key, value);
    if (error < 0) {
        return setIniError("set string", error);
    }
    markDirty();
    return true;
}

bool
StorageService::setInt(const char* section, const char* key, const long value) {
    if (!validName(section) || !validName(key)) {
        return setError("invalid int setting");
    }

    const SI_Error error = _ini.SetLongValue(section, key, value);
    if (error < 0) {
        return setIniError("set int", error);
    }
    markDirty();
    return true;
}

bool
StorageService::setDouble(const char* section, const char* key, const double value) {
    if (!validName(section) || !validName(key)) {
        return setError("invalid double setting");
    }

    const SI_Error error = _ini.SetDoubleValue(section, key, value);
    if (error < 0) {
        return setIniError("set double", error);
    }
    markDirty();
    return true;
}

bool
StorageService::setBool(const char* section, const char* key, const bool value) {
    if (!validName(section) || !validName(key)) {
        return setError("invalid bool setting");
    }

    const SI_Error error = _ini.SetBoolValue(section, key, value);
    if (error < 0) {
        return setIniError("set bool", error);
    }
    markDirty();
    return true;
}

void
StorageService::forEach(StorageServiceEntryCallback callback, void* context) const {
    if (callback == nullptr) {
        return;
    }

    CSimpleIniA::TNamesDepend sections;
    _ini.GetAllSections(sections);

    for (CSimpleIniA::TNamesDepend::const_iterator section = sections.begin(); section != sections.end(); ++section) {
        CSimpleIniA::TNamesDepend keys;
        _ini.GetAllKeys(section->pItem, keys);

        for (CSimpleIniA::TNamesDepend::const_iterator key = keys.begin(); key != keys.end(); ++key) {
            callback(section->pItem, key->pItem, _ini.GetValue(section->pItem, key->pItem, ""), context);
        }
    }
}

bool
StorageService::buildFsPath(const char* storagePath, char* out, const uint32_t outLength) {
    if (!validName(storagePath) || out == nullptr || outLength == 0) {
        return false;
    }

    const int written = snprintf(out, outLength, "%s%s", MOUNTPOINT, storagePath);
    return written > 0 && static_cast<uint32_t>(written) < outLength;
}

bool
StorageService::removeFileIfExists(const char* path) {
    if (!validName(path)) {
        return false;
    }
    if (!LittleFS.exists(path + strlen(MOUNTPOINT))) {
        return true;
    }
    return remove(path) == 0;
}

bool
StorageService::setError(const char* message) {
    snprintf(_lastError, sizeof(_lastError), "%s", message ? message : "unknown error");
    return false;
}

bool
StorageService::setIniError(const char* operation, const SI_Error error) {
    snprintf(_lastError, sizeof(_lastError), "%s failed: %d", operation ? operation : "ini operation",
             static_cast<int>(error));
    return false;
}

void
StorageService::markDirty() {
    _dirty = true;
    _lastError[0] = 0;
}


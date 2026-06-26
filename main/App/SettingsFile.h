#pragma once

#include <stddef.h>
#include "mcu_typedef.h"

#define SETTINGS_FILE_PROFILE_DIR         "/littlefs/profiles"
#define SETTINGS_FILE_ACTIVE_PROFILE_FILE "/littlefs/profiles/active.txt"
#define SETTINGS_FILE_DEFAULT_PROFILE     "default.ini"

bool settingsFileNameIsSafe(const char* name);
bool settingsFileBuildProfilePath(const char* name, char* path, size_t pathLength);
bool settingsFileEnsureProfileDir();

bool settingsFileReadActiveProfile(char* name, size_t nameLength);
bool settingsFileWriteActiveProfile(const char* name);
bool settingsFileApplyActiveProfile();
bool settingsFileActivateProfile(const char* name);

bool settingsFileLoad(const char* path, settings_t* target, char* error, size_t errorLength);
bool settingsFileSave(const char* path, const settings_t* source);
bool settingsFileValidate(const char* path, char* error, size_t errorLength);

void settingsFileInit();
void settingsFileMarkDirty();
bool settingsFileSaveActive();
bool settingsFileSaveIfDirty();
bool settingsFileIsDirty();

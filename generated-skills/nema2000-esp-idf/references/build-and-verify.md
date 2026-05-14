# Build And Verify

Use ESP-IDF build verification after firmware edits.

## Local Windows Build

This workspace uses a Windows ESP-IDF PowerShell profile. From the repository root:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1
idf.py build
```

The bundled helper script runs the same sequence:

```powershell
.\generated-skills\nema2000-esp-idf\scripts\build.ps1
```

## Build Expectations

- Current lockfile indicates ESP-IDF `5.5.4` and target `esp32`.
- Use the repository root as the working directory.
- Run a full `idf.py build` after changing CMake, `sdkconfig`, component dependencies, compile flags, HAL startup, protocol conversion, or public headers.
- If only documentation in this skill changes, firmware build is not required.

## Reading Failures

- Look for the first real compiler, linker, CMake, or Kconfig error. The final summary often hides the useful failure.
- If a failure mentions an inactive symbol, inspect `main/CompileConfig.h` and surrounding `#ifdef` blocks.
- If a failure mentions a missing include, inspect `main/CMakeLists.txt` and the target component's `INCLUDE_DIRS`.
- If a failure appears inside `components/arduino-esp32`, first check whether application code is calling an API incorrectly before editing the vendored tree.

## CMake Notes

- Root `CMakeLists.txt` must keep ESP-IDF boilerplate order: `cmake_minimum_required`, `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`, then `project(...)`.
- `main/CMakeLists.txt` currently glob-registers main and HAL sources. New files under those directories are picked up automatically, but component files under `components/` need their component CMake files to include them.
- Prefer explicit component dependencies when adding cross-component includes or links.

## sdkconfig Notes

- Avoid manual churn in `sdkconfig` unless changing an actual configuration requirement.
- If changing generated config, explain which feature requires it and verify with `idf.py build`.
- Prefer committed defaults in `sdkconfig.defaults` only when the setting should be portable across workspaces.

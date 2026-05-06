file(GLOB_RECURSE sources ./src/*.*)

idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS src
    REQUIRES arduino-esp32 soc freertos
)

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "V1.34")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(s20_app)

set(RELEASE_BIN_NAME "${CMAKE_PROJECT_NAME}_${PROJECT_VER}.bin")
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/${RELEASE_BIN_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    "${CMAKE_BINARY_DIR}/${RELEASE_BIN_NAME}"
    COMMAND ${CMAKE_COMMAND} -E echo "Generated ${CMAKE_BINARY_DIR}/${RELEASE_BIN_NAME}"
    DEPENDS gen_project_binary
    VERBATIM
)
add_custom_target(release_bin ALL DEPENDS "${CMAKE_BINARY_DIR}/${RELEASE_BIN_NAME}")

cmake_minimum_required(VERSION 3.20)

if(ESP_PLATFORM)
    # using this library as a component in an ESP-IDF project
    idf_component_register(
        SRC_DIRS src
        INCLUDE_DIRS include)
    return() # ignore the rest of this file
endif()

file(GLOB_RECURSE sources
    ./*.*
    HAL/*.*
)

set(INC_DIRS . HAL)

idf_component_register(SRCS ${sources} INCLUDE_DIRS ${INC_DIRS})

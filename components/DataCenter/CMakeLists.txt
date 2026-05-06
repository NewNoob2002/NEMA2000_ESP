idf_component_register(SRCS
    "Account.cpp"
    "DataCenter.cpp"
    "ringBuffer/ringBuffer.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_timer freertos)

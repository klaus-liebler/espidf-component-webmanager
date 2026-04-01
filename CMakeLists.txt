if(NOT DEFINED GENERATED_DIR)
    message(ERROR "NOT DEFINED GENERATED_DIR!!!")
endif()

idf_component_register(
    INCLUDE_DIRS "cpp" "${GENERATED_DIR}"
    SRCS "cpp/webmanager.cc"
    REQUIRES "flatbuffers-25.1.24" "mdns" "common"  "errorcodes" "esp_http_server" "esp_https_server" "spi_flash" "spiffs"  "app_update" "esp_wifi" "esp_driver_tsens"
    )


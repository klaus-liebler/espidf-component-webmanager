#pragma once
#include <inttypes.h>
#include <cstring>
#include <ctime>
#include <esp_wifi.h>

namespace webmanager{
    constexpr time_t FAR_FUTURE(INT64_MAX);
    constexpr size_t MAX_AP_NUM{8};
    constexpr BaseType_t RECONNECT_TIMEOUT_US{8'000'000};
    constexpr BaseType_t SHUTDOWN_AP_TIMEOUT_US{30'000'000};
    constexpr BaseType_t COMMON_TIMEOUT_US{30'000'000};
    constexpr wifi_auth_mode_t AP_AUTHMODE{wifi_auth_mode_t::WIFI_AUTH_WPA2_PSK};
    constexpr const char* NVS_PARTITION{"nvs"};
    constexpr const char* WIFI_NVS_NAMESPACE{"wifimananger"};
    constexpr const char* nvs_key_wifi_ssid{"ssid"};
    constexpr const char* nvs_key_wifi_password{"password"};
    constexpr size_t HTTP_BUFFER_SIZE{2*2048};
    constexpr size_t MAX_FILE_SIZE{256*1024};
    /* Max length a file path can have on storage */
    constexpr size_t FILE_PATH_MAX{20+ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN};
    constexpr const char* FILES_GLOB{"/files/*"};
    constexpr const size_t FILES_BASE_PATH_LEN{6};

    #define _(n) n
    enum class WorkingState{//bezieht sich auf den State, der zuletzt erreicht wurde (also nicht der, der als nächstes erreicht werden soll)
        #include "webmanager_workingstate.inc"
    };

    #undef _
    #define _(n) #n

    constexpr const char* WorkingStateStrings[]={
        #include "webmanager_workingstate.inc"
    };

    #undef _

}
#pragma once
#include <sdkconfig.h>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include "esp_netif.h"

#include "esp_tls.h"
#include <hal/efuse_hal.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include <lwip/ip4_addr.h>
#include <driver/gpio.h>
#include <nvs.h>
#include <spi_flash_mmap.h>
#include <esp_sntp.h>
#include <time.h>
#include <mdns.h>
#include <common-esp32.hh>
#include <esp_log.h>
#include <sys/time.h>
#if (CONFIG_HTTPD_MAX_REQ_HDR_LEN < 1024)
#error "CONFIG_HTTPD_MAX_REQ_HDR_LEN<1024 (Max HTTP Request Header Length)"
#endif

#ifndef CONFIG_HTTPD_WS_SUPPORT
#error "Enable Websocket support for HTTPD in menuconfig"
#endif
#include "esp_vfs.h"

#define TAG "WMAN"
#include "webmanager_constants.hh"
#include "webmanager_interfaces.hh"
#include "webmanager_async_response.hh"
#include "wsprotocol_cpp/ws_protocol.hh"

namespace webmanager
{
    extern const char webmanager_html_br_start[] asm("_binary_index_compressed_br_start");
    extern const size_t webmanager_html_br_length asm("index_compressed_br_length");

    class M : public webmanager::iWebmanagerCallback
    {
    private:
        static M *singleton;
        uint8_t *http_buffer;
        const char* hostname{nullptr};

        esp_netif_t *wifi_netif_sta{nullptr};
        esp_netif_t *wifi_netif_ap{nullptr};

        wifi_config_t wifi_config_sta = {}; // 132byte
        wifi_config_t wifi_config_ap = {};  // 132byte
        bool fallbackToStoredStaConfig{false};//false means: Fallback is AccessPoint
        //wird auf true gesetzt, wenn eine Sta-Verbindung erfolgreich ist und die config im NVS gespeichert wurde
        //wird auf false gesetzt, wenn der accessPoint gestartet wird

        SemaphoreHandle_t webmanager_semaphore{nullptr}; // stellt sicher, dass die Timer-Aufrufe nicht überlappen können
        TimerHandle_t timSupervisor{nullptr};

        httpd_handle_t http_server{nullptr};
        int websocket_file_descriptor{-1};
        std::string auth_username{""};
        std::string auth_password{""};
        std::string session_token{""};
        time_t session_expiry_us{0};
        const time_t SESSION_TIMEOUT_US = 3600000000; // 1 hour in microseconds

        // Das ist der Status, der alles beschreiben muss
        WorkingState workingState{WorkingState::AP_STARTED};
        time_t tTimeout_us{INT64_MAX};
        time_t tShutdownAp_us{INT64_MAX};
        time_t tReconnect_us{INT64_MAX};

        bool staConnectionState{false};

        // Deadline-Modell fuer den AP-Fallback (ersetzt den vormaligen Attempt-Zaehler
        // remainingAttempsToConnectAsSTA/RECONNECTS_ON_STARTUP/RECONNECTS_ON_OPERATION, s.
        // docs/plan_v2/03-wifimanager-review.md): apFallbackTimeout_us wird einmalig in Begin()
        // gesetzt (konstant danach), giveUpAt_us wird beim ERSTEN Disconnect einer Serie scharf-
        // geschaltet (now_us + apFallbackTimeout_us) und bei erfolgreichem Connect wieder auf
        // FAR_FUTURE zurueckgesetzt. FAR_FUTURE als Sentinel fuer apFallbackTimeout_us bedeutet
        // "nie AP oeffnen, fuer immer weiterversuchen" (heutiges Verhalten, Default).
        time_t apFallbackTimeout_us{FAR_FUTURE};
        time_t giveUpAt_us{FAR_FUTURE};

        // Korrelations-ID des zuletzt entgegengenommenen RequestWifiConnect -- wird gebraucht,
        // weil ResponseWifiConnect asynchron aus wifi_event_handler/ip_event_handler heraus
        // verschickt wird (nicht direkt aus dem Request-Handler), die requestId des Requests aber
        // trotzdem unveraendert in der Response zurueckgegeben werden soll (s. Schema-Kommentar
        // in ws-protocol/wifimanager.cs).
        uint16_t lastWifiConnectRequestId{0};

        const char* ws2c(WorkingState w){
            return WorkingStateStrings[static_cast<size_t>(w)];
        }
        
        void setStatus(WorkingState workingState, time_t tTimeout_us=FAR_FUTURE, time_t tShutdownAp_us=INT64_MAX, time_t tReconnect_us=INT64_MAX)
        {
            if(this->workingState!=workingState){
                this->workingState = workingState;
                ESP_LOGI(TAG, "Switch to workingState %s", ws2c(this->workingState));
            }
            if(this->tReconnect_us!=tReconnect_us){
                this->tReconnect_us=tReconnect_us;
                if(this->tReconnect_us==FAR_FUTURE){
                    ESP_LOGI(TAG, "Deactivating tReconnect while beeing in state %s", ws2c(this->workingState));
                }else{
                    ESP_LOGI(TAG, "Setting tReconnect to %llums while beeing in state %s", tReconnect_us/1000, ws2c(this->workingState));
                }
            }
            if(this->tTimeout_us!=tTimeout_us){
                this->tTimeout_us=tTimeout_us;
                if(this->tTimeout_us==FAR_FUTURE){
                    ESP_LOGI(TAG, "Deactivating tTimeout_us while beeing in state %s", ws2c(this->workingState));
                }else{
                    ESP_LOGI(TAG, "Setting tTimeout_us to %llums while beeing in state %s", tTimeout_us/1000, ws2c(this->workingState));
                }
            }
            if(this->tShutdownAp_us!=tShutdownAp_us){
                this->tShutdownAp_us=tShutdownAp_us;
                if(this->tShutdownAp_us==FAR_FUTURE){
                    ESP_LOGI(TAG, "Deactivating tShutdownAp_us while beeing in state %s", ws2c(this->workingState));
                }else{
                    ESP_LOGI(TAG, "Setting tShutdownAp_us to %llums while beeing in state %s", tShutdownAp_us/1000, ws2c(this->workingState));
                }
            }
        }

        std::vector<iWebmanagerPlugin *> *plugins{nullptr};

        M() { http_buffer = new uint8_t[HTTP_BUFFER_SIZE]; }

        void connectAsSTA(time_t now_us)
        {
            ESP_LOGI(TAG, "Trying to connect as station. {'ssid':'%s', 'password':'%s', 'fallback':'%s'}", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password, fallbackToStoredStaConfig?"STORED_STA":"AP");
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
            ESP_ERROR_CHECK(esp_wifi_connect());
            this->setStatus(WorkingState::KEEP_CONNECTION, now_us+COMMON_TIMEOUT_US, FAR_FUTURE, FAR_FUTURE);
        }

        void configureAndOpenAccessPointAndSetStatus()
        {
            fallbackToStoredStaConfig=false;//because now, the AP must be the fallback
            ESP_LOGI(TAG, "Opening Access Point. {'ssid':'%s', 'password':'%s'}", wifi_config_ap.ap.ssid, wifi_config_ap.ap.password);
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if(mode!=WIFI_MODE_APSTA){
                //has to be done "lazy", because otherwise already connected stations loose their connections
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
            }
            this->setStatus(WorkingState::AP_STARTED, FAR_FUTURE);
        }

        void sendWifiConnectionNotSuccessfulMessage()
        {
            WsProtocol::wifimanager::ResponseWifiConnect::Payload resp{};
            resp.requestId = lastWifiConnectRequestId;
            resp.success = false;
            resp.ssid = "";
            resp.ip = 0;
            resp.netmask = 0;
            resp.gateway = 0;
            resp.rssi = 0;
            uint8_t buf[256];
            size_t len = WsProtocol::wifimanager::ResponseWifiConnect::Encode(resp, buf, sizeof(buf));
            ESP_LOGI(TAG, "sendWifiConnectionNotSuccessfulMessage: requestId=%d, encoded len=%d, fd=%d", (int)resp.requestId, (int)len, (int)websocket_file_descriptor);
            if (len > 0)
            {
                esp_err_t ret = SendRawAsync(buf, len);
                // ESP_ERR_INVALID_STATE = kein Websocket-Client verbunden -- passiert erwartbar bei
                // jedem Boot-Zeit-Autoreconnect zu einer gespeicherten SSID (KEEP_CONNECTION), noch
                // bevor ueberhaupt ein Browser die Seite geoeffnet hat. Nur andere Fehler sind
                // tatsaechlich ungewoehnlich.
                if (ret == ESP_ERR_INVALID_STATE) ESP_LOGD(TAG, "sendWifiConnectionNotSuccessfulMessage: no websocket client connected (yet)");
                else if (ret != ESP_OK) ESP_LOGW(TAG, "sendWifiConnectionNotSuccessfulMessage: SendRawAsync failed with %s", esp_err_to_name(ret));
            }
        }

        void sendWifiConnectionSuccessfulMessage(const esp_netif_ip_info_t *ip){
            create_or_update_sta_config();
            wifi_ap_record_t ap = {};
            esp_wifi_sta_get_ap_info(&ap);
            // 'ap.rssi' wurde zuvor geholt, aber nie in die Response geschrieben (echter Bug,
            // per Recherche bestaetigt) -- jetzt korrekt gesetzt.
            WsProtocol::wifimanager::ResponseWifiConnect::Payload resp{};
            resp.requestId = lastWifiConnectRequestId;
            resp.success = true;
            resp.ssid = (const char*)wifi_config_sta.sta.ssid;
            resp.ip = ip->ip.addr;
            resp.netmask = ip->netmask.addr;
            resp.gateway = ip->gw.addr;
            resp.rssi = ap.rssi;
            uint8_t buf[256];
            size_t len = WsProtocol::wifimanager::ResponseWifiConnect::Encode(resp, buf, sizeof(buf));
            ESP_LOGI(TAG, "sendWifiConnectionSuccessfulMessage: requestId=%d, ssid='%s', ip=%s, encoded len=%d, fd=%d", (int)resp.requestId, resp.ssid, ip4addr_ntoa((const ip4_addr_t*)&ip->ip), (int)len, (int)websocket_file_descriptor);
            if (len > 0)
            {
                esp_err_t ret = SendRawAsync(buf, len);
                // ESP_ERR_INVALID_STATE = kein Websocket-Client verbunden -- passiert erwartbar bei
                // jedem Boot-Zeit-Autoreconnect zu einer gespeicherten SSID (KEEP_CONNECTION), noch
                // bevor ueberhaupt ein Browser die Seite geoeffnet hat. Nur andere Fehler sind
                // tatsaechlich ungewoehnlich.
                if (ret == ESP_ERR_INVALID_STATE) ESP_LOGD(TAG, "sendWifiConnectionSuccessfulMessage: no websocket client connected (yet)");
                else if (ret != ESP_OK) ESP_LOGW(TAG, "sendWifiConnectionSuccessfulMessage: SendRawAsync failed with %s", esp_err_to_name(ret));
            }
        }

        esp_err_t delete_sta_config()
        {
            esp_err_t ret;
            nvs_handle handle{0};
            // Removed unused variable 'ret'
            GOTO_ERROR_ON_ERROR(nvs_open_from_partition(NVS_PARTITION, WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle), "Unable to open nvs partition");
            GOTO_ERROR_ON_ERROR(nvs_erase_key(handle, nvs_key_wifi_ssid), "Unable to delete wifi ssid");
            GOTO_ERROR_ON_ERROR(nvs_erase_key(handle, nvs_key_wifi_password), "Unable to delete wifi password");
            ret = nvs_commit(handle);
            ESP_LOGI(TAG, "Successfully erased Wifi Sta configuration in flash");
        error:
            nvs_close(handle);
            return ret;
        }

        esp_err_t create_or_update_sta_config()
        {
            nvs_handle handle;
            esp_err_t ret = ESP_OK;
            char tmp_ssid[33];     /**< SSID of target AP. */
            char tmp_password[64]; /**< Password of target AP. */
            bool changeSsid{false};
            bool changePassword{false};
            size_t sz{0};

            ESP_LOGD(TAG, "About to save config to flash!!");
            GOTO_ERROR_ON_ERROR(nvs_open_from_partition(NVS_PARTITION, WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle), "Unable to open nvs partition");
            sz = sizeof(tmp_ssid);
            ret = nvs_get_str(handle, nvs_key_wifi_ssid, tmp_ssid, &sz);
            if ((ret == ESP_OK && strcmp((char *)tmp_ssid, (char *)wifi_config_sta.sta.ssid) != 0) || ret == ESP_ERR_NVS_NOT_FOUND)
            {
                /* different ssid or ssid does not exist in flash: save new ssid */
                GOTO_ERROR_ON_ERROR(nvs_set_str(handle, nvs_key_wifi_ssid, (const char *)wifi_config_sta.sta.ssid), "Unable to nvs_set_str(handle, \"ssid\", ssid_sta)");
                ESP_LOGD(TAG, "wifi_manager_wrote wifi_sta_config: ssid: %s", wifi_config_sta.sta.ssid);
                changeSsid = true;
            }

            sz = sizeof(tmp_password);
            ret = nvs_get_str(handle, nvs_key_wifi_password, tmp_password, &sz);
            if ((ret == ESP_OK && strcmp((char *)tmp_password, (char *)wifi_config_sta.sta.password) != 0) || ret == ESP_ERR_NVS_NOT_FOUND)
            {
                /* different password or password does not exist in flash: save new password */
                GOTO_ERROR_ON_ERROR(nvs_set_str(handle, nvs_key_wifi_password, (const char *)wifi_config_sta.sta.password), "Unable to nvs_set_str(handle, \"password\", password_sta)");
                ESP_LOGI(TAG, "wifi_manager_wrote wifi_sta_config: password: %s", wifi_config_sta.sta.password);
                changePassword = true;
            }
            if (changeSsid || changePassword)
            {
                ret = nvs_commit(handle);
                ESP_LOGI(TAG, "Updated Ssid '%s' and/or password '%s' have been written to flash", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password);
            }
            else
            {
                ESP_LOGI(TAG, "Ssid '%s' and/or password '%s' have not been changed.", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password);
            }
        error:
            nvs_close(handle);
            return ret;
        }

        esp_err_t read_sta_config()
        {
            nvs_handle handle;
            esp_err_t ret = ESP_OK;
            size_t sz;
            GOTO_ERROR_ON_ERROR(nvs_open_from_partition(NVS_PARTITION, WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle), "Unable to open nvs partition '%s' and namespace '%s' ", NVS_PARTITION, WIFI_NVS_NAMESPACE);
            sz = sizeof(wifi_config_sta.sta.ssid);
            if(ESP_OK != nvs_get_str(handle, nvs_key_wifi_ssid, (char *)wifi_config_sta.sta.ssid, &sz)){
                //no error message here, because it can be normal that there is no config in the flash. In this case, we just want to fallback to the AccessPoint mode
                ret=ESP_FAIL;
                goto error;
            }
            sz = sizeof(wifi_config_sta.sta.password);
            if(ESP_OK != nvs_get_str(handle, nvs_key_wifi_password, (char *)wifi_config_sta.sta.password, &sz)){
                //no error message here, because it can be normal that there is no config in the flash. In this case, we just want to fallback to the AccessPoint mode
                ret=ESP_FAIL;
                goto error;
            }
            ESP_LOGI(TAG, "Successfully read Wifi credentials {'ssid':'%s', 'password':'%s'}", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password);
            ret = (wifi_config_sta.sta.ssid[0] == '\0') ? ESP_FAIL : ESP_OK;
        error:
            nvs_close(handle);
            return ret;
        }

        void supervisorTask(){
            while(true){
                this->Supervise();
                vTaskDelay(pdMS_TO_TICKS(4000));
            }
        }

        void wifi_event_handler(esp_event_base_t event_base, int32_t event_id, void *event_data)
        {
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            time_t now_us = esp_timer_get_time();
            switch (event_id)
            {
            case WIFI_EVENT_SCAN_DONE:
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                staConnectionState = false;
                // Deadline nur beim ERSTEN Disconnect einer Serie scharfschalten -- nachfolgende
                // Disconnects innerhalb derselben Serie (waehrend wir schon am Zurueckverbinden
                // sind) duerfen die urspruengliche Deadline nicht immer wieder nach hinten
                // verschieben, sonst wuerde (wie im alten Zaehler-Modell) nie aufgegeben.
                if (giveUpAt_us == FAR_FUTURE){
                    giveUpAt_us = now_us + apFallbackTimeout_us;
                }
                if (now_us > giveUpAt_us){
                    if(fallbackToStoredStaConfig && read_sta_config()){
                        ESP_LOGW(TAG, "Establishing connection to new SSID failed finally. Go back to stored SSID {'ssid':'%s', 'password':'%s'}", wifi_config_ap.ap.ssid, wifi_config_ap.ap.password);
                        fallbackToStoredStaConfig=false;
                        giveUpAt_us = now_us + apFallbackTimeout_us;
                        connectAsSTA(now_us);
                        this->setStatus(WorkingState::KEEP_CONNECTION, now_us + COMMON_TIMEOUT_US);
                        this->sendWifiConnectionNotSuccessfulMessage();
                    }
                    else{
                        ESP_LOGW(TAG, "Establishing connection to SSID failed finally. Go back to Access Point Mode {'ssid':'%s', 'password':'%s'}", wifi_config_ap.ap.ssid, wifi_config_ap.ap.password);
                        this->sendWifiConnectionNotSuccessfulMessage();
                        configureAndOpenAccessPointAndSetStatus();
                    }
                }
                else{
                    ESP_LOGW(TAG, "Establishing connection with SSID '%s' failed. Retrying until %lldms.", wifi_config_sta.sta.ssid, giveUpAt_us/1000);
                    this->setStatus(WorkingState::KEEP_CONNECTION, now_us + COMMON_TIMEOUT_US, FAR_FUTURE, now_us+RECONNECT_TIMEOUT_US);
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Established connection to SSID successfully. Now, waiting for a IP address... {'ssid':'%s', 'password':'%s'}", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password);
                staConnectionState = true;
                create_or_update_sta_config();
                fallbackToStoredStaConfig=true;
                this->giveUpAt_us=FAR_FUTURE;
                //Das Timeout muss hier auf einen sinnvollen wert gesetzt werden, weil wir ja noch keine IP-Adresse haben
                //erst wenn diese im IP-Handler gesetzt wird, kann das timeout auf FAR-FUTURE gesetzt werden
                this->setStatus(WorkingState::KEEP_CONNECTION, now_us + COMMON_TIMEOUT_US, FAR_FUTURE, FAR_FUTURE);
                //Nein, erst wenn die IP-Adresse gesetzt wurde... this->sendWifiConnectionSuccessfulMessage()
                break;
            case WIFI_EVENT_AP_START:
            {
                ESP_LOGI(TAG, "Successfully started Access Point with ssid %s and password '%s'. Webmanager is here: https://%s", wifi_config_ap.ap.ssid, wifi_config_ap.ap.password, hostname);
                break;
            }
            case WIFI_EVENT_AP_STOP:
            {
                ESP_LOGI(TAG, "Successfully closed Access Point.");
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED:
            {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " joined this AccessPoint, AID=%d", MAC2STR(event->mac), event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " leaved this AccessPoint, AID=%d", MAC2STR(event->mac), event->aid);
                break;
            }
            }
            xSemaphoreGive(webmanager_semaphore);
        }

        void ip_event_handler(esp_event_base_t event_base, int32_t event_id, void *event_data)
        {
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            time_t now_us = esp_timer_get_time();
            switch (event_id)
            {
            case IP_EVENT_ASSIGNED_IP_TO_CLIENT:{
                const ip_event_assigned_ip_to_client_t *ip = (ip_event_assigned_ip_to_client_t *)event_data;
                ESP_LOGI(TAG, "Connected Wifi Station got IP from DHCP {'ip':'" IPSTR "'}", IP2STR(&ip->ip));
                break;
            }
            case IP_EVENT_STA_GOT_IP:
            {
                const ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                const esp_netif_ip_info_t *ip = &(event->ip_info);
                ESP_LOGI(TAG, "Wifi Sta got IP from DHCP {'ip':'" IPSTR "', 'netmask':'" IPSTR "','gw':'" IPSTR "', 'hostname':'%s'}", IP2STR(&ip->ip), IP2STR(&ip->netmask), IP2STR(&ip->gw), hostname);
                this->setStatus(WorkingState::KEEP_CONNECTION, FAR_FUTURE, now_us+SHUTDOWN_AP_TIMEOUT_US, FAR_FUTURE);
                this->sendWifiConnectionSuccessfulMessage(ip);
                esp_sntp_init();
                break;
            }
            case IP_EVENT_ETH_GOT_IP:
            {
                const ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                const esp_netif_ip_info_t *ip = &(event->ip_info);
                ESP_LOGI(TAG, "ETHERNET got IP from DHCP: {'ip':'" IPSTR "', 'netmask':'" IPSTR "','gw':'" IPSTR "', 'hostname':'%s'}", IP2STR(&ip->ip), IP2STR(&ip->netmask), IP2STR(&ip->gw), hostname);
                esp_sntp_init(); // seems to be safe if called twice (ETH and WIFI STA!)
                break;
            }
            case IP_EVENT_ETH_LOST_IP:
            {
                ESP_LOGI(TAG, "IP_EVENT_ETH_LOST_IP");
                break;
            }
            case IP_EVENT_STA_LOST_IP:
            {
                ESP_LOGD(TAG, "IP_EVENT_STA_LOST_IP");
                break;
            }
            }
            xSemaphoreGive(webmanager_semaphore);
        }

        void sntp_handler()
        {
            time_t now;
            char strftime_buf[64];
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "Notification of a time synchronization. The current date/time in Berlin is: %s", strftime_buf);
            for (const auto &p : *this->plugins)
            {
                p->OnTimeUpdate(this);
            }
            // LogJournal(messagecodes::C::SNTP, esp_timer_get_time() / 1000);
        }

        static void ws_async_send(void *arg)
        {
            M *myself = M::GetSingleton();
            AsyncResponse *a = static_cast<AsyncResponse *>(arg);
            assert(a);
            assert(a->buffer);
            assert(a->buffer_len);
            assert(myself);
            if (myself->http_server && myself->websocket_file_descriptor != -1)
            {
                httpd_ws_frame_t ws_pkt = {false, false, HTTPD_WS_TYPE_BINARY, a->buffer, a->buffer_len};
                esp_err_t ret = httpd_ws_send_frame_async(myself->http_server, myself->websocket_file_descriptor, &ws_pkt);
                if (ret == ESP_OK)
                {
                    ESP_LOGD(TAG, "httpd_ws_send_frame_async: data_len:%u\n", ws_pkt.len);
                }
                else
                {
                    ESP_LOGW(TAG, "httpd_ws_send_frame_async failed (0x%x). Invalidating websocket session fd %d", (unsigned int)ret, myself->websocket_file_descriptor);
                    httpd_sess_trigger_close(myself->http_server, myself->websocket_file_descriptor);
                    myself->websocket_file_descriptor = -1;
                }
                // should be syncronous. So the buffer can be deleted, when the function returns
            }
            delete a;
        }

        void close_active_websocket_before_ap_shutdown()
        {
            if (!http_server || websocket_file_descriptor == -1)
            {
                return;
            }

            const int ws_fd = websocket_file_descriptor;

            if (httpd_ws_get_fd_info(http_server, ws_fd) == HTTPD_WS_CLIENT_WEBSOCKET)
            {
                uint8_t close_payload[2] = {0x03, 0xE8}; // 1000 = normal closure
                httpd_ws_frame_t close_pkt = {false, false, HTTPD_WS_TYPE_CLOSE, close_payload, sizeof(close_payload)};
                esp_err_t send_ret = httpd_ws_send_frame_async(http_server, ws_fd, &close_pkt);
                if (send_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to send websocket close frame to fd %d (%d)", ws_fd, send_ret);
                }
            }

            httpd_sess_trigger_close(http_server, ws_fd);
            websocket_file_descriptor = -1;
            ESP_LOGI(TAG, "Closed active websocket session fd %d before AP shutdown", ws_fd);
        }

        esp_err_t handle_webmanager_ws(httpd_req_t *req)
        {
            if (req->method == HTTP_GET)
            {
                // Validate session token on WebSocket handshake
                char cookie_buf[256] = {0};
                if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) != ESP_OK ||
                    !validate_session_token(cookie_buf))
                {
                    ESP_LOGW(TAG, "WebSocket: No valid session token");
                    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
                    return ESP_FAIL;
                }
                
                ESP_LOGI(TAG, "WebSocket connection authenticated and opened (fd=%d)", (int)httpd_req_to_sockfd(req));
                return ESP_OK;
            }

            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

            httpd_ws_frame_t ws_pkt = {false, false, HTTPD_WS_TYPE_BINARY, nullptr, 0};

            // always store the last websocket file descriptor
            this->websocket_file_descriptor = httpd_req_to_sockfd(req);

            /* Set max_len = 0 to get the frame len */
            esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
            if (ret != ESP_OK)
            {
                // Bad/malformed websocket frames can happen during disconnect/AP transitions.
                // Never abort the device from this callback.
                if (ret == ESP_ERR_INVALID_STATE)
                {
                    ESP_LOGW(TAG, "Ignoring websocket frame in invalid state (%d)", ret);
                    return ESP_OK;
                }
                ESP_LOGW(TAG, "httpd_ws_recv_frame(header) failed with %d", ret);
                return ret;
            }
            if (ws_pkt.len == 0 || ws_pkt.type != HTTPD_WS_TYPE_BINARY)
            {
                ESP_LOGE(TAG, "Received an empty or an non binary websocket frame");
                return ESP_OK;
            }
            uint8_t *buf = new uint8_t[ws_pkt.len];
            assert(buf);
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
                delete[] buf;
                return ret;
            }
            if (ws_pkt.len < 4)
            {
                ESP_LOGW(TAG, "Ignoring short websocket binary frame (len=%u)", (unsigned)ws_pkt.len);
                delete[] buf;
                return ESP_OK;
            }
            uint16_t namespaceId = (uint16_t)(buf[0] | (buf[1] << 8));
            uint16_t messageTypeId = (uint16_t)(buf[2] | (buf[3] << 8));
            eMessageReceiverResult success = ProvideWebsocketMessage(this, req, &ws_pkt, namespaceId, messageTypeId, buf, ws_pkt.len);
            if (success == eMessageReceiverResult::NOT_FOR_ME && plugins)
            {
                for (auto p : *plugins)
                {
                    success = p->ProvideWebsocketMessage(this, req, &ws_pkt, namespaceId, messageTypeId, buf, ws_pkt.len);
                    if (success != eMessageReceiverResult::NOT_FOR_ME)
                    {
                        break;
                    }
                }
            }
            if (success == eMessageReceiverResult::NOT_FOR_ME)
            {
                ESP_LOGW(TAG, "Not yet implemented request for namespace %u, neither internal nor in a plugin", (unsigned)namespaceId);
            }
            else if (success == eMessageReceiverResult::FOR_ME_BUT_FAILED)
            {
                ESP_LOGW(TAG, "Request for namespace %u has been implemented by plugin, but processing failed", (unsigned)namespaceId);
            }
            delete[] buf;
            return ESP_OK;
        }

        // wifimanager wird hier direkt (nicht ueber den generischen 'plugins'-Vektor) behandelt,
        // weil es eng mit der WLAN-State-Machine dieser Klasse verzahnt ist -- funktional aber ein
        // regulaerer iWebmanagerPlugin-Aufruf wie jeder andere Namespace, kein Sonderfall im
        // Dispatcher mehr (anders als vorher).
        eMessageReceiverResult ProvideWebsocketMessage(iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen)
        {
            if (namespaceId != WsProtocol::wifimanager::NAMESPACE_ID)
                return eMessageReceiverResult::NOT_FOR_ME;
            switch (messageTypeId)
            {
            case WsProtocol::wifimanager::RequestNetworkInformation::TYPE_ID:
                return sendResponseNetworkInformation(frame, frameLen);
            case WsProtocol::wifimanager::RequestWifiConnect::TYPE_ID:
                return handleRequestWifiConnect(frame, frameLen);
            case WsProtocol::wifimanager::RequestWifiDisconnect::TYPE_ID:
                return handleRequestWifiDisconnect(frame, frameLen);
            default:
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;
            }
        }

        eMessageReceiverResult handleRequestWifiConnect(const uint8_t *frame, size_t frameLen)
        {
            WsProtocol::wifimanager::RequestWifiConnect::Payload req{};
            if (!WsProtocol::wifimanager::RequestWifiConnect::Decode(frame, frameLen, req))
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;
            lastWifiConnectRequestId = req.requestId;

            [[maybe_unused]] esp_err_t ret{ESP_OK}; // von ESP_GOTO_ON_FALSE unten benoetigt (schreibt "ret = err_code"), Wert wird aber nie gelesen
            time_t now_us{0};
            size_t len{0};
            len = strlen(req.ssid);
            ESP_GOTO_ON_FALSE(len <= MAX_SSID_LEN - 1, ESP_FAIL, negativeresponse, TAG, "SSID too long");
            len = strlen(req.password);
            ESP_GOTO_ON_FALSE(len <= MAX_PASSPHRASE_LEN - 1, ESP_FAIL, negativeresponse, TAG, "PASSPHRASE too long");
            ESP_GOTO_ON_FALSE(len > 0, ESP_FAIL, negativeresponse, TAG, "no PASSPHRASE given");
            strncpy((char *)wifi_config_sta.sta.ssid, req.ssid, MAX_SSID_LEN - 1);
            strncpy((char *)wifi_config_sta.sta.password, req.password, MAX_PASSPHRASE_LEN - 1 );
            wifi_config_sta.sta.ssid[MAX_SSID_LEN - 1] = '\0';
            wifi_config_sta.sta.password[MAX_PASSPHRASE_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Got a new ssid '%s' and password '%s' from browser.", wifi_config_sta.sta.ssid, wifi_config_sta.sta.password);
            if (!xSemaphoreTake(webmanager_semaphore, portMAX_DELAY))
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;
            now_us = esp_timer_get_time();
            giveUpAt_us = now_us + apFallbackTimeout_us;
            connectAsSTA(now_us);
            xSemaphoreGive(webmanager_semaphore);
            return eMessageReceiverResult::OK;
        negativeresponse:
            {
                WsProtocol::wifimanager::ResponseWifiConnect::Payload resp{};
                resp.requestId = req.requestId;
                resp.success = false;
                resp.ssid = (const char*)wifi_config_sta.sta.ssid;
                resp.ip = 0;
                resp.netmask = 0;
                resp.gateway = 0;
                resp.rssi = 0;
                uint8_t buf[256];
                size_t n = WsProtocol::wifimanager::ResponseWifiConnect::Encode(resp, buf, sizeof(buf));
                return (n > 0 && SendRawAsync(buf, n) == ESP_OK) ? eMessageReceiverResult::OK : eMessageReceiverResult::FOR_ME_BUT_FAILED;
            }
        }

        eMessageReceiverResult handleRequestWifiDisconnect(const uint8_t *frame, size_t frameLen)
        {
            WsProtocol::wifimanager::RequestWifiDisconnect::Payload req{};
            if (!WsProtocol::wifimanager::RequestWifiDisconnect::Decode(frame, frameLen, req))
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;

            WsProtocol::wifimanager::ResponseWifiDisconnect::Payload resp{};
            resp.requestId = req.requestId;
            uint8_t buf[64];
            size_t len = WsProtocol::wifimanager::ResponseWifiDisconnect::Encode(resp, buf, sizeof(buf));
            if (len > 0) SendRawAsync(buf, len);
            vTaskDelay(pdMS_TO_TICKS(2000)); // warte 2s, um die Beantwortung des Requests noch zu ermöglichen

            if (!xSemaphoreTake(webmanager_semaphore, portMAX_DELAY))
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            delete_sta_config();
            ESP_LOGI(TAG, "Disconnected as STA from ssid '%s'.", wifi_config_sta.sta.ssid);
            configureAndOpenAccessPointAndSetStatus();
            xSemaphoreGive(webmanager_semaphore);
            return eMessageReceiverResult::OK;
        }

        eMessageReceiverResult sendResponseNetworkInformation(const uint8_t *frame, size_t frameLen)
        {
            WsProtocol::wifimanager::RequestNetworkInformation::Payload req{};
            if (!WsProtocol::wifimanager::RequestNetworkInformation::Decode(frame, frameLen, req))
                return eMessageReceiverResult::FOR_ME_BUT_FAILED;
            //bool forceUpdate = req.forceNewSearch;
            ESP_LOGI(TAG, "Prepare to send ResponseNetworkInformation");
            esp_err_t ret{ESP_OK};

            wifi_ap_record_t *ap{nullptr};
            wifi_ap_record_t my_ap={};
            esp_netif_ip_info_t ap_ip_info = {};
            esp_netif_ip_info_t sta_ip_info = {};
            wifi_ap_record_t accessp_records[MAX_AP_NUM];
            uint16_t accessp_records_len = MAX_AP_NUM;

            // Grosszuegig bemessen: bis zu MAX_AP_NUM Elemente, je [classId:u16][ssid<=32+null]
            // [primaryChannel:i32][rssi:i32][authMode:i32].
            uint8_t ap_scratch[MAX_AP_NUM * 64];
            size_t ap_scratch_pos = 0;
            size_t ap_appended = 0;

            //if (!xSemaphoreTake(webmanager_semaphore, portMAX_DELAY)) return ESP_ERR_INVALID_STATE;

            GOTO_ERROR_ON_ERROR(esp_wifi_scan_start(nullptr, true), "Wifi Scan did NOT complete successfully.");
            GOTO_ERROR_ON_ERROR(esp_wifi_scan_get_ap_records(&accessp_records_len, accessp_records), "Could not get access point list");
            ESP_LOGI(TAG, "Wifi Scan successfully completed. Found %d access points.", accessp_records_len);


            for (size_t i = 0; i < accessp_records_len; i++)
            {
                ap = accessp_records + i;
                WsProtocol::wifimanager::AccessPoint::Payload item{};
                item.ssid = (const char*)ap->ssid;
                item.primaryChannel = ap->primary;
                item.rssi = ap->rssi;
                item.authMode = (int)ap->authmode;
                size_t newPos = WsProtocol::wifimanager::AppendResponseNetworkInformationAccesspointsAccessPointElement(item, ap_scratch, ap_scratch_pos, sizeof(ap_scratch));
                if (newPos == 0) break; // Scratch-Puffer voll -- restliche APs auslassen statt abzustuerzen
                ap_scratch_pos = newPos;
                ap_appended++;
                ESP_LOGI(TAG, "  AP %25s; %4d", (char *)ap->ssid, ap->rssi);
            }

            ESP_ERROR_CHECK(esp_netif_get_ip_info(wifi_netif_ap, &ap_ip_info));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(wifi_netif_sta, &sta_ip_info));
            esp_wifi_sta_get_ap_info(&my_ap);
        error:
            //xSemaphoreGive(webmanager_semaphore);
            {
                WsProtocol::wifimanager::ResponseNetworkInformation::Payload resp{};
                resp.requestId = req.requestId;
                resp.hostname = hostname;
                resp.ssidAp = (const char*)wifi_config_ap.ap.ssid;
                resp.passwordAp = (const char*)wifi_config_ap.ap.password;
                resp.ipAp = ap_ip_info.ip.addr;
                resp.isConnectedSta = this->staConnectionState;
                resp.ssidSta = (const char*)wifi_config_sta.sta.ssid;
                resp.ipSta = sta_ip_info.ip.addr;
                resp.netmaskSta = sta_ip_info.netmask.addr;
                resp.gatewaySta = sta_ip_info.gw.addr;
                resp.rssiSta = my_ap.rssi;
                resp.accesspointsData = ap_scratch;
                resp.accesspointsCount = ap_appended;
                resp.accesspointsDataSize = ap_scratch_pos;

                uint8_t buf[1536];
                size_t len = WsProtocol::wifimanager::ResponseNetworkInformation::Encode(resp, buf, sizeof(buf));
                ret = (len > 0 && SendRawAsync(buf, len) == ESP_OK) ? ESP_OK : ESP_FAIL;
            }
            return ret == ESP_OK ? eMessageReceiverResult::OK : eMessageReceiverResult::FOR_ME_BUT_FAILED;
        }

        esp_err_t handle_ota_post(httpd_req_t *req)
        {
            ESP_LOGI(TAG, "in handle_ota_post");
            char buf[1024];
            esp_ota_handle_t ota_handle;
            size_t remaining = req->content_len;

            const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
            ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

            while (remaining > 0)
            {
                int recv_len = httpd_req_recv(req, buf, std::min(remaining, (size_t)sizeof(buf)));
                if (recv_len <= 0)
                {
                    // Serious Error: Abort OTA
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
                    return ESP_FAIL;
                }
                if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
                {
                    // Timeout Error: Just retry
                    continue;
                }
                if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK)
                {
                    // Successful Upload: Flash firmware chunk
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
                    return ESP_FAIL;
                }

                remaining -= recv_len;
            }

            // Validate and switch to new OTA image and reboot
            if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
                return ESP_FAIL;
            }

            httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

            vTaskDelay(500 / portTICK_PERIOD_MS);
            esp_restart();

            return ESP_OK;
        }

        esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
        {
            struct dirent *entry;
            DIR *dir = opendir(dirpath);
            if (!dir)
            {
                ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
                return ESP_FAIL;
            }
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr_chunk(req, "{'files':[");
            while ((entry = readdir(dir)) != nullptr)
            {
                if (entry->d_type == DT_DIR)
                    continue;
                httpd_resp_sendstr_chunk(req, "'");
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "',");
            }
            closedir(dir);
            dir = opendir(dirpath);

            httpd_resp_sendstr_chunk(req, "], 'dirs':[");
            while ((entry = readdir(dir)) != nullptr)
            {
                if (entry->d_type != DT_DIR)
                    continue;
                httpd_resp_sendstr_chunk(req, "'");
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "',");
            }
            closedir(dir);
            httpd_resp_sendstr_chunk(req, "]}");
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_OK;
        }

        esp_err_t handle_files_get(httpd_req_t *req)
        {
            FILE *fd = nullptr;
            struct stat file_stat;

            const char *path = req->uri + FILES_BASE_PATH_LEN;
            ESP_LOGI(TAG, "Got GET files for filename %s ", path);

            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

            /* If name has trailing '/', respond with directory contents */
            if (path[strlen(path) - 1] == '/')
            {
                return http_resp_dir_html(req, path);
            }

            if (stat(path, &file_stat) == -1)
            {
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
                return ESP_FAIL;
            }

            fd = fopen(path, "r");
            if (!fd)
            {
                ESP_LOGE(TAG, "Failed to read existing file : %s", path);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", path, file_stat.st_size);

            size_t chunksize;
            do
            {
                /* Read file in chunks into the scratch buffer */
                chunksize = fread(http_buffer, 1, HTTP_BUFFER_SIZE, fd);

                if (chunksize > 0)
                {
                    if (httpd_resp_send_chunk(req, (const char *)http_buffer, chunksize) != ESP_OK)
                    {
                        fclose(fd);
                        ESP_LOGE(TAG, "File sending failed!");
                        httpd_resp_sendstr_chunk(req, nullptr);
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                        return ESP_FAIL;
                    }
                }
            } while (chunksize != 0);

            fclose(fd);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_OK;
        }

        esp_err_t handle_files_post(httpd_req_t *req)
        {
            FILE *fd = NULL;
            struct stat file_stat;

            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

            const char *path = req->uri + FILES_BASE_PATH_LEN;
            ESP_LOGI(TAG, "Got POST files for filename %s ", path);

            if (path[strlen(path) - 1] == '/')
            {
                ESP_LOGE(TAG, "We need a filename, not a directory name : %s", path);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "We need a filename, not a directory name!");
                return ESP_FAIL;
            }

            if (false && stat(path, &file_stat) == 0)
            {
                // Files should be overwritten, hence "false &&"
                ESP_LOGE(TAG, "File already exists : %s", path);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
                return ESP_FAIL;
            }

            if (req->content_len > MAX_FILE_SIZE)
            {
                ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large");
                return ESP_FAIL;
            }

            fd = fopen(path, "w");
            if (!fd)
            {
                ESP_LOGE(TAG, "Failed to create file : %s", path);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "Receiving file : %s...", path);
            size_t received;
            size_t remaining = req->content_len;

            while (remaining > 0)
            {

                ESP_LOGI(TAG, "Remaining size : %d", remaining);
                /* Receive the file part by part into a buffer */
                if ((received = httpd_req_recv(req, (char *)http_buffer, std::min(remaining, HTTP_BUFFER_SIZE))) <= 0)
                {
                    if (received == HTTPD_SOCK_ERR_TIMEOUT)
                        continue;
                    /* In case of unrecoverable error,
                     * close and delete the unfinished file*/
                    fclose(fd);
                    unlink(path);
                    ESP_LOGE(TAG, "File reception failed!");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                    return ESP_FAIL;
                }

                /* Write buffer content to file on storage */
                if (received && (received != fwrite(http_buffer, 1, received, fd)))
                {
                    /* Couldn't write everything to file!
                     * Storage may be full? */
                    fclose(fd);
                    unlink(path);

                    ESP_LOGE(TAG, "File write failed!");
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
                    return ESP_FAIL;
                }
                remaining -= received;
            }

            fclose(fd);
            if (stat(path, &file_stat) != 0)
            {
                ESP_LOGE(TAG, "File stat was not possible. write failed!");
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File stat was not possible. write failed!");
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "File reception for %s complete. File has %ldbytes", path, file_stat.st_size);
            httpd_resp_sendstr(req, "File uploaded successfully");
            return ESP_OK;
        }

        esp_err_t handle_files_delete(httpd_req_t *req)
        {
            struct stat file_stat;
            const char *path = req->uri + FILES_BASE_PATH_LEN;
            ESP_LOGI(TAG, "Got DELETE files for filename %s ", path);

            /* Filename cannot have a trailing '/' */
            if (path[strlen(path) - 1] == '/')
            {
                ESP_LOGE(TAG, "We need a filename, not a directory name : %s", path);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "We need a filename, not a directory name!");
                return ESP_FAIL;
            }

            if (stat(path, &file_stat) == -1)
            {
                ESP_LOGE(TAG, "File does not exist : %s", path);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "Deleting file : %s", path);
            unlink(path);
            httpd_resp_sendstr(req, "File deleted successfully");
            return ESP_OK;
        }

        // Konstantzeitiger Vergleich (Laenge zuerst, dann XOR-Akkumulation ohne Early-Exit) --
        // ersetzt den vormaligen std::string==-Vergleich, der bei einem Zeichen-Mismatch frueh
        // abbricht und damit ein Timing-Seitenkanal ist. Kein externes Krypto-Lib noetig.
        static bool constant_time_equals(const std::string &a, const std::string &b)
        {
            if (a.size() != b.size()) return false;
            unsigned char diff = 0;
            for (size_t i = 0; i < a.size(); i++) diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
            return diff == 0;
        }

        bool validate_credentials(const char *username, const char *password)
        {
            if (!username || !password) return false;
            return constant_time_equals(auth_username, username) && constant_time_equals(auth_password, password);
        }

        // Dekodiert application/x-www-form-urlencoded-Text in-place (Standard-Kodierung eines
        // <form method='POST'>-Submits ohne enctype-Angabe): '+' -> Leerzeichen, '%XX' -> Byte XX.
        // Fehlte bisher komplett in handle_login_post() -- Benutzername/Passwort mit Sonderzeichen
        // (Leerzeichen, '&', '=', Nicht-ASCII wie Umlaute) kamen dadurch percent-kodiert im
        // Rohtext an und matchten nie gegen den echten (dekodierten) gespeicherten Wert. Reine
        // ASCII-Buchstaben/Ziffern sind vom Encoding nicht betroffen, das hat den Bug lange
        // verdeckt (s. Log in handle_login_post).
        static void url_decode(char *s)
        {
            char *dst = s;
            while (*s)
            {
                if (*s == '+')
                {
                    *dst++ = ' ';
                    s++;
                }
                else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2]))
                {
                    char hex[3] = {s[1], s[2], 0};
                    *dst++ = (char)strtol(hex, nullptr, 16);
                    s += 3;
                }
                else
                {
                    *dst++ = *s++;
                }
            }
            *dst = '\0';
        }

        std::string generate_random_token()
        {
            char token[33];
            uint8_t random_bytes[16];
            esp_fill_random(random_bytes, sizeof(random_bytes));
            
            for (int i = 0; i < 16; i++) {
                snprintf(&token[i*2], 3, "%02x", random_bytes[i]);
            }
            token[32] = '\0';
            return std::string(token);
        }

        bool create_session(const char *username)
        {
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            session_token = generate_random_token();
            session_expiry_us = esp_timer_get_time() + SESSION_TIMEOUT_US;
            xSemaphoreGive(webmanager_semaphore);
            ESP_LOGI(TAG, "Session created for user '%s', token expires in 1 hour", username);
            return true;
        }

        bool validate_session_token(const char *cookie_header)
        {
            if (!cookie_header) return false;
            
            char token_buf[33] = {0};
            const char *session_cookie = strstr(cookie_header, "session=");
            if (!session_cookie) return false;
            
            session_cookie += 8; // strlen("session=")
            sscanf(session_cookie, "%32s", token_buf);
            
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            time_t now = esp_timer_get_time();
            bool valid = (!session_token.empty() && 
                         session_token == token_buf && 
                         now < session_expiry_us);
            xSemaphoreGive(webmanager_semaphore);
            
            return valid;
        }

        void invalidate_session()
        {
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            session_token.clear();
            session_expiry_us = 0;
            xSemaphoreGive(webmanager_semaphore);
            ESP_LOGI(TAG, "Session invalidated");
        }

        // Client kann das "session"-Cookie NICHT selbst per document.cookie loeschen, weil es
        // HttpOnly gesetzt ist (bewusst, s. handle_login_post -- schuetzt vor Diebstahl per XSS) --
        // das war bislang der einzige Ort, an dem "abgemeldet" versucht wurde (rein clientseitig,
        // s. app_controller.ts), wirkungslos: das Cookie blieb gueltig, ein Reload auf "/" zeigte
        // wieder die (weiterhin authentifizierte) SPA statt der Login-Maske. Einziger Weg, ein
        // HttpOnly-Cookie zu loeschen: der Server selbst schickt ein neues Set-Cookie mit
        // abgelaufenem Datum.
        esp_err_t handle_logout_post(httpd_req_t *req)
        {
            ESP_LOGI(TAG, "Logout requested");
            invalidate_session();
            httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; HttpOnly; SameSite=Strict; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
            httpd_resp_set_hdr(req, "Set-Cookie", "username=; Path=/; SameSite=Strict; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
            httpd_resp_set_status(req, "303 See Other");
            httpd_resp_set_hdr(req, "Location", "/");
            httpd_resp_sendstr(req, "");
            return ESP_OK;
        }

        esp_err_t handle_login_form(httpd_req_t *req)
        {
            const char *html = 
                "<!DOCTYPE html>"
                "<html lang='de'>"
                "<head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>Webmanager Login</title>"
                "<link href='https://fonts.googleapis.com/css?family=Dosis:400,700' rel='stylesheet'>"
                "<style>"
                ":root { --blue-rich: #0066cc; --blue-4: hsl(211, 39%, 44%); --main-white: #f2f2f2; }"
                "*{margin:0;padding:0;box-sizing:border-box;}"
                "body { font-family: 'Dosis', sans-serif; display: flex; justify-content: center; align-items: center; min-height: 100vh; background: linear-gradient(135deg, var(--blue-4) 0%, var(--blue-rich) 100%); padding: 20px; }"
                ".login-container { background: var(--main-white); padding: 3rem 2rem; border-radius: 8px; box-shadow: 0 10px 40px rgba(0,0,0,0.2); width: 100%; max-width: 420px; }"
                "h1 { text-align: center; color: var(--blue-rich); margin-bottom: 2rem; font-size: 28px; font-weight: 700; }"
                ".form-group { margin-bottom: 1.5rem; }"
                "label { display: block; margin-bottom: 0.6rem; color: #333; font-weight: 500; font-size: 14px; }"
                "input[type='text'], input[type='password'] { width: 100%; padding: 0.75rem 1rem; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; font-family: 'Dosis', sans-serif; transition: border-color 0.2s ease-out, box-shadow 0.2s ease-out; }"
                "input[type='text']:focus, input[type='password']:focus { outline: none; border-color: var(--blue-rich); box-shadow: 0 0 5px rgba(0, 102, 204, 0.3); }"
                "button { width: 100%; padding: 0.75rem; background: var(--blue-rich); color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: 700; font-family: 'Dosis', sans-serif; cursor: pointer; transition: background-color 0.2s ease-out, transform 0.1s ease-out; }"
                "button:hover { background: var(--blue-4); }"
                "button:active { transform: scale(0.98); }"
                ".error { color: #dc3545; text-align: center; margin-bottom: 1rem; font-weight: 500; }"
                "@media (max-width: 480px) { .login-container { padding: 2rem 1.5rem; } h1 { font-size: 24px; } input { font-size: 16px; } }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='login-container'>"
                "<h1>Webmanager Login</h1>"
                "<form method='POST' action='/login'>"
                "<div class='form-group'>"
                "<label for='username'>Benutzername:</label>"
                "<input type='text' id='username' name='username' autocomplete='username' required autofocus>"
                "</div>"
                "<div class='form-group'>"
                "<label for='password'>Kennwort:</label>"
                "<input type='password' id='password' name='password' autocomplete='current-password' required>"
                "</div>"
                "<button type='submit'>Anmelden</button>"
                "</form>"
                "</div>"
                "</body>"
                "</html>";
            
            httpd_resp_set_type(req, "text/html; charset=utf-8");
            httpd_resp_sendstr(req, html);
            return ESP_OK;
        }

        esp_err_t handle_login_post(httpd_req_t *req)
        {
            char buf[256] = {0};
            size_t recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (recv_len <= 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
                return ESP_FAIL;
            }

            char username[64] = {0};
            char password[64] = {0};

            // Parse form data: username=...&password=...
            char *user_ptr = strstr(buf, "username=");
            char *pass_ptr = strstr(buf, "password=");

            if (!user_ptr || !pass_ptr) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing credentials");
                return ESP_FAIL;
            }

            user_ptr += 9; // strlen("username=")
            pass_ptr += 9; // strlen("password=")

            sscanf(user_ptr, "%63[^&]", username);
            sscanf(pass_ptr, "%63[^&]", password);
            url_decode(username);
            url_decode(password);
            ESP_LOGI(TAG, "Login attempt for user '%s' (password length %d after url-decode)", username, (int)strlen(password));

            // Validate credentials
            if (validate_credentials(username, password)) {
                ESP_LOGI(TAG, "Login successful for user '%s'", username);
                create_session(username);
                
                // Set cookies with session token -- zwei separate Set-Cookie-Header (ein Aufruf
                // von httpd_resp_set_hdr PRO Cookie), statt (wie zuvor) beide Cookies in EINEN
                // Header-Wert zu packen, der selbst schon einen literalen "Set-Cookie: "-Praefix
                // und ein eingebettetes "\r\n" enthielt -- das war kein valides HTTP (ein
                // Header-Wert darf keinen zweiten Header-Namen + Zeilenumbruch enthalten).
                char session_cookie[128];
                snprintf(session_cookie, sizeof(session_cookie),
                    "session=%s; Path=/; HttpOnly; SameSite=Strict", session_token.c_str());
                httpd_resp_set_hdr(req, "Set-Cookie", session_cookie);
                char username_cookie[128];
                snprintf(username_cookie, sizeof(username_cookie),
                    "username=%s; Path=/; SameSite=Strict", username);
                httpd_resp_set_hdr(req, "Set-Cookie", username_cookie);
                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", "/");
                httpd_resp_sendstr(req, "");
                return ESP_OK;
            }

            ESP_LOGW(TAG, "Login failed for user '%s'", username);
            httpd_resp_set_type(req, "text/html; charset=utf-8");
            httpd_resp_sendstr(req, 
                "<!DOCTYPE html>"
                "<html lang='de'>"
                "<head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<link href='https://fonts.googleapis.com/css?family=Dosis:400,700' rel='stylesheet'>"
                "<style>"
                ":root { --blue-rich: #0066cc; --blue-4: hsl(211, 39%, 44%); --main-white: #f2f2f2; }"
                "*{margin:0;padding:0;box-sizing:border-box;}"
                "body { font-family: 'Dosis', sans-serif; display: flex; justify-content: center; align-items: center; min-height: 100vh; background: linear-gradient(135deg, var(--blue-4) 0%, var(--blue-rich) 100%); padding: 20px; }"
                ".error-container { background: var(--main-white); padding: 3rem 2rem; border-radius: 8px; box-shadow: 0 10px 40px rgba(0,0,0,0.2); width: 100%; max-width: 420px; text-align: center; }"
                "h2 { color: #dc3545; margin-bottom: 1rem; font-size: 24px; font-weight: 700; }"
                "p { color: #666; margin-bottom: 2rem; font-size: 14px; }"
                "a { text-decoration: none; }"
                "button { padding: 0.75rem 2rem; background: var(--blue-rich); color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: 700; font-family: 'Dosis', sans-serif; cursor: pointer; transition: background-color 0.2s ease-out; }"
                "button:hover { background: var(--blue-4); }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='error-container'>"
                "<h2>Authentifizierung fehlgeschlagen</h2>"
                "<p>Benutzername oder Kennwort ist ungültig.</p>"
                "<a href='/'><button>Zurück zum Login</button></a>"
                "</div>"
                "</body>"
                "</html>");
            return ESP_OK;
        }

        esp_err_t handle_webmanager_get(httpd_req_t *req)
        {
            // Check for valid session cookie
            char cookie_buf[256] = {0};
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK)
            {
                if (validate_session_token(cookie_buf))
                {
                    ESP_LOGI(TAG, "User authenticated via session token");
                    httpd_resp_set_type(req, "text/html");
                    httpd_resp_set_hdr(req, "Content-Encoding", "br");
                    httpd_resp_send(req, webmanager_html_br_start, webmanager_html_br_length);
                    return ESP_OK;
                }
            }
            
            // No valid session: show login form
            ESP_LOGI(TAG, "Showing login form (no valid session)");
            return handle_login_form(req);
        }

    public:
        static M *GetSingleton()
        {
            if (!singleton)
            {
                singleton = new M();
            }
            return singleton;
        }

        bool GetStaState()
        {
            return this->staConnectionState;
        }

        const char *GetHostname()
        {
            esp_netif_get_hostname(this->wifi_netif_sta, &hostname);
            return hostname;
        }

        esp_ip4_addr_t GetIpAddress()
        {  
            esp_netif_ip_info_t  ip={};
            esp_netif_get_ip_info(this->wifi_netif_sta, &ip);
            return ip.ip;
        }

        const char *GetSsid()
        {
            return (const char *)this->wifi_config_sta.sta.ssid;
        }

        bool HasRealtime()
        {
            struct timeval tv_now;
            gettimeofday(&tv_now, nullptr);
            time_t seconds_epoch = tv_now.tv_sec;
            return seconds_epoch > 1684412222; // epoch time when this code has been written
        }

        esp_err_t SendRawAsync(const uint8_t* data, size_t len) override
        {
            if (!http_server)
                return ESP_FAIL;
            if (websocket_file_descriptor == -1)
            {
                ESP_LOGD(TAG, "SendRawAsync: no active websocket connection (fd==-1), dropping %d bytes", (int)len);
                return ESP_ERR_INVALID_STATE;
            }
            auto *a = new AsyncResponse(data, len);
            esp_err_t ret = httpd_queue_work(http_server, M::ws_async_send, a);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "SendRawAsync: httpd_queue_work failed with %s (fd=%d)", esp_err_to_name(ret), (int)websocket_file_descriptor);
                delete (a);
                if (ret == ESP_ERR_INVALID_ARG || ret == ESP_FAIL)
                {
                    websocket_file_descriptor = -1;
                }
            }
            return ret;
        }

        void RegisterHTTPDHandlers(httpd_handle_t httpd_handle)
        {
            httpd_uri_t files_get = {
                FILES_GLOB,
                HTTP_GET,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_files_get(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &files_get));

            httpd_uri_t files_post = {
                FILES_GLOB,
                HTTP_POST,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_files_post(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &files_post));

            httpd_uri_t files_delete = {
                FILES_GLOB,
                HTTP_DELETE,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_files_delete(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &files_delete));

            httpd_uri_t ota_post = {
                "/ota",
                HTTP_POST,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_ota_post(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &ota_post));
            
            httpd_uri_t login_post = {
                "/login",
                HTTP_POST,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_login_post(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &login_post));

            httpd_uri_t logout_post = {
                "/logout",
                HTTP_POST,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_logout_post(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &logout_post));

            httpd_uri_t webmanager_ws = {
                "/webmanager_ws",
                HTTP_GET,
                [](httpd_req_t *req)
                { return static_cast<webmanager::M *>(req->user_ctx)->handle_webmanager_ws(req); }, this, true, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &webmanager_ws));
            
            httpd_uri_t webmanager_get = {
                "/*", HTTP_GET,
                [](httpd_req_t *req)
                { return static_cast<M *>(req->user_ctx)->handle_webmanager_get(req); },
                this, false, false, nullptr};
            ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &webmanager_get));
            this->http_server = httpd_handle;
        }



        esp_err_t Begin(const char *accessPointSsid, const char *accessPointPassword, const char *hostname, bool resetStoredWifiConnection, std::vector<iWebmanagerPlugin *> *plugins, bool init_netif_and_create_event_loop = true, bool startOwnSupervisorTask=true, esp_log_level_t wifiLogLevel=ESP_LOG_WARN, const char *auth_username_param="admin", const char *auth_password_param="password", time_t apFallbackTimeout_us_param=FAR_FUTURE)
        {
            ESP_LOGI(TAG, "Stating Webmanager");
            this->apFallbackTimeout_us = apFallbackTimeout_us_param;

            this->hostname=hostname;
            this->auth_username=auth_username_param;
            this->auth_password=auth_password_param;
            
            if (strlen(accessPointPassword) < 8 && AP_AUTHMODE != WIFI_AUTH_OPEN){
                ESP_LOGE(TAG, "Password too short for authentication. Minimal length is 8. Exiting Webmanager");
                return ESP_FAIL;
            }

            if (webmanager_semaphore != nullptr){
                ESP_LOGE(TAG, "webmanager already started. Exiting 'Begin'-method");
                return ESP_FAIL;
            }
            
            webmanager_semaphore = xSemaphoreCreateBinary();
            xSemaphoreGive(webmanager_semaphore);

            if (init_netif_and_create_event_loop)
            {
                ESP_ERROR_CHECK(esp_netif_init());
                ESP_ERROR_CHECK(esp_event_loop_create_default());
            }

            this->plugins = plugins;

            // Create and check netifs
            wifi_netif_sta = esp_netif_create_default_wifi_sta();
            wifi_netif_ap = esp_netif_create_default_wifi_ap();
            assert(wifi_netif_sta);
            assert(wifi_netif_ap);

            // attach event handler for wifi & ip
            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
                                                                { static_cast<webmanager::M *>(arg)->wifi_event_handler(event_base, event_id, event_data); }, this, nullptr));
            ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, [](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
                                                                { static_cast<webmanager::M *>(arg)->ip_event_handler(event_base, event_id, event_data); }, this, nullptr));

            // init WIFI base
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));
            ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

            // Prepare WIFI_CONFIG for sta mode
            wifi_config_sta.sta.scan_method = WIFI_FAST_SCAN;
            wifi_config_sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            wifi_config_sta.sta.threshold.rssi = -127;
            wifi_config_sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            wifi_config_sta.sta.pmf_cfg.capable = true;
            wifi_config_sta.sta.pmf_cfg.required = false;

            wifi_config_ap.ap.channel = 0;
            wifi_config_ap.ap.max_connection = 1;
            wifi_config_ap.ap.authmode = AP_AUTHMODE;
            std::strcpy((char *)(wifi_config_ap.ap.ssid), accessPointSsid);
            std::strcpy((char *)(wifi_config_ap.ap.password), accessPointPassword);
            

            ESP_ERROR_CHECK(esp_netif_set_hostname(wifi_netif_sta, hostname));
            ESP_ERROR_CHECK(esp_netif_set_hostname(wifi_netif_ap, hostname));

            ESP_ERROR_CHECK(mdns_init());
            ESP_ERROR_CHECK(mdns_hostname_set(hostname));
            const char *MDNS_INSTANCE = "ESP32_MDNS_INSTANCE";
            ESP_ERROR_CHECK(mdns_instance_name_set(MDNS_INSTANCE));

            // set wifi logging 
            esp_log_level_set("wifi", wifiLogLevel);

            // Turn Power Saving off
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

            // SNTP (simple network time protocol) client and start it, when we got an IP address (see event handler)
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_set_time_sync_notification_cb([](struct timeval *tv)
                                                   { webmanager::M::GetSingleton()->sntp_handler(); });
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Germany
            tzset();
            time_t now_us = esp_timer_get_time();
            if (resetStoredWifiConnection)
            {
                ESP_LOGI(TAG, "Forced to delete saved wifi configuration. Starting access point and do an initial scan.");
                delete_sta_config();
                configureAndOpenAccessPointAndSetStatus();
            }
            else if (read_sta_config() != ESP_OK)
            {
                ESP_LOGI(TAG, "Unable to read WIFI SSID or PASSWORD from flash. Starting access point and do an initial scan.");
                configureAndOpenAccessPointAndSetStatus();
            }
            else
            {
                // auf keinen Fall einen AccessPoint aufmachen
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_start());
                giveUpAt_us = now_us + apFallbackTimeout_us;
                connectAsSTA(now_us);
                this->setStatus(WorkingState::KEEP_CONNECTION, now_us + COMMON_TIMEOUT_US);
            }
            ESP_ERROR_CHECK(esp_wifi_start());
            for (const auto &i : *this->plugins)
            {
                i->OnBegin(this);
            }

            ESP_LOGI(TAG, "Webmanager has been succcessfully initialized");

            // Configure and start timer
            if(startOwnSupervisorTask){
                xTaskCreate([](void* arg){((webmanager::M *)(arg))->supervisorTask();}, "wifi_supervisor", 4*4096, this, 12, nullptr);
            }
            return ESP_OK;
        }

        esp_err_t CallMeAfterInitializationToMarkCurrentPartitionAsValid()
        {
            /* Mark current app as valid */
            ESP_LOGI(TAG, "Webmanager marks current Partition as valid");
            const esp_partition_t *partition = esp_ota_get_running_partition();
            esp_ota_img_states_t ota_state;
            if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK)
            {
                if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
                {
                    esp_ota_mark_app_valid_cancel_rollback();
                }
            }
            return ESP_OK;
        }

        void Supervise(){
            xSemaphoreTake(webmanager_semaphore, portMAX_DELAY);
            time_t now_us = esp_timer_get_time();
            ESP_LOGD("WMSV", "timSupervisor_cb {'workingState':'%s', 'tReconnect':%lld, 'tShutdownAp':%lld, 'tTimeout':%lld}",
                ws2c(workingState),
               tReconnect_us/1000,
               tShutdownAp_us/1000,
               tTimeout_us/1000
               );
            if(now_us>tReconnect_us){
                connectAsSTA(now_us);
            }
            if(now_us>tShutdownAp_us){
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if(mode!=WIFI_MODE_STA){
                    close_active_websocket_before_ap_shutdown();
                    esp_wifi_set_mode(WIFI_MODE_STA);
                    ESP_LOGI("WMSV", "Switching off AccessPoint");
                }else{
                    ESP_LOGI("WMSV", "Switching off AccessPoint...but it was already off.");
                }
                setStatus(WorkingState::KEEP_CONNECTION, FAR_FUTURE, FAR_FUTURE, FAR_FUTURE);
            }
            if(now_us>tTimeout_us){
                ESP_LOGW("WMSV", "Unexpected full Timeout in Webmanager while beeing in state %s. Go back to AccessPoint-Mode", ws2c(workingState));
                configureAndOpenAccessPointAndSetStatus();
            }
            xSemaphoreGive(webmanager_semaphore);
        }
    };
}
#undef TAG

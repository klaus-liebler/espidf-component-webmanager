#pragma once
#include "webmanager_base.hh"
#include <esp_wifi.h>

#define TAG "WMAN"

namespace webmanager
{
    // WLAN-Variante des Webmanagers: alles Generische (HTTP(S)-Server, Login/Sessions, SPA,
    // Websocket/Plugins, mDNS, SNTP) steckt in aWebmanagerBase, hier liegt ausschliesslich der
    // WLAN-spezifische Teil (STA-Verbindung, AccessPoint-Fallback, wifimanager-Websocket-
    // Namespace, Supervisor-State-Machine). Die oeffentliche API ist unveraendert.
    class M : public aWebmanagerBase
    {
    private:
        static M *singleton;

        esp_netif_t *wifi_netif_sta{nullptr};
        esp_netif_t *wifi_netif_ap{nullptr};

        wifi_config_t wifi_config_sta = {}; // 132byte
        wifi_config_t wifi_config_ap = {};  // 132byte
        bool fallbackToStoredStaConfig{false};//false means: Fallback is AccessPoint
        //wird auf true gesetzt, wenn eine Sta-Verbindung erfolgreich ist und die config im NVS gespeichert wurde
        //wird auf false gesetzt, wenn der accessPoint gestartet wird

        SemaphoreHandle_t webmanager_semaphore{nullptr}; // stellt sicher, dass die Timer-Aufrufe nicht überlappen können
        TimerHandle_t timSupervisor{nullptr};

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

        // aus Begin() uebernommen, wird erst in StartNetworkStateMachine() ausgewertet
        bool resetStoredWifiConnectionOnStart{false};

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

        M() {}

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

        // Nicht-ETH-IP-Ereignisse (ETH wird generisch in aWebmanagerBase::ip_event_handler behandelt)
        void OnIpEvent(int32_t event_id, void *event_data) override
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
            case IP_EVENT_STA_LOST_IP:
            {
                ESP_LOGD(TAG, "IP_EVENT_STA_LOST_IP");
                break;
            }
            }
            xSemaphoreGive(webmanager_semaphore);
        }

        // wifimanager wird hier direkt (nicht ueber den generischen 'plugins'-Vektor) behandelt,
        // weil es eng mit der WLAN-State-Machine dieser Klasse verzahnt ist -- funktional aber ein
        // regulaerer iWebmanagerPlugin-Aufruf wie jeder andere Namespace, kein Sonderfall im
        // Dispatcher mehr (anders als vorher).
        eMessageReceiverResult ProvideWebsocketMessage(iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen) override
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

        // Einheitliche Statusabfrage ueber einen aWebmanagerBase* (s. Basisklasse) -- identischer
        // Zustand wie GetStaState().
        bool IsNetworkUp() const override
        {
            return this->staConnectionState;
        }

        esp_netif_t *GetPrimaryNetif() override
        {
            return this->wifi_netif_sta;
        }

        const char *GetSsid()
        {
            return (const char *)this->wifi_config_sta.sta.ssid;
        }

        esp_err_t BringUpNetwork() override
        {
            // Create and check netifs
            wifi_netif_sta = esp_netif_create_default_wifi_sta();
            wifi_netif_ap = esp_netif_create_default_wifi_ap();
            assert(wifi_netif_sta);
            assert(wifi_netif_ap);

            // attach event handler for wifi (ip is attached generically by the base class)
            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
                                                                { static_cast<webmanager::M *>(arg)->wifi_event_handler(event_base, event_id, event_data); }, this, nullptr));

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

            // Der Hostname des sta-netif wird generisch von der Basisklasse gesetzt (GetPrimaryNetif())
            ESP_ERROR_CHECK(esp_netif_set_hostname(wifi_netif_ap, hostname));

            // Turn Power Saving off
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
            return ESP_OK;
        }

        void StartNetworkStateMachine() override
        {
            time_t now_us = esp_timer_get_time();
            if (resetStoredWifiConnectionOnStart)
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
        }

        esp_err_t Begin(const char *accessPointSsid, const char *accessPointPassword, const char *hostname, bool resetStoredWifiConnection, std::vector<iWebmanagerPlugin *> *plugins, bool init_netif_and_create_event_loop = true, bool startOwnSupervisorTask=true, esp_log_level_t wifiLogLevel=ESP_LOG_WARN, const char *auth_username_param="admin", const char *auth_password_param="password", time_t apFallbackTimeout_us_param=FAR_FUTURE)
        {
            ESP_LOGI(TAG, "Stating Webmanager");
            this->apFallbackTimeout_us = apFallbackTimeout_us_param;
            this->resetStoredWifiConnectionOnStart = resetStoredWifiConnection;

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

            std::strcpy((char *)(wifi_config_ap.ap.ssid), accessPointSsid);
            std::strcpy((char *)(wifi_config_ap.ap.password), accessPointPassword);

            // set wifi logging
            esp_log_level_set("wifi", wifiLogLevel);

            esp_err_t ret = BeginBase(hostname, plugins, init_netif_and_create_event_loop, auth_username_param, auth_password_param);
            if (ret != ESP_OK)
            {
                return ret;
            }

            ESP_LOGI(TAG, "Webmanager has been succcessfully initialized");

            // Configure and start timer
            if(startOwnSupervisorTask){
                xTaskCreate([](void* arg){((webmanager::M *)(arg))->supervisorTask();}, "wifi_supervisor", 4*4096, this, 12, nullptr);
            }
            return ESP_OK;
        }

        void Supervise() override {
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

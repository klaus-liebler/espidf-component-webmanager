#pragma once
#include "webmanager_base.hh"

#define TAG "WMANE"

namespace webmanager_eth_only
{
    // Ethernet-only-Variante des Webmanagers: identischer Funktionsumfang wie webmanager::M,
    // aber komplett ohne WLAN (kein esp_wifi_*, keine STA/AP-Netifs, kein AccessPoint-Fallback,
    // kein Supervisor-Task, kein wifimanager-Websocket-Namespace).
    //
    // ERWARTUNG: Das Board hat Ethernet BEREITS vor dem Aufruf von Begin() hochgefahren (z.B. per
    // ETHERNET::initETH_W5500(...) in der HAL), inkl. esp_netif_init() und Default-Event-Loop.
    // Diese Klasse sucht lediglich das bestehende ETH-netif ("ETH_DEF") und setzt dessen Hostnamen.
    class M : public webmanager::aWebmanagerBase
    {
    private:
        static inline M *singleton{nullptr};
        esp_netif_t *eth_netif{nullptr};

        M() {}

    public:
        static M *GetSingleton()
        {
            if (!singleton)
            {
                singleton = new M();
            }
            return singleton;
        }

        esp_err_t BringUpNetwork() override
        {
            eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
            if (!eth_netif)
            {
                ESP_LOGE(TAG, "No ethernet netif with key 'ETH_DEF' found. The board HAL has to bring up ethernet BEFORE calling Begin().");
                return ESP_ERR_INVALID_STATE;
            }
            ESP_LOGI(TAG, "Found existing ethernet netif 'ETH_DEF'");
            return ESP_OK;
        }

        esp_netif_t *GetPrimaryNetif() override
        {
            return this->eth_netif;
        }

        bool IsNetworkUp() const override
        {
            return this->eth_netif != nullptr && esp_netif_is_netif_up(this->eth_netif);
        }

        esp_err_t Begin(const char *hostname, std::vector<webmanager::iWebmanagerPlugin *> *plugins, bool init_netif_and_create_event_loop = false, const char *auth_username_param = "admin", const char *auth_password_param = "password")
        {
            ESP_LOGI(TAG, "Starting Webmanager (ethernet only)");
            esp_err_t ret = BeginBase(hostname, plugins, init_netif_and_create_event_loop, auth_username_param, auth_password_param);
            if (ret != ESP_OK)
            {
                return ret;
            }
            ESP_LOGI(TAG, "Webmanager (ethernet only) has been succcessfully initialized");
            return ESP_OK;
        }
    };
}
#undef TAG

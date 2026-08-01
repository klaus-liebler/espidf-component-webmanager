#pragma once

#include "webmanager_interfaces.hh"
#include "wsprotocol_cpp/ws_protocol.hh"
#include <driver/temperature_sensor.h>
#define TAG "SYSINFO"

class SystemInfoPlugin : public webmanager::iWebmanagerPlugin
{
private:
    temperature_sensor_handle_t tempHandle{nullptr};

    static WsProtocol::systeminfo::Mac6 ReadMac(esp_mac_type_t type)
    {
        WsProtocol::systeminfo::Mac6 mac{};
        esp_read_mac(mac.v, type);
        return mac;
    }

    webmanager::eMessageReceiverResult sendResponseSystemData(webmanager::iWebmanagerCallback *callback, uint16_t requestId)
    {
        ESP_LOGI(TAG, "Prepare to send ResponseSystemData");
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);

        // Grosszuegig bemessen: bis zu 20 Partitionen, je [classId:u16][label+appName+appVersion+
        // appDate+appTime (je <=32+null)][type,subtype,otaState,running:4 Byte][size:u32].
        static uint8_t partitions_scratch[20 * 192];
        size_t partitions_pos = 0;
        size_t partitions_count = 0;

        while (it)
        {
            const esp_partition_t *p = esp_partition_get(it);
            esp_ota_img_states_t ota_state;
            esp_ota_get_state_partition(p, &ota_state);
            esp_app_desc_t app_info = {};
            esp_ota_get_partition_description(p, &app_info);

            WsProtocol::systeminfo::PartitionInfo::Payload item{};
            item.label = p->label;
            item.type = (uint8_t)p->type;
            item.subtype = (uint8_t)p->subtype;
            item.size = p->size;
            item.otaState = (int8_t)ota_state;
            item.running = (p == running);
            bool isUnpopulatedAppPartition = p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && p->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX && !(ota_state == ESP_OTA_IMG_NEW || ota_state == ESP_OTA_IMG_PENDING_VERIFY || ota_state == ESP_OTA_IMG_VALID || ota_state == ESP_OTA_IMG_INVALID || ota_state == ESP_OTA_IMG_ABORTED);
            if (isUnpopulatedAppPartition)
            {
                item.appName = "";
                item.appVersion = "";
                item.appDate = "";
                item.appTime = "";
            }
            else
            {
                item.appName = app_info.project_name;
                item.appVersion = app_info.version;
                item.appDate = app_info.date;
                item.appTime = app_info.time;
            }

            size_t newPos = WsProtocol::systeminfo::AppendResponseSystemDataPartitionsPartitionInfoElement(item, partitions_scratch, partitions_pos, sizeof(partitions_scratch));
            if (newPos > 0)
            {
                partitions_pos = newPos;
                partitions_count++;
            }
            it = esp_partition_next(it);
        }

        esp_chip_info_t chip_info = {};
        esp_chip_info(&chip_info);
        struct timeval tv_now;
        gettimeofday(&tv_now, nullptr);

        float tsens_out{0.0};
        if(tempHandle){
            ESP_ERROR_CHECK(temperature_sensor_get_celsius(tempHandle, &tsens_out));
        }

        WsProtocol::systeminfo::ResponseSystemData::Payload resp{};
        resp.requestId = requestId;
        resp.secondsEpoch = tv_now.tv_sec;
        resp.secondsUptime = esp_timer_get_time() / 1000000;
        resp.freeHeap = esp_get_free_heap_size();
        resp.macAddressWifiSta = ReadMac(ESP_MAC_WIFI_STA);
        resp.macAddressWifiSoftap = ReadMac(ESP_MAC_WIFI_SOFTAP);
        resp.macAddressBt = ReadMac(ESP_MAC_BT);
        resp.macAddressEth = ReadMac(ESP_MAC_ETH);
#if CONFIG_SOC_IEEE802154_SUPPORTED
        resp.macAddressIeee802154 = ReadMac(ESP_MAC_IEEE802154);
#else
        resp.macAddressIeee802154 = {};
#endif
        resp.chipModel = (uint32_t)chip_info.model;
        resp.chipFeatures = chip_info.features;
        resp.chipRevision = chip_info.revision;
        resp.chipCores = chip_info.cores;
        resp.chipTemperature = tsens_out;
        resp.partitionsData = partitions_scratch;
        resp.partitionsCount = partitions_count;
        resp.partitionsDataSize = partitions_pos;

        static uint8_t buf[4096];
        size_t len = WsProtocol::systeminfo::ResponseSystemData::Encode(resp, buf, sizeof(buf));
        return (len > 0 && callback->SendRawAsync(buf, len) == ESP_OK) ? webmanager::eMessageReceiverResult::OK : webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
    }

public:
    SystemInfoPlugin(temperature_sensor_handle_t tempHandle):tempHandle(tempHandle)
    {
    }

    void OnBegin(webmanager::iWebmanagerCallback *callback) override
    {
        (void)(callback);

    }
    void OnWifiConnect(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }
    void OnWifiDisconnect(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }
    void OnTimeUpdate(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }
    webmanager::eMessageReceiverResult ProvideWebsocketMessage(webmanager::iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen) override
    {
        if (namespaceId != WsProtocol::systeminfo::NAMESPACE_ID)
            return webmanager::eMessageReceiverResult::NOT_FOR_ME;

        switch (messageTypeId)
        {
        case WsProtocol::systeminfo::RequestRestart::TYPE_ID:
        {
            esp_restart();
            return webmanager::eMessageReceiverResult::OK;
        }

        case WsProtocol::systeminfo::RequestSystemData::TYPE_ID:
        {
            WsProtocol::systeminfo::RequestSystemData::Payload req{};
            if (!WsProtocol::systeminfo::RequestSystemData::Decode(frame, frameLen, req))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            return sendResponseSystemData(callback, req.requestId);
        }
        default:
            return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
        }
    }
};
#undef TAG

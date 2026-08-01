#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <memory>
#include <map>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wsprotocol_cpp/ws_protocol.hh"
#include "sdkconfig.h"
#include <common.hh>
#include "interfaces.hh"
#include "scheduler_timers.hh"
#include "sunsetsunrise.hh"
#include "webmanager_interfaces.hh"
#define TAG "SCHEDULER"
#include "esp_log.h"
namespace scheduler
{
    class Scheduler : public webmanager::iWebmanagerPlugin, public webmanager::iScheduler
    {
    private:
        webmanager::iWebmanagerCallback *callback;
        nvs_handle_t nvsSchedulerHandle;
        std::map<std::string, aTimer *> name2timer;
        uint32_t julianDay{0};

    public:
        Scheduler(nvs_handle_t nvsSchedulerHandle) : nvsSchedulerHandle(nvsSchedulerHandle) {}

        uint16_t GetCurrentValueOfSchedule(const char *schedulerName) override
        {
            if (!this->name2timer.contains(std::string(schedulerName)))
                return 0;
            time_t currentTime;

            time(&currentTime); // Get the current time
            return GetCurrentValueOfSchedule(schedulerName, currentTime);
        }

        uint16_t GetCurrentValueOfSchedule(const char *schedulerName, time_t currentTime)
        {

            scheduler::aTimer *o = this->name2timer.at(std::string(schedulerName));
            if (currentTime > 1800 && currentTime < 1716498366L)
            { // Failsafe: If System runs for at least half an hour, but has no valid timestamp (constant is epoch time when writing this code)
                return true;
            }
            struct tm *localTime;
            localTime = localtime(&currentTime); // Convert the current time to the local time
            int day_of_week = localTime->tm_wday;
            int h = localTime->tm_hour;
            int m = localTime->tm_min;
            int s = localTime->tm_sec;
            uint16_t val = o->GetCurrentValue(currentTime, day_of_week, h, m, s);
            ESP_LOGI(TAG, "On %d:%d:%d und weekday %d timer is %d", h, m, s, day_of_week, val);
            return val;
        }
        void Begin()
        {
            name2timer.clear();
            name2timer[ALWAYS.GetName()] = &ALWAYS;
            name2timer[NEVER.GetName()] = &NEVER,
            name2timer[DAILY_6_22.GetName()] = &DAILY_6_22;
            name2timer[WORKING_DAYS_7_18.GetName()] = &WORKING_DAYS_7_18;
            name2timer[TestEvenMinutesOnOddMinutesOff.GetName()] = &TestEvenMinutesOnOddMinutesOff;
            nvs_iterator_t it = nullptr;
            esp_err_t res = nvs_entry_find_in_handle(this->nvsSchedulerHandle, NVS_TYPE_BLOB, &it);
            while (res == ESP_OK)
            {
                nvs_entry_info_t info;
                nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL
                size_t length{0};
                nvs_get_blob(this->nvsSchedulerHandle, info.key, nullptr, &length);
                uint8_t data[length];
                nvs_get_blob(this->nvsSchedulerHandle, info.key, data, &length);
                WsProtocol::scheduler::Schedule::Payload schedule{};
                size_t pos = 0;
                if (WsProtocol::scheduler::Schedule::Decode(data, length, pos, schedule))
                {
                    aTimer *t = Builder::BuildFromSchedule(schedule);
                    if (t) this->name2timer[t->GetName()] = t;
                }
                res = nvs_entry_next(&it);
            }
            nvs_release_iterator(it);
        }

        esp_err_t Loop(time_t unixSecs)
        {

            uint32_t newJulianDay = sunsetsunrise::JulianDate(unixSecs);
            if (newJulianDay == this->julianDay)
                return ESP_OK;
            this->julianDay = newJulianDay;
            double latDeg = 52.0965;
            double lonDeg = 7.6171;
            time_t sunriseUnixSecs{0};
            time_t sunsetUnixSecs{0};
            sunsetsunrise::NextSunriseAndSunset<double>(this->julianDay, latDeg, lonDeg, sunsetsunrise::eDawn::CIVIL, sunriseUnixSecs, sunsetUnixSecs);
            ESP_LOGI(TAG, "A new julian day %lu has begun. sunrise %lld, sunset %lld", julianDay, sunriseUnixSecs, sunsetUnixSecs);
            for (auto const &[key, val] : name2timer)
            {
                val->NewDayHasBegun(julianDay, sunriseUnixSecs, sunsetUnixSecs);
            }
            return ESP_OK;
        }

        webmanager::eMessageReceiverResult handleRequestOpen(webmanager::iWebmanagerCallback *callback, const WsProtocol::scheduler::RequestSchedulerOpen::Payload &req)
        {
            if (!this->name2timer.contains(req.name))
            {
                ESP_LOGW(TAG, "Did not found scheduler '%s' in local database", req.name);
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            }
            ESP_LOGI(TAG, "Send ResponseSchedulerOpen for '%s' back to browser", req.name);
            scheduler::aTimer *o = this->name2timer.at(req.name);

            uint8_t variant_scratch[128];
            size_t variantLen = o->EncodeScheduleVariant(variant_scratch, 0, sizeof(variant_scratch));
            WsProtocol::scheduler::Schedule::Payload schedule{};
            schedule.name = req.name;
            schedule.scheduleData = variant_scratch;
            schedule.scheduleDataSize = variantLen;

            uint8_t payload_scratch[160];
            size_t payloadLen = WsProtocol::scheduler::AppendResponseSchedulerOpenPayloadScheduleElement(schedule, payload_scratch, 0, sizeof(payload_scratch));

            WsProtocol::scheduler::ResponseSchedulerOpen::Payload resp{};
            resp.requestId = req.requestId;
            resp.payloadData = payload_scratch;
            resp.payloadDataSize = payloadLen;

            uint8_t buf[256];
            size_t len = WsProtocol::scheduler::ResponseSchedulerOpen::Encode(resp, buf, sizeof(buf));
            return (len > 0 && callback->SendRawAsync(buf, len) == ESP_OK) ? webmanager::eMessageReceiverResult::OK : webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
        }

        void FillAvailableScheduleNames(std::vector<std::string> &names) override
        {
            for (auto const &[key, val] : name2timer)
            {
                names.push_back(key);
            }
        }

        webmanager::eMessageReceiverResult handleRequestList(webmanager::iWebmanagerCallback *callback, uint16_t requestId)
        {
            static uint8_t items_scratch[64 * 40];
            size_t items_pos = 0;
            size_t items_count = 0;
            for (auto const &[key, val] : name2timer)
            {
                WsProtocol::scheduler::SchedulerListItem::Payload item{};
                item.name = key.c_str();
                item.type = val->GetScheduleType();
                size_t newPos = WsProtocol::scheduler::AppendResponseSchedulerListItemsSchedulerListItemElement(item, items_scratch, items_pos, sizeof(items_scratch));
                if (newPos > 0) { items_pos = newPos; items_count++; }
            }

            WsProtocol::scheduler::ResponseSchedulerList::Payload resp{};
            resp.requestId = requestId;
            resp.itemsData = items_scratch;
            resp.itemsCount = items_count;
            resp.itemsDataSize = items_pos;

            static uint8_t buf[4096];
            size_t len = WsProtocol::scheduler::ResponseSchedulerList::Encode(resp, buf, sizeof(buf));
            return (len > 0 && callback->SendRawAsync(buf, len) == ESP_OK) ? webmanager::eMessageReceiverResult::OK : webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
        }

        webmanager::eMessageReceiverResult handleRequestDelete(webmanager::iWebmanagerCallback *callback, const WsProtocol::scheduler::RequestSchedulerDelete::Payload &req)
        {
            if (!this->name2timer.contains(req.name))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            this->name2timer.erase(req.name);
            nvs_erase_key(this->nvsSchedulerHandle, req.name);
            nvs_commit(this->nvsSchedulerHandle);
            ESP_LOGI(TAG, "Successfully deleted fingerprint %s", req.name);
            return handleRequestList(callback, req.requestId);
        }

        webmanager::eMessageReceiverResult handleRequestRename(webmanager::iWebmanagerCallback *callback, const WsProtocol::scheduler::RequestSchedulerRename::Payload &req)
        {
            if (!this->name2timer.contains(req.oldName))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            if (this->name2timer.contains(req.newName))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            scheduler::aTimer *o = this->name2timer.at(req.oldName);
            size_t max_size{128};
            uint8_t blob[max_size];
            o->RenameAndFillNvsBlob(req.newName, blob, max_size);
            this->name2timer.erase(req.oldName);
            this->name2timer[req.newName] = o;
            nvs_erase_key(this->nvsSchedulerHandle, req.oldName);
            nvs_set_blob(this->nvsSchedulerHandle, req.newName, blob, max_size);
            nvs_commit(this->nvsSchedulerHandle);
            ESP_LOGI(TAG, "Successfully renamed fingerprint %s to %s", req.oldName, req.newName);
            return handleRequestList(callback, req.requestId);
        }

        webmanager::eMessageReceiverResult handleRequestSave(webmanager::iWebmanagerCallback *callback, const WsProtocol::scheduler::RequestSchedulerSave::Payload &req)
        {
            bool ok = WsProtocol::scheduler::DecodeRequestSchedulerSavePayloadElements(req.payloadData, req.payloadDataSize, 1,
                [&](auto &schedule) {
                    std::string name = schedule.name;
                    if (this->name2timer.contains(name))
                    {
                        ESP_LOGI(TAG, "%s is as existing fingerprint -->erase old in map and in flash", name.c_str());
                        scheduler::aTimer *o = this->name2timer.at(name);
                        this->name2timer.erase(name);
                        nvs_erase_key(this->nvsSchedulerHandle, name.c_str());
                        delete (o);
                    }
                    aTimer *t = Builder::BuildFromSchedule(schedule);
                    if (!t) return;
                    this->name2timer[name] = t;
                    size_t max_size{128};
                    uint8_t blob[max_size];
                    t->FillNvsBlob(blob, max_size);
                    nvs_set_blob(this->nvsSchedulerHandle, name.c_str(), blob, max_size);
                    nvs_commit(this->nvsSchedulerHandle);
                    ESP_LOGI(TAG, "Successfully saved fingerprint %s ", name.c_str());
                });
            if (!ok) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            return handleRequestList(callback, req.requestId);
        }

        void OnBegin(webmanager::iWebmanagerCallback*) override{}
        void OnWifiConnect(webmanager::iWebmanagerCallback*) override{}
        void OnWifiDisconnect(webmanager::iWebmanagerCallback*)override{}
        void OnTimeUpdate(webmanager::iWebmanagerCallback*)override{}
        webmanager::eMessageReceiverResult ProvideWebsocketMessage(webmanager::iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen) override
        {
            if (namespaceId != WsProtocol::scheduler::NAMESPACE_ID)
                return webmanager::eMessageReceiverResult::NOT_FOR_ME;
            switch (messageTypeId)
            {
            case WsProtocol::scheduler::RequestSchedulerList::TYPE_ID:
            {
                WsProtocol::scheduler::RequestSchedulerList::Payload r{};
                if (!WsProtocol::scheduler::RequestSchedulerList::Decode(frame, frameLen, r)) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
                return handleRequestList(callback, r.requestId);
            }
            case WsProtocol::scheduler::RequestSchedulerDelete::TYPE_ID:
            {
                WsProtocol::scheduler::RequestSchedulerDelete::Payload r{};
                if (!WsProtocol::scheduler::RequestSchedulerDelete::Decode(frame, frameLen, r)) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
                return handleRequestDelete(callback, r);
            }
            case WsProtocol::scheduler::RequestSchedulerRename::TYPE_ID:
            {
                WsProtocol::scheduler::RequestSchedulerRename::Payload r{};
                if (!WsProtocol::scheduler::RequestSchedulerRename::Decode(frame, frameLen, r)) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
                return handleRequestRename(callback, r);
            }
            case WsProtocol::scheduler::RequestSchedulerSave::TYPE_ID:
            {
                WsProtocol::scheduler::RequestSchedulerSave::Payload r{};
                if (!WsProtocol::scheduler::RequestSchedulerSave::Decode(frame, frameLen, r)) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
                return handleRequestSave(callback, r);
            }
            case WsProtocol::scheduler::RequestSchedulerOpen::TYPE_ID:
            {
                WsProtocol::scheduler::RequestSchedulerOpen::Payload r{};
                if (!WsProtocol::scheduler::RequestSchedulerOpen::Decode(frame, frameLen, r)) return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
                return handleRequestOpen(callback, r);
            }
            default:
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            }
        }
    };

}
#undef TAG
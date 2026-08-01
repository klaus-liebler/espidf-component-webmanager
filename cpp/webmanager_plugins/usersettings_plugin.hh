#pragma once
#include "webmanager_interfaces.hh"
#include "wsprotocol_cpp/ws_protocol.hh"
#include <type_traits>
#include <cstring>
#include <array>

// Fuer NVS-Speicherung ist der konkrete Settings-Typ (nicht nur der Wire-classId) noetig -- dieses
// Enum ersetzt das vormalige Flatbuffers-Union-Enum "usersettings::Setting" und wird von GroupCfg/
// SettingCfg (board-/projektspezifisch, s. usersettings/nvs_accessor.hh.inc) weiterhin verwendet.
enum class SettingKind
{
    IntegerSetting,
    EnumSetting,
    BooleanSetting,
    StringSetting,
};

struct SettingCfg
{
    const char *settingKey;
    SettingKind type;
};

class GroupCfg
{
public:
    const char *groupKey;
    size_t setting_len;
    SettingCfg settings[];
};

struct GroupAndStringSetting
{
    const char *groupKey;
    const char *settingkey;
};

struct GroupAndIntegerSetting
{
    const char *groupKey;
    const char *settingkey;
};

struct GroupAndBooleanSetting
{
    const char *groupKey;
    const char *settingkey;
};

struct GroupAndEnumSetting
{
    const char *groupKey;
    const char *settingkey;
};

#include "usersettings/nvs_accessor.hh.inc"

class UsersettingsPlugin : public webmanager::iWebmanagerPlugin
{
private:
    const char *partitionName{nullptr};

    const GroupCfg *GetGroup(const char *groupKey)
    {

        for (const GroupCfg *group : groups)
        {
            if (strcmp(groupKey, group->groupKey) == 0)
            {
                return group;
            }
        }
        return nullptr;
    }

public:
    UsersettingsPlugin(const char *partitionName) : partitionName(partitionName) {}

    webmanager::eMessageReceiverResult handleRequestSetUserSettings(const WsProtocol::usersettings::RequestSetUserSettings::Payload &req, webmanager::iWebmanagerCallback *callback)
    {
        const char *groupKey = req.groupKey;
        const GroupCfg *group = GetGroup(groupKey);
        RETURN_FAIL_ON_FALSE(group != nullptr, "There is no group with key '%s'", groupKey);
        ESP_LOGI(TAG, "In handleRequestSetUserSettings for GroupKey %s ItemCount %u", groupKey, group->setting_len);

        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open_from_partition(partitionName, group->groupKey, NVS_READWRITE, &nvs_handle));
        ESP_LOGI(TAG, "Successfully opened partition, group: %s with %u items. Updating %u items", group->groupKey, group->setting_len, (unsigned)req.settingsCount);

        // Grosszuegig bemessen: bis zu 32 Settings-Keys, je <=32 Zeichen + Nullterminator.
        static uint8_t keys_scratch[32 * 33];
        size_t keys_pos = 0;
        size_t keys_count = 0;

        WsProtocol::usersettings::DecodeRequestSetUserSettingsSettingsElements(req.settingsData, req.settingsDataSize, req.settingsCount,
            [&](auto &item) {
                using T = std::decay_t<decltype(item)>;
                const char *settingKey = item.settingKey;
                if constexpr (std::is_same_v<T, WsProtocol::usersettings::IntegerSettingWrapper::Payload>) {
                    ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, settingKey, item.value));
                } else if constexpr (std::is_same_v<T, WsProtocol::usersettings::EnumSettingWrapper::Payload>) {
                    ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, settingKey, item.value));
                } else if constexpr (std::is_same_v<T, WsProtocol::usersettings::BooleanSettingWrapper::Payload>) {
                    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, settingKey, item.value ? 1 : 0));
                } else if constexpr (std::is_same_v<T, WsProtocol::usersettings::StringSettingWrapper::Payload>) {
                    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, settingKey, item.value));
                }
                size_t keyLen = strlen(settingKey);
                if (keys_pos + keyLen + 1 <= sizeof(keys_scratch)) {
                    memcpy(keys_scratch + keys_pos, settingKey, keyLen);
                    keys_pos += keyLen;
                    keys_scratch[keys_pos++] = 0;
                    keys_count++;
                }
            });

        // Close
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        WsProtocol::usersettings::ResponseSetUserSettings::Payload resp{};
        resp.requestId = req.requestId;
        resp.groupKey = groupKey;
        resp.settingKeysData = keys_scratch;
        resp.settingKeysCount = keys_count;
        resp.settingKeysDataSize = keys_pos;

        static uint8_t buf[2048];
        size_t len = WsProtocol::usersettings::ResponseSetUserSettings::Encode(resp, buf, sizeof(buf));
        if (len > 0) callback->SendRawAsync(buf, len);
        return webmanager::eMessageReceiverResult::OK;
    }

    esp_err_t GetIntegerSetting(const GroupAndIntegerSetting &s, int32_t *value)
    {
        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open(s.groupKey, NVS_READONLY, &nvs_handle));
        esp_err_t ret = nvs_get_i32(nvs_handle, s.settingkey, value);
        nvs_close(nvs_handle);
        return ret;
    }

    esp_err_t GetStringSetting(const GroupAndStringSetting &s, char *value, size_t maxLen)
    {
        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open(s.groupKey, NVS_READONLY, &nvs_handle));
        esp_err_t ret = nvs_get_str(nvs_handle, s.settingkey, value, &maxLen);
        nvs_close(nvs_handle);
        return ret;
    }

    esp_err_t GetBoolSetting(const GroupAndBooleanSetting &s, bool *value)
    {
        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open(s.groupKey, NVS_READONLY, &nvs_handle));
        uint8_t tmp{0};
        esp_err_t ret = nvs_get_u8(nvs_handle, s.settingkey, &tmp);
        nvs_close(nvs_handle);
        *value = tmp != 0;
        return ret;
    }

    esp_err_t GetEnumSetting(const GroupAndEnumSetting &s, int32_t *value)
    {
        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open(s.groupKey, NVS_READONLY, &nvs_handle));
        esp_err_t ret = nvs_get_i32(nvs_handle, s.settingkey, value);
        nvs_close(nvs_handle);
        return ret;
    }

    webmanager::eMessageReceiverResult handleRequestGetUserSettings(const WsProtocol::usersettings::RequestGetUserSettings::Payload &get, webmanager::iWebmanagerCallback *callback)
    {
        const char *groupKey = get.groupKey;
        const GroupCfg *group = GetGroup(groupKey);
        RETURN_FAIL_ON_FALSE(group != nullptr, "There is no group with key '%s'", groupKey);
        ESP_LOGI(TAG, "In handleRequestGetUserSettings for GroupKey %s ItemCount %u", groupKey, group->setting_len);

        nvs_handle_t nvs_handle{0};
        RETURN_ON_ERROR(nvs_open_from_partition(partitionName, group->groupKey, NVS_READONLY, &nvs_handle));
        ESP_LOGI(TAG, "Successfully opened partition, reading %u items", group->setting_len);

        // Grosszuegig bemessen: bis zu 32 Settings, je [classId:u16][settingKey<=32+null][Wert<=32+null].
        static uint8_t settings_scratch[32 * 96];
        size_t settings_pos = 0;
        size_t settings_count = 0;

        for (size_t i = 0; i < group->setting_len; i++)
        {
            const SettingCfg *settingCfg = &group->settings[i];
            const char *settingKey = settingCfg->settingKey;
            size_t newPos = 0;
            switch (settingCfg->type)
            {
            case SettingKind::IntegerSetting:
            {
                int32_t value{0};
                ESP_ERROR_CHECK(nvs_get_i32(nvs_handle, settingKey, &value));
                WsProtocol::usersettings::IntegerSettingWrapper::Payload item{settingKey, value};
                newPos = WsProtocol::usersettings::AppendResponseGetUserSettingsSettingsIntegerSettingWrapperElement(item, settings_scratch, settings_pos, sizeof(settings_scratch));
                break;
            }
            case SettingKind::EnumSetting:
            {
                int32_t value{0};
                ESP_ERROR_CHECK(nvs_get_i32(nvs_handle, settingKey, &value));
                WsProtocol::usersettings::EnumSettingWrapper::Payload item{settingKey, value};
                newPos = WsProtocol::usersettings::AppendResponseGetUserSettingsSettingsEnumSettingWrapperElement(item, settings_scratch, settings_pos, sizeof(settings_scratch));
                break;
            }
            case SettingKind::BooleanSetting:
            {
                uint8_t value{0};
                ESP_ERROR_CHECK(nvs_get_u8(nvs_handle, settingKey, &value));
                WsProtocol::usersettings::BooleanSettingWrapper::Payload item{settingKey, value != 0};
                newPos = WsProtocol::usersettings::AppendResponseGetUserSettingsSettingsBooleanSettingWrapperElement(item, settings_scratch, settings_pos, sizeof(settings_scratch));
                break;
            }
            case SettingKind::StringSetting:
            {
                /*
                To get the size necessary to store the value, call nvs_get_str with zero out_value and non-zero pointer to length.
                Variable pointed to by length argument will be set to the required length.
                For nvs_get_str, this length includes the zero terminator.
                When calling nvs_get_str with non-zero out_value, length has to be non-zero and has to point to the length available in out_value.
                */

                char *value{nullptr};
                size_t length{0};
                if (nvs_get_str(nvs_handle, settingKey, value, &length) != ESP_OK)
                {
                    ESP_LOGW(TAG, "Can`t sead StringSetting %s", settingKey);
                    break;
                }
                // now, length contains the necessary size
                if (length == 0)
                {
                    ESP_LOGW(TAG, "Read StringSetting %s successfully, but with zero length value", settingKey);
                    value = new char[1]{'\0'};
                }
                else
                {
                    value = new char[length];
                    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, settingKey, value, &length));
                    ESP_LOGI(TAG, "Read StringSetting %s successfully with value %s", settingKey, value);
                }
                WsProtocol::usersettings::StringSettingWrapper::Payload item{settingKey, value};
                newPos = WsProtocol::usersettings::AppendResponseGetUserSettingsSettingsStringSettingWrapperElement(item, settings_scratch, settings_pos, sizeof(settings_scratch));
                delete[] value;
                break;
            }
            default:
                break;
            }
            if (newPos > 0)
            {
                settings_pos = newPos;
                settings_count++;
            }
        }
        // Close
        nvs_close(nvs_handle);

        WsProtocol::usersettings::ResponseGetUserSettings::Payload resp{};
        resp.requestId = get.requestId;
        resp.groupKey = groupKey;
        resp.settingsData = settings_scratch;
        resp.settingsCount = settings_count;
        resp.settingsDataSize = settings_pos;

        static uint8_t buf[4096];
        size_t len = WsProtocol::usersettings::ResponseGetUserSettings::Encode(resp, buf, sizeof(buf));
        if (len > 0) callback->SendRawAsync(buf, len);
        return webmanager::eMessageReceiverResult::OK;
    }

    void OnBegin(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }

    void OnWifiConnect(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }
    void OnWifiDisconnect(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }
    void OnTimeUpdate(webmanager::iWebmanagerCallback *callback) override { (void)(callback); }

    webmanager::eMessageReceiverResult ProvideWebsocketMessage(webmanager::iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen) override
    {
        if (namespaceId != WsProtocol::usersettings::NAMESPACE_ID)
            return eMessageReceiverResult::NOT_FOR_ME;

        switch (messageTypeId)
        {
        case WsProtocol::usersettings::RequestGetUserSettings::TYPE_ID:
        {
            WsProtocol::usersettings::RequestGetUserSettings::Payload req{};
            if (!WsProtocol::usersettings::RequestGetUserSettings::Decode(frame, frameLen, req))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            return handleRequestGetUserSettings(req, callback);
        }
        case WsProtocol::usersettings::RequestSetUserSettings::TYPE_ID:
        {
            WsProtocol::usersettings::RequestSetUserSettings::Payload req{};
            if (!WsProtocol::usersettings::RequestSetUserSettings::Decode(frame, frameLen, req))
                return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
            return handleRequestSetUserSettings(req, callback);
        }

        default:
            return webmanager::eMessageReceiverResult::FOR_ME_BUT_FAILED;
        }
    }
};

#pragma once
#include <esp_err.h>
#include <esp_http_server.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace webmanager
{
    enum class eMessageReceiverResult
    {
        OK = 0,
        NOT_FOR_ME = 1,
        FOR_ME_BUT_FAILED,
    };

    class iWebmanagerCallback
    {
    public:
        // 'data' muss bereits einen vollstaendigen ws-protocol-Frame sein (4-Byte-Kopf
        // namespaceId:u16 + messageTypeId:u16 + Payload, s. generiertes ws_protocol.hh,
        // <Namespace>::<Message>::Encode()). Ersetzt das vormalige, Flatbuffers-spezifische
        // WrapAndSendAsync(uint32_t, FlatBufferBuilder&) vollstaendig -- kein Plugin baut mehr
        // Flatbuffers-Antworten.
        virtual esp_err_t SendRawAsync(const uint8_t* data, size_t len) = 0;
    };

    class iWebmanagerPlugin
    {
    public:
        virtual void OnBegin(iWebmanagerCallback *callback) = 0;
        virtual void OnWifiConnect(iWebmanagerCallback *callback) = 0;
        virtual void OnWifiDisconnect(iWebmanagerCallback *callback) = 0;
        virtual void OnTimeUpdate(iWebmanagerCallback *callback)=0;
        // 'frame' zeigt auf den KOMPLETTEN eingehenden Frame inkl. 4-Byte-Kopf (namespaceId/
        // messageTypeId sind bereits vom Dispatcher geparst und werden hier zusaetzlich
        // mitgegeben) -- passend zu den generierten <Namespace>::<Message>::Decode(data, len,
        // out)-Funktionen, die selbst intern 'pos=4' ueberspringen. Kein Vorab-Slicing mehr.
        virtual eMessageReceiverResult ProvideWebsocketMessage(iWebmanagerCallback *callback, httpd_req_t *req, httpd_ws_frame_t *ws_pkt, uint16_t namespaceId, uint16_t messageTypeId, const uint8_t *frame, size_t frameLen) = 0;
    };

    class iScheduler{
    public:
        virtual uint16_t GetCurrentValueOfSchedule(const char* schedulerName)=0;
        // Ersetzt das vormalige, Flatbuffers-spezifische FillFlatbufferWithAvailableNames(
        // FlatBufferBuilder&, vector<Offset<String>>&) -- Aufrufer (z.B. fingerprint) bauen ihre
        // eigene wire-Repraesentation selbst aus den Klartext-Namen.
        virtual void FillAvailableScheduleNames(std::vector<std::string> &names) = 0;
    };
}
#pragma once
#include <esp_err.h>
#include <esp_http_server.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "wsprotocol_cpp/ws_protocol.hh"

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
        // 'data': a complete frame (8-byte header + payload, see the generated ws_protocol.hh,
        // <Namespace>::<Message>::Encode()) - for events and pushes. The data is copied.
        virtual esp_err_t SendRawAsync(const uint8_t* data, size_t len) = 0;

        // A Responder for an answer sent later, outside ProvideWebsocketMessage() (e.g. from an event
        // task): 'request' is the header of the request (a copy of reply.Request()), 'buffer' holds the
        // answer while it is encoded.
        virtual WsProtocol::Responder ResponderFor(const WsProtocol::FrameHeader &request, uint8_t *buffer, size_t size) = 0;
    };

    class iWebmanagerPlugin
    {
    public:
        virtual void OnBegin(iWebmanagerCallback *callback) = 0;
        virtual void OnWifiConnect(iWebmanagerCallback *callback) = 0;
        virtual void OnWifiDisconnect(iWebmanagerCallback *callback) = 0;
        virtual void OnTimeUpdate(iWebmanagerCallback *callback)=0;
        // A received frame ('frame': complete, header included). Its header is reply.Request()
        // (namespaceId, messageTypeId); the answer goes through 'reply' (Send(): carries the request's
        // correlation, Error(): an error reply) - never touch the header yourself. Decoding with
        // WsProtocol::Dispatch() answers Malformed by itself.
        // NOT_FOR_ME: the next plugin is asked (none: the requester gets UnknownMessage).
        // FOR_ME_BUT_FAILED without an answer: the requester gets the error reply Failed.
        virtual eMessageReceiverResult ProvideWebsocketMessage(iWebmanagerCallback *callback, WsProtocol::Responder &reply, const uint8_t *frame, size_t frameLen) = 0;
    };

    // WsProtocol::Dispatch() for plugin handlers that return an eMessageReceiverResult: decodes the
    // request with its generated Decode() and returns handler(reply, payload); a frame that cannot be
    // decoded is answered Malformed (OK: it is answered).
    template <class Payload, class Handler>
    eMessageReceiverResult Handle(WsProtocol::Responder &reply, const uint8_t *frame, size_t frameLen,
                                  bool (*decode)(const uint8_t *, size_t, Payload &), Handler &&handler)
    {
        eMessageReceiverResult result = eMessageReceiverResult::OK;
        WsProtocol::Dispatch(reply, frame, frameLen, decode,
                             [&](WsProtocol::Responder &r, const Payload &payload) { result = handler(r, payload); });
        return result;
    }

    class iScheduler{
    public:
        virtual uint16_t GetCurrentValueOfSchedule(const char* schedulerName)=0;
        // Ersetzt das vormalige, Flatbuffers-spezifische FillFlatbufferWithAvailableNames(
        // FlatBufferBuilder&, vector<Offset<String>>&) -- Aufrufer (z.B. fingerprint) bauen ihre
        // eigene wire-Repraesentation selbst aus den Klartext-Namen.
        virtual void FillAvailableScheduleNames(std::vector<std::string> &names) = 0;
    };
}
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// 'data' ist bereits ein vollstaendiger ws-protocol-Frame (4-Byte-Kopf namespaceId:u16+
// messageTypeId:u16 + Payload, s. generiertes ws_protocol.hh) -- wird unveraendert kopiert,
// kein zusaetzliches Voranstellen eines Namespace-Praefix mehr noetig (ersetzt den vormaligen,
// Flatbuffers-spezifischen Konstruktor AsyncResponse(uint32_t ns, FlatBufferBuilder*)).
class AsyncResponse{
    public:
    uint8_t* buffer;
    size_t buffer_len;

    AsyncResponse(const uint8_t* data, size_t len){
        buffer_len = len;
        buffer = new uint8_t[len];
        std::memcpy(buffer, data, len);
    }

    ~AsyncResponse(){
        delete[] buffer;
    }
};

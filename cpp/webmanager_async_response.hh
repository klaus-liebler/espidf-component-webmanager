#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// 'data' is a complete frame (8-byte header + payload, see the generated ws_protocol.hh); copied
// unchanged, so the sender's buffer may be reused at once.
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

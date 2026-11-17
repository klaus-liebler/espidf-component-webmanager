#pragma once
// Das ESP-IDF-Log (ESP_LOGx) im Browser, Namespace 'firmwarelog' (best_binary_buffers_schema/
// firmwarelog.cs). Opt-in: nur Firmware, die dieses Plugin anmeldet, hat es.
//
//   plugins.push_back(new FirmwareLogPlugin());   // moeglichst frueh: puffert ab hier auch die Startmeldungen
//
// Ein Hook auf esp_log_set_vprintf() gibt jede Zeile unveraendert an die bisherige Ausgabe (UART) weiter
// und legt sie zusaetzlich zerlegt (Level, Tag, Text) in einen Ringpuffer. Gesendet wird nicht im Hook
// (der laeuft in beliebigen Tasks, auch mit kleinem Stack, und das Senden loggt selbst), sondern von
// einer eigenen Task: erst nachdem der Browser mit RequestLogHistory angefordert hat, dann den
// Pufferinhalt und jede neue Zeile, bis die Verbindung weg ist. Zeilen dieser Task selbst werden nicht
// gepuffert (sonst Rueckkopplung). Ist der Puffer voll, bevor gesendet werden konnte, gehen die aeltesten
// Zeilen verloren; der Browser sieht das an der Luecke in Sequence.
#include "webmanager_interfaces.hh"
#include "wsprotocol_cpp/ws_protocol.hh"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

class FirmwareLogPlugin : public webmanager::iWebmanagerPlugin
{
public:
    static constexpr size_t TAG_LEN = 24;   // 23 Byte + \0, wie im Schema
    static constexpr size_t TEXT_LEN = 192; // 191 Byte + \0, wie im Schema

    struct Entry
    {
        uint32_t sequence;
        uint32_t uptimeMs;
        uint8_t level;
        char tag[TAG_LEN];
        char text[TEXT_LEN];
    };

    // capacity: gepufferte Zeilen (je ca. 225 Byte; im PSRAM, falls vorhanden).
    explicit FirmwareLogPlugin(size_t capacity = 32) : capacity(capacity)
    {
        ring = static_cast<Entry *>(heap_caps_malloc(capacity * sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!ring)
            ring = static_cast<Entry *>(heap_caps_malloc(capacity * sizeof(Entry), MALLOC_CAP_8BIT));
        mutex = xSemaphoreCreateMutex();
        if (!ring || !mutex)
        {
            ESP_LOGE("FWLOG", "Not enough memory for the firmware log buffer");
            return;
        }
        instance = this;
        previous = esp_log_set_vprintf(&FirmwareLogPlugin::vprintfHook);
    }

    void OnBegin(webmanager::iWebmanagerCallback *cb) override
    {
        callback = cb;
        if (instance == this && !task)
            xTaskCreate(&FirmwareLogPlugin::taskMain, "fwlog", 3072, this, tskIDLE_PRIORITY + 1, &task);
    }
    void OnWifiConnect(webmanager::iWebmanagerCallback *cb) override { (void)cb; }
    void OnWifiDisconnect(webmanager::iWebmanagerCallback *cb) override { (void)cb; }
    void OnTimeUpdate(webmanager::iWebmanagerCallback *cb) override { (void)cb; }

    webmanager::eMessageReceiverResult ProvideWebsocketMessage(webmanager::iWebmanagerCallback *cb, WsProtocol::Responder &reply, const uint8_t *frame, size_t frameLen) override
    {
        (void)cb;
        namespace fl = WsProtocol::firmwarelog;
        if (reply.Request().namespaceId != fl::NAMESPACE_ID)
            return webmanager::eMessageReceiverResult::NOT_FOR_ME;
        switch (reply.Request().messageTypeId)
        {
        case fl::RequestLogHistory::TYPE_ID:
            return webmanager::Handle(reply, frame, frameLen, fl::RequestLogHistory::Decode,
                                      [this](WsProtocol::Responder &r, const fl::RequestLogHistory::Payload &)
                                      {
                                          fl::ResponseLogHistory::Payload resp{};
                                          if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE)
                                          {
                                              resp.oldestSequence = oldestSequence();
                                              resp.nextSequence = nextSequence;
                                              sendSequence = resp.oldestSequence;
                                              xSemaphoreGive(mutex);
                                          }
                                          streaming = ring != nullptr;
                                          r.Send(fl::ResponseLogHistory::Encode, resp);
                                          if (task)
                                              xTaskNotifyGive(task);
                                          return webmanager::eMessageReceiverResult::OK;
                                      });
        case fl::RequestSetLogLevel::TYPE_ID:
            return webmanager::Handle(reply, frame, frameLen, fl::RequestSetLogLevel::Decode,
                                      [](WsProtocol::Responder &r, const fl::RequestSetLogLevel::Payload &req)
                                      {
                                          const char *tag = (req.tag && req.tag[0]) ? req.tag : "*";
                                          esp_log_level_set(tag, (esp_log_level_t)req.level);
                                          r.Send(fl::ResponseSetLogLevel::Encode, fl::ResponseSetLogLevel::Payload{});
                                          return webmanager::eMessageReceiverResult::OK;
                                      });
        default:
            return webmanager::eMessageReceiverResult::NOT_FOR_ME;
        }
    }

    // Zerlegt eine Zeile im Format von ESP-IDF (Log V1, mit oder ohne Farben):
    // "\033[0;33mW (1234) TAG: Text\033[0m\n". Andere Zeilen: Level Info, ohne Tag, ganzer Text.
    // Oeffentlich fuer Tests.
    static void Parse(const char *line, Entry &e)
    {
        const char *p = line;
        if (*p == '\033')
        {
            const char *m = strchr(p, 'm');
            if (m)
                p = m + 1;
        }
        e.level = (uint8_t)ESP_LOG_INFO;
        e.tag[0] = '\0';
        const char *text = p;
        uint8_t level = levelOf(*p);
        if (level != ESP_LOG_NONE && p[1] == ' ' && p[2] == '(')
        {
            const char *close = strstr(p, ") ");
            if (close)
            {
                e.level = level;
                const char *tagStart = close + 2;
                const char *colon = strstr(tagStart, ": ");
                if (colon)
                {
                    size_t n = (size_t)(colon - tagStart);
                    if (n > TAG_LEN - 1)
                        n = TAG_LEN - 1;
                    memcpy(e.tag, tagStart, n);
                    e.tag[n] = '\0';
                    text = colon + 2;
                }
                else
                {
                    text = tagStart;
                }
            }
        }
        size_t len = strlen(text);
        auto trimNewlines = [&]()
        { while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--; };
        trimNewlines();
        if (len >= 4 && memcmp(text + len - 4, "\033[0m", 4) == 0)
            len -= 4;
        trimNewlines();
        if (len > TEXT_LEN - 1)
            len = TEXT_LEN - 1;
        memcpy(e.text, text, len);
        e.text[len] = '\0';
    }

private:
    static inline FirmwareLogPlugin *instance{nullptr};
    static inline vprintf_like_t previous{nullptr};

    const size_t capacity;
    Entry *ring{nullptr};
    SemaphoreHandle_t mutex{nullptr};
    TaskHandle_t task{nullptr};
    webmanager::iWebmanagerCallback *callback{nullptr};
    uint32_t nextSequence{0};  // unter mutex
    uint32_t sendSequence{0};  // unter mutex
    std::atomic<bool> streaming{false};
    char lineBuffer[TEXT_LEN + TAG_LEN + 48]; // unter mutex

    static uint8_t levelOf(char c)
    {
        switch (c)
        {
        case 'E': return ESP_LOG_ERROR;
        case 'W': return ESP_LOG_WARN;
        case 'I': return ESP_LOG_INFO;
        case 'D': return ESP_LOG_DEBUG;
        case 'V': return ESP_LOG_VERBOSE;
        default: return ESP_LOG_NONE;
        }
    }

    uint32_t oldestSequence() const
    {
        return nextSequence > capacity ? nextSequence - (uint32_t)capacity : 0;
    }

    static int vprintfHook(const char *fmt, va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        int ret = previous ? previous(fmt, args) : vprintf(fmt, args);
        if (instance)
            instance->capture(fmt, copy);
        va_end(copy);
        return ret;
    }

    void capture(const char *fmt, va_list args)
    {
        if (xPortInIsrContext() || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
            return;
        if (task && xTaskGetCurrentTaskHandle() == task)
            return; // eigene Zeilen (Senden) nicht puffern
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE)
            return;
        vsnprintf(lineBuffer, sizeof(lineBuffer), fmt, args);
        Entry &e = ring[nextSequence % capacity];
        Parse(lineBuffer, e);
        e.sequence = nextSequence++;
        e.uptimeMs = esp_log_timestamp();
        if (nextSequence - sendSequence > capacity)
            sendSequence = nextSequence - (uint32_t)capacity; // die aeltesten sind ueberschrieben
        xSemaphoreGive(mutex);
        if (streaming && task)
            xTaskNotifyGive(task);
    }

    static void taskMain(void *arg)
    {
        static_cast<FirmwareLogPlugin *>(arg)->run();
    }

    void run()
    {
        namespace fl = WsProtocol::firmwarelog;
        uint8_t frame[TAG_LEN + TEXT_LEN + 32]; // Kopf 8 + Zahlen 9 + Laengen der Zeichenketten
        Entry e;
        bool backlog = false;
        while (true)
        {
            ulTaskNotifyTake(pdTRUE, backlog ? pdMS_TO_TICKS(50) : pdMS_TO_TICKS(1000));
            // in kleinen Portionen, damit die Warteschlange des HTTP-Servers nicht ueberlaeuft
            for (int i = 0; i < 16 && streaming && callback; i++)
            {
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
                    break;
                bool any = sendSequence != nextSequence;
                if (any)
                    e = ring[sendSequence % capacity];
                xSemaphoreGive(mutex);
                if (!any)
                    break;
                fl::NotifyLogMessage::Payload msg{};
                msg.sequence = e.sequence;
                msg.level = (fl::LogLevel)e.level;
                msg.uptimeMs = e.uptimeMs;
                msg.tag = e.tag;
                msg.text = e.text;
                size_t len = fl::NotifyLogMessage::Encode(msg, frame, sizeof(frame));
                esp_err_t err = len ? callback->SendRawAsync(frame, len) : ESP_OK;
                if (err == ESP_ERR_INVALID_STATE)
                {
                    streaming = false; // keine Verbindung mehr: bis zur naechsten Anforderung
                    break;
                }
                if (err != ESP_OK)
                    break; // Warteschlange voll: spaeter erneut
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    if (sendSequence == e.sequence)
                        sendSequence++;
                    xSemaphoreGive(mutex);
                }
            }
            backlog = false;
            if (streaming && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                backlog = sendSequence != nextSequence;
                xSemaphoreGive(mutex);
            }
        }
    }
};

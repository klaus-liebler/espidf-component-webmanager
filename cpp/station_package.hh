#pragma once
// Stationspaket (s. sensact docs/concept-modular-firmware.md, "Web-UI als Teil des Stationspakets"):
// eine Datei mit Kopf und Abschnitten, die eine Station als Ganzes uebernimmt. Schritt 1 enthaelt
// nur die Web-UI; die Konfiguration (Hardware, Apps) kommt mit der datengetriebenen Basis als
// weitere Abschnittsart hinzu, ohne das Format zu aendern.
//
// Aufbau (little endian):
//   PackageHeader (128 Byte)
//   PackageSection[sectionCount] (je 48 Byte)
//   Nutzdaten der Abschnitte (offset/size relativ zum Dateianfang)
//
// Das Geraet nimmt ein Paket nur an, wenn nodeId und compatId zu seiner Firmware passen.
// compatId ist ein SHA-256 ueber alles, was Firmware und Web-UI gemeinsam aus dem Hausmodell und
// dem ws-Protokoll erzeugt bekommen (berechnet vom Builder, in die Firmware als STATION_COMPAT_ID
// eingebaut). Pakete mit anderem Modell/Protokoll werden abgelehnt.
#include <cstdint>
#include <cstring>

namespace webmanager::station_package
{
    constexpr char MAGIC[8] = {'S', 'N', 'S', 'C', 'T', 'P', 'K', 'G'};
    constexpr uint16_t FORMAT_VERSION = 1;
    constexpr uint16_t MAX_SECTIONS = 8;

    enum class SectionType : uint16_t
    {
        WEBUI_HTML_BR = 1, // komplette Web-UI (index.html, brotli-komprimiert)
        // HARDWARE_CONFIG = 2, APP_CONFIG = 3: kommen mit der datengetriebenen Basis
    };

#pragma pack(push, 1)
    struct PackageHeader
    {
        char magic[8];
        uint16_t formatVersion;
        uint16_t sectionCount;
        uint32_t headerSize; // sizeof(PackageHeader) + sectionCount * sizeof(PackageSection)
        char nodeId[32];     // nullterminiert/aufgefuellt
        uint8_t compatId[32];
        uint64_t createdUnix;
        char label[40]; // lesbare Beschreibung, nullterminiert/aufgefuellt
    };

    struct PackageSection
    {
        uint16_t type;
        uint16_t reserved;
        uint32_t offset;
        uint32_t size;
        uint32_t reserved2;
        uint8_t sha256[32];
    };
#pragma pack(pop)

    static_assert(sizeof(PackageHeader) == 128);
    static_assert(sizeof(PackageSection) == 48);

    enum class CheckResult
    {
        OK,
        BAD_MAGIC,
        BAD_VERSION,
        BAD_LAYOUT,
        WRONG_NODE,
        WRONG_COMPAT,
    };

    inline const char *ToString(CheckResult r)
    {
        switch (r)
        {
        case CheckResult::OK: return "OK";
        case CheckResult::BAD_MAGIC: return "Keine Stationspaket-Datei";
        case CheckResult::BAD_VERSION: return "Paketformat wird von dieser Firmware nicht unterstuetzt";
        case CheckResult::BAD_LAYOUT: return "Paket beschaedigt (Kopf/Abschnitte)";
        case CheckResult::WRONG_NODE: return "Paket ist fuer eine andere Station";
        case CheckResult::WRONG_COMPAT: return "Paket passt nicht zur Firmware (anderes Hausmodell/Protokoll) - erst die passende Firmware aufspielen";
        }
        return "?";
    }

    // Prueft Kopf und Abschnittstabelle (ohne Pruefsummen der Nutzdaten).
    inline CheckResult CheckHeader(const PackageHeader &h, const PackageSection *sections, uint32_t fileSize,
                                   const char *expectedNodeId, const uint8_t *expectedCompatId)
    {
        if (memcmp(h.magic, MAGIC, sizeof(MAGIC)) != 0) return CheckResult::BAD_MAGIC;
        if (h.formatVersion != FORMAT_VERSION) return CheckResult::BAD_VERSION;
        if (h.sectionCount == 0 || h.sectionCount > MAX_SECTIONS) return CheckResult::BAD_LAYOUT;
        if (h.headerSize != sizeof(PackageHeader) + h.sectionCount * sizeof(PackageSection) || h.headerSize > fileSize) return CheckResult::BAD_LAYOUT;
        for (uint16_t i = 0; i < h.sectionCount; i++)
        {
            const auto &s = sections[i];
            if (s.offset < h.headerSize || s.size > fileSize || s.offset > fileSize - s.size) return CheckResult::BAD_LAYOUT;
        }
        if (strncmp(h.nodeId, expectedNodeId, sizeof(h.nodeId)) != 0) return CheckResult::WRONG_NODE;
        if (memcmp(h.compatId, expectedCompatId, sizeof(h.compatId)) != 0) return CheckResult::WRONG_COMPAT;
        return CheckResult::OK;
    }

    // "0a1b..." (64 Hex-Zeichen) -> 32 Byte; false bei ungueltiger Eingabe
    inline bool ParseHex32(const char *hex, uint8_t out[32])
    {
        if (!hex || strlen(hex) != 64) return false;
        for (int i = 0; i < 32; i++)
        {
            auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = (uint8_t)(hi << 4 | lo);
        }
        return true;
    }
}

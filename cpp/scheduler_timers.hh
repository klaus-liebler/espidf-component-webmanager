#pragma once
#include <cstdio>
#include <ctime>
#include <map>
#include <vector>
#include <string>
#include <type_traits>
#include "wsprotocol_cpp/ws_protocol.hh"
#include "esp_random.h"
#include "sunsetsunrise.hh"
#define TAG "SCHEDULER"
#include "esp_log.h"
namespace scheduler
{
    class aTimer
    {
    protected:
        std::string name;
    public:
        std::string GetName(){
            return name;
        }
        aTimer(std::string name):name(name){}

        virtual ~aTimer(){}

        virtual uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const = 0;

        static aTimer* BuildFromBlob(uint8_t* data){return nullptr;}

        virtual void NewDayHasBegun(uint32_t julianDay, tms_t todaysSunrise, tms_t  todaysSunset){return;}

        virtual WsProtocol::scheduler::ScheduleType GetScheduleType() const = 0;

        // Schreibt NUR die getaggte Variante (classId + Klassenfelder) nach 'dest' -- kein Name, kein
        // aeusserer Schedule-Rahmen. Der aeussere Rahmen (Schedule::Payload{name, scheduleData,
        // scheduleDataSize}) wird vom jeweiligen Aufrufer selbst gebaut (FillNvsBlob hier, oder direkt
        // in scheduler.hh fuer RequestSchedulerSave/ResponseSchedulerOpen), weil deren Encode-Empfaenger
        // (NVS-Blob vs. Message-Payload) unterschiedliche Puffer-Ziele haben.
        virtual size_t EncodeScheduleVariant(uint8_t* dest, size_t pos, size_t dest_size) const = 0;

        void RenameAndFillNvsBlob(std::string newName, uint8_t* data, size_t& len_in_out){
            this->name=newName;
            FillNvsBlob(data, len_in_out);
        }

        void FillNvsBlob(uint8_t* data, size_t& len_in_out){
            uint8_t variant_scratch[128];
            size_t variantLen = EncodeScheduleVariant(variant_scratch, 0, sizeof(variant_scratch));
            WsProtocol::scheduler::Schedule::Payload payload{};
            payload.name = name.c_str();
            payload.scheduleData = variant_scratch;
            payload.scheduleDataSize = variantLen;
            size_t written = WsProtocol::scheduler::Schedule::Encode(payload, data, 0, len_in_out);
            len_in_out = written;
        }
    };

    // Gemeinsame Basis fuer die 5 fest verdrahteten "Predefined"-Singletons (ALWAYS/NEVER/DAILY_6_22/
    // WORKING_DAYS_7_18/TestEvenMinutesOnOddMinutesOff) -- spart die vormals 5x identisch kopierten
    // GetScheduleType()/EncodeScheduleVariant()-Ueberschreibungen.
    class aPredefinedTimer : public aTimer
    {
    public:
        aPredefinedTimer(std::string name):aTimer(name){}

        WsProtocol::scheduler::ScheduleType GetScheduleType() const override
        {
            return WsProtocol::scheduler::ScheduleType::PREDEFINED;
        }

        size_t EncodeScheduleVariant(uint8_t* dest, size_t pos, size_t dest_size) const override
        {
            WsProtocol::scheduler::Predefined::Payload item{};
            return WsProtocol::scheduler::AppendScheduleSchedulePredefinedElement(item, dest, pos, dest_size);
        }
    };

    class cALWAYS: public aPredefinedTimer
    {
        public:
        cALWAYS(std::string name):aPredefinedTimer(name){}
        protected:
        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            return UINT16_MAX;
        }
    } ALWAYS("ALWAYS");

    class cNEVER: public aPredefinedTimer
    {
        public:
        cNEVER(std::string name):aPredefinedTimer(name){}
        protected:
        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            return 0;
        }
    } NEVER("NEVER");

    class cDAILY_6_22: public aPredefinedTimer
    {
        public:
        cDAILY_6_22(std::string name):aPredefinedTimer(name){}
        protected:
        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            return (h >= 6 && h < 22)?UINT16_MAX:0;
        }
    } DAILY_6_22("DAILY_6_22");

    class cWORKING_DAYS_7_18: public aPredefinedTimer
    {
        public:
        cWORKING_DAYS_7_18(std::string name):aPredefinedTimer(name){}
        protected:
        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            if (d > 5)
                return 0;
            return (h >= 7 && h < 18)?UINT16_MAX:0;
        }
    } WORKING_DAYS_7_18("WORKING_DAYS_7_18");


    class cTestEvenMinutesOnOddMinutesOff: public aPredefinedTimer
    {
        public:
        cTestEvenMinutesOnOddMinutesOff(std::string name):aPredefinedTimer(name){}
        protected:
        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            return (m%2==0)?UINT16_MAX:0;
        }
    } TestEvenMinutesOnOddMinutesOff("TestEvenOdd");

    class OneWeekIn15MinutesTimer :public aTimer{
        private:
        std::array<uint8_t, 84> data;
        public:
        OneWeekIn15MinutesTimer(std::string name, std::array<uint8_t, 84> data):aTimer(name), data(data){}

        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            uint8_t twoHours=data[d*12+(h>>1)];
            uint8_t fifteenMinutesSlot = 4*(h&1)+(m/15);
            return (twoHours&(1<<fifteenMinutesSlot))?UINT16_MAX:0;
        }

        static aTimer* BuildFromWsProtocol(std::string name, const WsProtocol::scheduler::OneWeekIn15Minutes::Payload &owi15m){
            std::array<uint8_t, 84> data;
            std::memcpy(data.data(), owi15m.data.v, 84);
            return new scheduler::OneWeekIn15MinutesTimer(name, data);
        }

        WsProtocol::scheduler::ScheduleType GetScheduleType() const override
        {
            return WsProtocol::scheduler::ScheduleType::ONE_WEEK_IN_15_MINUTES;
        }

        size_t EncodeScheduleVariant(uint8_t* dest, size_t pos, size_t dest_size) const override
        {
            WsProtocol::scheduler::OneWeekIn15Minutes::Payload item{};
            std::memcpy(item.data.v, data.data(), 84);
            return WsProtocol::scheduler::AppendScheduleScheduleOneWeekIn15MinutesElement(item, dest, pos, dest_size);
        }
    };

    class SunRandomTimer:public aTimer{
        private:
            const float offsetHours{0};
            const float randomHours{0};
            time_t todaysStart{0};
            time_t todaysEnd{0};

        public:
        SunRandomTimer(std::string the_name, float offsetHours, float randomHours):aTimer(the_name), offsetHours(offsetHours), randomHours(randomHours){

        }

        WsProtocol::scheduler::ScheduleType GetScheduleType() const override
        {
            return WsProtocol::scheduler::ScheduleType::SUN_RANDOM;
        }

        size_t EncodeScheduleVariant(uint8_t* dest, size_t pos, size_t dest_size) const override
        {
            WsProtocol::scheduler::SunRandom::Payload item{};
            item.offsetMinutes = (uint16_t)(offsetHours*60);
            item.randomMinutes = (uint16_t)(randomHours*60);
            return WsProtocol::scheduler::AppendScheduleScheduleSunRandomElement(item, dest, pos, dest_size);
        }

        static aTimer* BuildFromWsProtocol(std::string name, const WsProtocol::scheduler::SunRandom::Payload &sr){
            return new scheduler::SunRandomTimer(name, sr.offsetMinutes / 60.0, sr.randomMinutes / 60.0);
        }

        uint16_t GetCurrentValue(time_t unixSecs, int d, int h, int m, int s) const override
        {
            return (unixSecs>=todaysStart&& unixSecs<=todaysEnd)?UINT16_MAX:0;
        }



        void NewDayHasBegun(uint32_t julianDay, time_t todaysSunriseUnixSecs, time_t todaysSunsetUnixSecs) override{


            float randomSunriseHours = (((float)esp_random()/(float)UINT32_MAX)*2*randomHours)-randomHours;
            float randomSunsetHours = (((float)esp_random()/(float)UINT32_MAX)*2*randomHours)-randomHours;
            todaysStart = todaysSunriseUnixSecs+(offsetHours+randomSunriseHours)*60*60;
            todaysEnd = todaysSunsetUnixSecs-(offsetHours+randomSunsetHours)*60*60;
            return;
        }
    };

    class Builder{
        public:
        // Deckt (wie schon die vormalige Flatbuffers-Fassung) nur OneWeekIn15Minutes/SunRandom ab --
        // ein aus NVS geladenes "Predefined"-Schedule kann nicht rekonstruiert werden (die 5 Predefined-
        // Varianten sind fest verdrahtete Singletons, kein generischer Predefined-aTimer-Typ), faellt
        // also auf nullptr zurueck, exakt wie zuvor.
        static aTimer* BuildFromSchedule(const WsProtocol::scheduler::Schedule::Payload &schedule){
            aTimer* result = nullptr;
            WsProtocol::scheduler::DecodeScheduleScheduleElements(schedule.scheduleData, schedule.scheduleDataSize, 1,
                [&](auto &variant) {
                    using T = std::decay_t<decltype(variant)>;
                    if constexpr (std::is_same_v<T, WsProtocol::scheduler::OneWeekIn15Minutes::Payload>) {
                        result = OneWeekIn15MinutesTimer::BuildFromWsProtocol(schedule.name, variant);
                    } else if constexpr (std::is_same_v<T, WsProtocol::scheduler::SunRandom::Payload>) {
                        result = SunRandomTimer::BuildFromWsProtocol(schedule.name, variant);
                    }
                });
            return result;
        }
    };



}
#undef TAG

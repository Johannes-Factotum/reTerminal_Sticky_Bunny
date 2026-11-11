#pragma once

#include <cstdint>

enum class StickyBuzzerPattern : uint8_t {
    None,
    Alarm,
    Hatch,
    Eat,
    CooShy,
    CooWarm,
    Play,
    PlayTired,
    VoiceShy,
    VoiceWarm,
    VoiceClose,
    VoiceHungry,
    VoiceTired,
    VoiceSad,
    VoiceFoodie,
    VoiceAffectionate,
    VoiceActive,
    Sleep,
    Wake,
    PowerSleep,
    FocusStart,
    TaskDone,
    SessionComplete,
};

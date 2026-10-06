#pragma once
#include "RCBListPacket.h"

namespace CardFilter {
    enum class SpStat : int {
        Upgrade,
        Level,
        Attack,
        Defence,
        Element,
        HpMp,
        PvE,
        PvP,
        FireRes,
        WaterRes,
        LightRes,
        ShadowRes,
        Count,
    };
    constexpr int SpStatCount = static_cast<int>(SpStat::Count);

    const wchar_t* SpStatName(SpStat Stat);
    int SpStatValue(const Packet::RCBListSPInfo& Sp, SpStat Stat);

    struct SpFilter {
        int Min[SpStatCount] = {};
    };

    bool IsActive(const SpFilter& Rules);
    bool Matches(const SpFilter& Rules, const Packet::RCBListSPInfo& Sp);

    constexpr int MaxRank = 7;

    int SkillRank(const Packet::RCBListPSPInfo& Psp, int Skill);
}

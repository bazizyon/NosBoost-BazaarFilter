#include "CardFilter.h"
#include <algorithm>

namespace CardFilter {
    const wchar_t* SpStatName(const SpStat Stat) {
        switch (Stat) {
            case SpStat::Upgrade:   return L"Upgrade (+)";
            case SpStat::Level:     return L"Level";
            case SpStat::Attack:    return L"Attack";
            case SpStat::Defence:   return L"Defence";
            case SpStat::Element:   return L"Element";
            case SpStat::HpMp:      return L"HP / MP";
            case SpStat::PvE:       return L"PvE (atk + ele)";
            case SpStat::PvP:       return L"PvP (atk + def)";
            case SpStat::FireRes:   return L"Fire resistance";
            case SpStat::WaterRes:  return L"Water resistance";
            case SpStat::LightRes:  return L"Light resistance";
            case SpStat::ShadowRes: return L"Shadow resistance";
            default:                return L"?";
        }
    }

    int SpStatValue(const Packet::RCBListSPInfo& Sp, const SpStat Stat) {
        switch (Stat) {
            case SpStat::Upgrade:   return Sp.upgradingGrade;
            case SpStat::Level:     return Sp.level;
            case SpStat::Attack:    return Sp.attackPerf;
            case SpStat::Defence:   return Sp.defencePerf;
            case SpStat::Element:   return Sp.elementPerf;
            case SpStat::HpMp:      return Sp.hpmpPerf;
            case SpStat::PvE:       return Sp.attackPerf + Sp.elementPerf;
            case SpStat::PvP:       return Sp.attackPerf + Sp.defencePerf;
            case SpStat::FireRes:   return Sp.fireResPerf;
            case SpStat::WaterRes:  return Sp.waterResPerf;
            case SpStat::LightRes:  return Sp.lightResPerf;
            case SpStat::ShadowRes: return Sp.shadowResPerf;
            default:                return 0;
        }
    }

    bool IsActive(const SpFilter& Rules) {
        return std::any_of(std::begin(Rules.Min), std::end(Rules.Min), [](const int Min) { return Min > 0; });
    }

    bool Matches(const SpFilter& Rules, const Packet::RCBListSPInfo& Sp) {
        for (int i = 0; i < SpStatCount; i++) {
            if (Rules.Min[i] > 0 && SpStatValue(Sp, static_cast<SpStat>(i)) < Rules.Min[i]) return false;
        }
        return true;
    }

    int SkillRank(const Packet::RCBListPSPInfo& Psp, const int Skill) {
        switch (Skill) {
            case 0:  return Psp.skill1tier;
            case 1:  return Psp.skill2tier;
            case 2:  return Psp.skill3tier;
            default: return 0;
        }
    }
}

#include "ShellFilter.h"
#include <algorithm>
#include <variant>

namespace ShellFilter {
    namespace {
        int ValueOf(const std::vector<Packet::RCBListShellOption>& Options, const int Id) {
            int Best = -1;
            for (const auto& Option : Options) {
                if (Option.id == Id) Best = std::max(Best, Option.value);
            }
            return Best;
        }

        bool Passes(const Group& Rules, const std::vector<Packet::RCBListShellOption>& Options) {
            switch (Rules.Type) {
                case GroupType::AllOf:
                    return std::all_of(Rules.Criteria.begin(), Rules.Criteria.end(), [&](const Criterion& Rule) {
                        const int Value = ValueOf(Options, Rule.Id);
                        return Value >= 0 && Value >= Rule.Value;
                    });
                case GroupType::Count: {
                    const auto Present = std::count_if(Rules.Criteria.begin(), Rules.Criteria.end(), [&](const Criterion& Rule) {
                        const int Value = ValueOf(Options, Rule.Id);
                        return Value >= 0 && Value >= Rule.Value;
                    });
                    return Present >= std::max(1, Rules.CountMin) && (Rules.CountMax <= 0 || Present <= Rules.CountMax);
                }
                case GroupType::Weighted: {
                    long long Sum = 0;
                    for (const Criterion& Rule : Rules.Criteria) {
                        const int Value = ValueOf(Options, Rule.Id);
                        if (Value > 0) Sum += static_cast<long long>(Rule.Value) * Value;
                    }
                    return Sum >= Rules.WeightMin;
                }
            }
            return false;
        }
    }

    bool IsActive(const Filter& Rules) {
        return std::any_of(Rules.Groups.begin(), Rules.Groups.end(),
                           [](const Group& Rules) { return !Rules.Criteria.empty(); });
    }

    bool ShellOptionsOf(const Packet::RCBListEntry& Entry, ShellAttributes::Group& OutGroup,
                        const std::vector<Packet::RCBListShellOption>*& OutOptions) {
        if (const auto* Weapon = std::get_if<Packet::RCBListWeaponInfo>(&Entry.itemInfo)) {
            OutGroup = ShellAttributes::Group::Weapon;
            OutOptions = &Weapon->shellOptions;
            return true;
        }
        if (const auto* Armor = std::get_if<Packet::RCBListArmorInfo>(&Entry.itemInfo)) {
            OutGroup = ShellAttributes::Group::Armor;
            OutOptions = &Armor->shellOptions;
            return true;
        }
        if (const auto* Fairy = std::get_if<Packet::RCBListFairyInfo>(&Entry.itemInfo)) {
            OutGroup = ShellAttributes::Group::Fairy;
            OutOptions = &Fairy->shellOptions;
            return true;
        }
        return false;
    }

    std::wstring Summary(const Filter& Rules, const std::vector<Packet::RCBListShellOption>& Options) {
        std::wstring Text;
        for (const Group& Rules : Rules.Groups) {
            if (Rules.Criteria.empty()) continue;
            std::wstring Part;
            if (Rules.Type == GroupType::Count) {
                const auto Present = std::count_if(Rules.Criteria.begin(), Rules.Criteria.end(), [&](const Criterion& Rule) {
                    const int Value = ValueOf(Options, Rule.Id);
                    return Value >= 0 && Value >= Rule.Value;
                });
                Part = std::to_wstring(Present) + L"/" + std::to_wstring(Rules.Criteria.size());
            } else if (Rules.Type == GroupType::Weighted) {
                long long Sum = 0;
                for (const Criterion& Rule : Rules.Criteria) {
                    const int Value = ValueOf(Options, Rule.Id);
                    if (Value > 0) Sum += static_cast<long long>(Rule.Value) * Value;
                }
                Part = L"score " + std::to_wstring(Sum);
            }
            if (Part.empty()) continue;
            if (!Text.empty()) Text += L"  ";
            Text += Part;
        }
        return Text;
    }

    bool Matches(const Filter& Rules, const std::vector<Packet::RCBListShellOption>& Options) {
        if (!IsActive(Rules)) return false;
        return std::all_of(Rules.Groups.begin(), Rules.Groups.end(), [&](const Group& Rules) {
            return Rules.Criteria.empty() || Passes(Rules, Options);
        });
    }
}

#pragma once
#include <string>
#include <vector>
#include "RCBListPacket.h"
#include "ShellAttributes.h"

namespace ShellFilter {
    enum class GroupType : int {
        AllOf = 0,
        Count = 1,
        Weighted = 2,
    };

    struct Criterion {
        int Id;
        int Value;
    };

    struct Group {
        GroupType Type = GroupType::AllOf;
        std::vector<Criterion> Criteria;
        int CountMin = 1;
        int CountMax = 0;
        int WeightMin = 0;
    };

    struct Filter {
        std::vector<Group> Groups;
    };

    bool IsActive(const Filter& Rules);

    bool ShellOptionsOf(const Packet::RCBListEntry& Entry, ShellAttributes::Group& OutGroup,
                        const std::vector<Packet::RCBListShellOption>*& OutOptions);

    bool Matches(const Filter& Rules, const std::vector<Packet::RCBListShellOption>& Options);

    std::wstring Summary(const Filter& Rules, const std::vector<Packet::RCBListShellOption>& Options);
}

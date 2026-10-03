#pragma once
#include <string>
#include <vector>

namespace ShellAttributes {
    enum class Group {
        Weapon,
        Armor,
        Fairy,
    };

    struct Attribute {
        std::wstring Name;
        int Id;
        Group Kind;
    };

    const std::vector<Attribute>& All();
    std::vector<const Attribute*> OfGroup(Group Kind);
    const Attribute* Find(Group Kind, int Id);
}

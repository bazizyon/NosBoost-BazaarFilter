#define NOMINMAX
#include "ShellAttributes.h"
#include <windows.h>
#include <cstring>

namespace ShellAttributes {
    namespace {
        constexpr uint8_t TablePattern[] = {
            0x53, 0x8B, 0xC6,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x4C, 0x82, 0xFC,
            0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0x12, 0x8D, 0x45, 0xF4, 0xE8,
        };
        constexpr char TableMask[] = "xxxx????xx????xxxxxx????xxxxxx";
        constexpr int TableOperandOffset = 0x0A;
        constexpr int MaxEntries = 200;

        bool IsReadable(const uintptr_t Address, const size_t Size) {
            MEMORY_BASIC_INFORMATION Info;
            if (Address < 0x10000 || !VirtualQuery(reinterpret_cast<void*>(Address), &Info, sizeof(Info))) return false;
            constexpr DWORD Readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                                     | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (Info.State != MEM_COMMIT || (Info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(Info.Protect & Readable)) {
                return false;
            }
            return Address + Size <= reinterpret_cast<uintptr_t>(Info.BaseAddress) + Info.RegionSize;
        }

        uintptr_t FindInGame(const uint8_t* Pattern, const char* Mask) {
            const auto Base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + reinterpret_cast<const IMAGE_DOS_HEADER*>(Base)->e_lfanew);
            const IMAGE_SECTION_HEADER* Section = IMAGE_FIRST_SECTION(Nt);
            const size_t Length = std::strlen(Mask);
            for (WORD s = 0; s < Nt->FileHeader.NumberOfSections; s++, Section++) {
                if (!(Section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
                const auto* Start = reinterpret_cast<const uint8_t*>(Base + Section->VirtualAddress);
                for (size_t i = 0; i + Length <= Section->Misc.VirtualSize; i++) {
                    size_t j = 0;
                    while (j < Length && (Mask[j] == '?' || Start[i + j] == Pattern[j])) j++;
                    if (j == Length) return reinterpret_cast<uintptr_t>(Start + i);
                }
            }
            return 0;
        }

        std::wstring Sanitize(const char* Raw) {
            std::string Text(Raw);
            for (const char* Token : {"%s", "<NEW_TYPE>", "<0>"}) {
                for (size_t At; (At = Text.find(Token)) != std::string::npos;) Text.erase(At, std::strlen(Token));
            }
            for (size_t At; (At = Text.find("%%")) != std::string::npos;) Text.replace(At, 2, "%");
            while (!Text.empty() && Text.back() == ' ') Text.pop_back();

            const int Length = MultiByteToWideChar(1252, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
            std::wstring Wide(Length, L'\0');
            MultiByteToWideChar(1252, 0, Text.data(), static_cast<int>(Text.size()), Wide.data(), Length);
            return Wide;
        }

        std::vector<Attribute> Load() {
            std::vector<Attribute> Result;
            const uintptr_t Match = FindInGame(TablePattern, TableMask);
            if (!Match) return Result;
            const uintptr_t Global = *reinterpret_cast<const uintptr_t*>(Match + TableOperandOffset);
            if (!IsReadable(Global, 4)) return Result;
            const uintptr_t Table = *reinterpret_cast<const uintptr_t*>(Global);
            if (!IsReadable(Table, MaxEntries * sizeof(char*))) return Result;
            const auto* const* Strings = reinterpret_cast<const char* const*>(Table);

            Group Kind = Group::Weapon;
            int GroupStart = 0;
            for (int i = 0; i < MaxEntries; i++) {
                const char* Raw = Strings[i];
                if (!Raw || !IsReadable(reinterpret_cast<uintptr_t>(Raw), 1)) continue;
                const std::string_view Name(Raw);
                if (Name.starts_with("C-")) break;
                if (Kind == Group::Weapon && Name.starts_with("Enhanced Melee Defense")) {
                    Kind = Group::Armor;
                    GroupStart = i;
                } else if (Kind == Group::Armor && Name.starts_with("Increases HP by")) {
                    Kind = Group::Fairy;
                    GroupStart = i;
                }
                Result.push_back({Sanitize(Raw), i - GroupStart + 1, Kind});
            }
            return Result;
        }
    }

    const std::vector<Attribute>& All() {
        static const std::vector<Attribute> Attributes = Load();
        return Attributes;
    }

    std::vector<const Attribute*> OfGroup(const Group Kind) {
        std::vector<const Attribute*> Result;
        for (const Attribute& Entry : All()) {
            if (Entry.Kind == Kind) Result.push_back(&Entry);
        }
        return Result;
    }

    const Attribute* Find(const Group Kind, const int Id) {
        for (const Attribute& Entry : All()) {
            if (Entry.Kind == Kind && Entry.Id == Id) return &Entry;
        }
        return nullptr;
    }
}

#define NOMINMAX
#include <windows.h>

#include "ModContract.h"
#include "WidgetKit.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWEditWidget.h"
#include "TEWButtonWidget.h"
#include "TEWCustomPanelWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TNTConsignmentWidget.h"
#include "RCBListPacket.h"
#include "ShellAttributes.h"
#include "ShellFilter.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace {
    using ShellAttributes::Group;

    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWButtonWidget::ClassName, TEWButtonWidget::Version, TEWButtonWidget::ExpectedSize},
        {TEWEditWidget::ClassName, TEWEditWidget::Version, TEWEditWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTConsignmentWidget::ClassName, TNTConsignmentWidget::Version, TNTConsignmentWidget::ExpectedSize},
    };

    constexpr uint16_t CategoryWeapon = 2;
    constexpr uint16_t CategoryArmour = 3;

    constexpr uint16_t PanelWidth = 270;
    constexpr int16_t Pad = 14;
    constexpr int16_t OptionsTop = 42;
    constexpr int16_t ContentTop = 90;
    constexpr int16_t TextNudge = -1;
    constexpr int16_t LineHeight = 22;
    constexpr int16_t HeaderHeight = 24;
    constexpr int16_t GroupGap = 8;
    constexpr int16_t ValueBoxWidth = 44;
    constexpr int16_t RemoveSize = 20;
    constexpr int16_t ContentWidth = PanelWidth - 2 * Pad;
    constexpr int16_t RemoveX = PanelWidth - Pad - RemoveSize;
    constexpr int16_t ValueX = RemoveX - 6 - ValueBoxWidth;
    constexpr int MaxGroups = 4;
    constexpr int MaxStatsPerGroup = 6;
    constexpr int ResultsPerPage = 10;
    constexpr int PagesPerSearch = 3;
    constexpr uint16_t SkinTop = 58;

    const Color PlainColor(255, 255, 255, 255);
    const Color DimmedColor(70, 255, 255, 255);
    const Color FallbackTint(255, 255, 60, 60);
    const Color HeaderColor(255, 160, 160, 160);

    const ModHost* CachedHost = nullptr;
    TLBSWidget* AttachedRoot = nullptr;
    TNTConsignmentWidget* Bazaar = nullptr;
    uintptr_t BazaarVTable = 0;
    TEWGraphicButtonWidget* FilterButton = nullptr;
    TEWLabel* MatchCountLabel = nullptr;
    std::wstring ShownMatchCount;

    std::vector<Packet::RCBListEntry> Listings;

    struct CategoryPanel;
    struct GroupSlot;

    struct StatRow {
        TEWLabel* Name = nullptr;
        WidgetKit::EditBox Value{};
        TEWGraphicButtonWidget* Remove = nullptr;
    };

    struct StatBinding {
        GroupSlot* Slot;
        int Row;
    };

    struct GroupSlot {
        CategoryPanel* Owner = nullptr;
        int Index = 0;
        WidgetKit::Dropdown* Type = nullptr;
        TEWLabel* ParamLabelA = nullptr;
        WidgetKit::EditBox ParamA{};
        TEWLabel* ParamLabelB = nullptr;
        WidgetKit::EditBox ParamB{};
        TEWLabel* ValueHeader = nullptr;
        TEWGraphicButtonWidget* Remove = nullptr;
        StatRow Rows[MaxStatsPerGroup];
        StatBinding Bindings[MaxStatsPerGroup];
        WidgetKit::Dropdown* Picker = nullptr;
    };

    struct CategoryPanel {
        Group Kind = Group::Weapon;
        ShellFilter::Filter Rules;
        std::vector<const ShellAttributes::Attribute*> Attributes;
        TLBSWidget* Container = nullptr;
        GroupSlot Slots[MaxGroups];
        TEWButtonWidget* AddGroup = nullptr;
    };

    CategoryPanel Panels[3];
    TEWCustomPanelWidget* Window = nullptr;
    TLBSWidget* Unavailable = nullptr;
    Color Tint = FallbackTint;

    enum class RowLook : uint8_t {
        Plain,
        Tinted,
        Dimmed,
    };
    RowLook RowLooks[ResultsPerPage] = {};

    struct Settings {
        bool TintMatches = true;
        bool DimOthers = true;
        bool HideOtherBuys = false;
        bool Enabled = true;
    } Config;

    bool BuyHiddenByUs[ResultsPerPage] = {};

    std::string SettingsPath() {
        char Path[MAX_PATH];
        GetModuleFileNameA(nullptr, Path, MAX_PATH);
        const std::string Dir(Path);
        return Dir.substr(0, Dir.find_last_of("\\/") + 1) + "mods\\BazaarFilter.ini";
    }

    void LoadSettings() {
        const std::string Path = SettingsPath();
        Config.TintMatches = GetPrivateProfileIntA("BazaarFilter", "TintMatches", 1, Path.c_str()) != 0;
        Config.DimOthers = GetPrivateProfileIntA("BazaarFilter", "DimOthers", 1, Path.c_str()) != 0;
        Config.HideOtherBuys = GetPrivateProfileIntA("BazaarFilter", "HideOtherBuys", 0, Path.c_str()) != 0;
        Config.Enabled = GetPrivateProfileIntA("BazaarFilter", "Enabled", 1, Path.c_str()) != 0;
    }

    void __cdecl SaveSettings(void*) {
        const std::string Path = SettingsPath();
        WritePrivateProfileStringA("BazaarFilter", "TintMatches", Config.TintMatches ? "1" : "0", Path.c_str());
        WritePrivateProfileStringA("BazaarFilter", "DimOthers", Config.DimOthers ? "1" : "0", Path.c_str());
        WritePrivateProfileStringA("BazaarFilter", "HideOtherBuys", Config.HideOtherBuys ? "1" : "0", Path.c_str());
        WritePrivateProfileStringA("BazaarFilter", "Enabled", Config.Enabled ? "1" : "0", Path.c_str());
    }

    void Attach(TLBSWidget* Parent, TLBSWidget* Child) {
        if (!Parent || !Child) return;
        Child->parent = Parent;
        Parent->childrenList->push_back(Child);
    }

    void Show(TLBSWidget* Widget, const bool Visible) {
        if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
    }

    void Place(TLBSWidget* Widget, const int16_t X, const int16_t Y, const int16_t Width, const int16_t Height) {
        if (Widget) Widget->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + Height)};
    }

    TEWLabel* AddLabel(TLBSWidget* Parent, const int16_t X, const int16_t TextY, const int16_t Width,
                       const uint8_t Alignment, const wchar_t* Text) {
        TEWLabel* Label = Widget::Create<TEWLabel>(CachedHost);
        if (!Label) return nullptr;
        Place(Label, X, static_cast<int16_t>(TextY + TextNudge), Width, 30);
        Label->textAlignment = Alignment;
        Label->pxPerLine = Width;
        Label->SetText(Text);
        Attach(Parent, Label);
        return Label;
    }

    TEWGraphicButtonWidget* AddRemoveButton(TLBSWidget* Parent, const WidgetKit::ClickFn OnClick, void* Argument) {
        TEWGraphicButtonWidget* Button = Widget::Create<TEWGraphicButtonWidget>(CachedHost);
        if (!Button) return nullptr;
        delete[] Button->imageData.atlasFrames;
        Button->imageData.imageName = 1593835585;
        Button->imageData.frameCount = 3;
        Button->imageData.atlasFrames = new AtlasFrame[3]{{244, 2, 20, 20}, {264, 2, 20, 20}, {244, 22, 20, 20}};
        Button->drawMode = 0;
        WidgetKit::SetOnClick(Button, OnClick, Argument);
        Attach(Parent, Button);
        return Button;
    }

    void SetPanelHeight(TEWCustomPanelWidget* Panel, uint16_t Height) {
        Height = std::max<uint16_t>(Height, SkinTop + Panel->nineSliceInfo.heightBot + 1);
        Panel->rect.bottom = static_cast<int16_t>(Panel->rect.top + Height);
        Panel->nineSliceInfo.heightMiddle = Height - SkinTop - Panel->nineSliceInfo.heightBot;
        Panel->nineSliceInfo.posBot = SkinTop + Panel->nineSliceInfo.heightMiddle;
    }

    int ParseNumber(const std::wstring& Text) {
        return static_cast<int>(std::max(0L, std::wcstol(Text.c_str(), nullptr, 10)));
    }

    void SetEditText(TEWEditWidget* Edit, const std::wstring& Text) {
        if (!Edit) return;
        Edit->SetText(Text.c_str());
        Edit->caretIndex = static_cast<int32_t>(Text.size());
    }

    const wchar_t* const GroupTypeNames[] = {L"All of", L"Count", L"Weighted"};

    void MoveTo(TLBSWidget* Widget, const int16_t X, const int16_t Y) {
        if (Widget && (Widget->rect.left != X || Widget->rect.top != Y)) Widget->MoveTo(X, Y);
    }

    void SetEditWidth(WidgetKit::EditBox& Box, const int16_t Width) {
        if (!Box.Frame || Box.Frame->rect.right - Box.Frame->rect.left == Width) return;
        Box.Frame->rect.right = static_cast<int16_t>(Box.Frame->rect.left + Width);
        Box.Frame->nineSliceInfo.widthMiddle = static_cast<uint16_t>(Width - 6);
        Box.Frame->nineSliceInfo.posRight = static_cast<uint16_t>(Width - 3);
        if (Box.Edit) Box.Edit->rect.right = static_cast<int16_t>(Width - 5);
    }

    std::wstring NumberText(const int Value) {
        return Value > 0 ? std::to_wstring(Value) : std::wstring{};
    }

    void Rebind(CategoryPanel& Panel) {
        auto& Groups = Panel.Rules.Groups;
        int16_t Y = 0;
        for (int g = 0; g < MaxGroups; g++) {
            GroupSlot& Slot = Panel.Slots[g];
            const bool Used = g < static_cast<int>(Groups.size());
            for (TLBSWidget* Part : {Slot.Type ? Slot.Type->Widget() : nullptr, static_cast<TLBSWidget*>(Slot.Remove),
                                     static_cast<TLBSWidget*>(Slot.ValueHeader),
                                     Slot.Picker ? Slot.Picker->Widget() : nullptr}) {
                Show(Part, Used);
            }
            if (!Used) {
                for (TLBSWidget* Part : {static_cast<TLBSWidget*>(Slot.ParamLabelA), static_cast<TLBSWidget*>(Slot.ParamA.Frame),
                                         static_cast<TLBSWidget*>(Slot.ParamLabelB), static_cast<TLBSWidget*>(Slot.ParamB.Frame)}) {
                    Show(Part, false);
                }
                for (StatRow& Row : Slot.Rows) {
                    Show(Row.Name, false);
                    Show(Row.Value.Frame, false);
                    Show(Row.Remove, false);
                }
                continue;
            }

            const ShellFilter::Group& Rules = Groups[g];
            const bool IsCount = Rules.Type == ShellFilter::GroupType::Count;
            const bool IsWeighted = Rules.Type == ShellFilter::GroupType::Weighted;

            if (Slot.Type) {
                Slot.Type->SetOpen(false);
                Slot.Type->SetSelected(static_cast<int>(Rules.Type));
                MoveTo(Slot.Type->Widget(), Pad, static_cast<int16_t>(Y + 3));
            }
            Show(Slot.ParamLabelA, IsWeighted);
            Show(Slot.ParamA.Frame, IsCount || IsWeighted);
            Show(Slot.ParamLabelB, IsCount);
            Show(Slot.ParamB.Frame, IsCount);
            constexpr int16_t ParamsX = Pad + 92;
            SetEditWidth(Slot.ParamA, IsWeighted ? 40 : 28);
            MoveTo(Slot.ParamLabelA, ParamsX, static_cast<int16_t>(Y + 4 + TextNudge));
            MoveTo(Slot.ParamA.Frame, static_cast<int16_t>(IsWeighted ? ParamsX + 34 : ParamsX), static_cast<int16_t>(Y + 2));
            MoveTo(Slot.ParamLabelB, ParamsX + 28, static_cast<int16_t>(Y + 4 + TextNudge));
            MoveTo(Slot.ParamB.Frame, ParamsX + 46, static_cast<int16_t>(Y + 2));
            SetEditText(Slot.ParamA.Edit, NumberText(IsCount ? Rules.CountMin : Rules.WeightMin));
            SetEditText(Slot.ParamB.Edit, NumberText(Rules.CountMax));
            if (Slot.ValueHeader) {
                Slot.ValueHeader->SetText(IsWeighted ? L"weight" : L"min");
                MoveTo(Slot.ValueHeader, ValueX, static_cast<int16_t>(Y + 4 + TextNudge));
            }
            MoveTo(Slot.Remove, RemoveX, static_cast<int16_t>(Y + 2));
            Y += HeaderHeight;

            for (int r = 0; r < MaxStatsPerGroup; r++) {
                StatRow& Row = Slot.Rows[r];
                const bool RowUsed = r < static_cast<int>(Rules.Criteria.size());
                Show(Row.Name, RowUsed);
                Show(Row.Value.Frame, RowUsed);
                Show(Row.Remove, RowUsed);
                if (!RowUsed) continue;
                const ShellAttributes::Attribute* Attribute = ShellAttributes::Find(Panel.Kind, Rules.Criteria[r].Id);
                if (Row.Name) Row.Name->SetText(Attribute ? Attribute->Name.c_str() : L"?");
                SetEditText(Row.Value.Edit, NumberText(Rules.Criteria[r].Value));
                MoveTo(Row.Name, Pad, static_cast<int16_t>(Y + 4 + TextNudge));
                MoveTo(Row.Value.Frame, ValueX, Y);
                MoveTo(Row.Remove, RemoveX, Y);
                Y += LineHeight;
            }

            const bool Full = static_cast<int>(Rules.Criteria.size()) >= MaxStatsPerGroup;
            if (Slot.Picker) {
                Slot.Picker->SetOpen(false);
                Show(Slot.Picker->Widget(), !Full);
                MoveTo(Slot.Picker->Widget(), Pad, Y);
            }
            if (!Full) Y += LineHeight;
            Y += GroupGap;
        }

        const bool CanAddGroup = static_cast<int>(Groups.size()) < MaxGroups;
        Show(Panel.AddGroup, CanAddGroup);
        MoveTo(Panel.AddGroup, Pad, Y);
    }

    CategoryPanel& PanelOf(const GroupSlot* Slot) {
        return *Slot->Owner;
    }

    void __cdecl OnTypePicked(void* Argument, const int Index) {
        auto* Slot = static_cast<GroupSlot*>(Argument);
        auto& Groups = PanelOf(Slot).Rules.Groups;
        if (Slot->Index >= static_cast<int>(Groups.size()) || Index < 0 || Index > 2) return;
        ShellFilter::Group& Rules = Groups[Slot->Index];
        const auto NewType = static_cast<ShellFilter::GroupType>(Index);
        if (Rules.Type == NewType) return;
        Rules.Type = NewType;
        for (ShellFilter::Criterion& Rule : Rules.Criteria) Rule.Value = NewType == ShellFilter::GroupType::Weighted ? 1 : 0;
        Rebind(PanelOf(Slot));
    }

    void __cdecl OnStatPicked(void* Argument, const int Index) {
        auto* Slot = static_cast<GroupSlot*>(Argument);
        CategoryPanel& Panel = PanelOf(Slot);
        Slot->Picker->SetSelected(-1);
        auto& Groups = Panel.Rules.Groups;
        if (Slot->Index >= static_cast<int>(Groups.size()) || Index < 0 || Index >= static_cast<int>(Panel.Attributes.size())) return;
        ShellFilter::Group& Rules = Groups[Slot->Index];
        const int Id = Panel.Attributes[Index]->Id;
        const bool AlreadyPicked = std::any_of(Rules.Criteria.begin(), Rules.Criteria.end(),
                                               [&](const ShellFilter::Criterion& Rule) { return Rule.Id == Id; });
        if (AlreadyPicked || static_cast<int>(Rules.Criteria.size()) >= MaxStatsPerGroup) return;
        Rules.Criteria.push_back({Id, Rules.Type == ShellFilter::GroupType::Weighted ? 1 : 0});
        Rebind(Panel);
    }

    void __cdecl OnRemoveStat(void* Argument) {
        const auto* Binding = static_cast<StatBinding*>(Argument);
        auto& Groups = PanelOf(Binding->Slot).Rules.Groups;
        if (Binding->Slot->Index >= static_cast<int>(Groups.size())) return;
        auto& Criteria = Groups[Binding->Slot->Index].Criteria;
        if (Binding->Row >= static_cast<int>(Criteria.size())) return;
        Criteria.erase(Criteria.begin() + Binding->Row);
        Rebind(PanelOf(Binding->Slot));
    }

    void __cdecl OnRemoveGroup(void* Argument) {
        auto* Slot = static_cast<GroupSlot*>(Argument);
        auto& Groups = PanelOf(Slot).Rules.Groups;
        if (Slot->Index >= static_cast<int>(Groups.size())) return;
        Groups.erase(Groups.begin() + Slot->Index);
        if (Groups.empty()) Groups.emplace_back();
        Rebind(PanelOf(Slot));
    }

    void __cdecl OnAddGroup(void* Argument) {
        auto* Panel = static_cast<CategoryPanel*>(Argument);
        if (static_cast<int>(Panel->Rules.Groups.size()) >= MaxGroups) return;
        Panel->Rules.Groups.emplace_back();
        Rebind(*Panel);
    }

    void ReadInputs(CategoryPanel& Panel) {
        auto& Groups = Panel.Rules.Groups;
        for (int g = 0; g < static_cast<int>(Groups.size()) && g < MaxGroups; g++) {
            GroupSlot& Slot = Panel.Slots[g];
            ShellFilter::Group& Rules = Groups[g];
            for (int r = 0; r < static_cast<int>(Rules.Criteria.size()) && r < MaxStatsPerGroup; r++) {
                if (Slot.Rows[r].Value.Edit) Rules.Criteria[r].Value = ParseNumber(Slot.Rows[r].Value.Edit->GetText());
            }
            const int ParamA = Slot.ParamA.Edit ? ParseNumber(Slot.ParamA.Edit->GetText()) : 0;
            if (Rules.Type == ShellFilter::GroupType::Count) {
                Rules.CountMin = std::max(1, ParamA);
                Rules.CountMax = Slot.ParamB.Edit ? ParseNumber(Slot.ParamB.Edit->GetText()) : 0;
            } else if (Rules.Type == ShellFilter::GroupType::Weighted) {
                Rules.WeightMin = ParamA;
            }
        }
    }

    void TickPanel(CategoryPanel& Panel, const TickContext& Context) {
        ReadInputs(Panel);
        for (int g = 0; g < static_cast<int>(Panel.Rules.Groups.size()) && g < MaxGroups; g++) {
            if (Panel.Slots[g].Type) Panel.Slots[g].Type->Tick(Context);
            if (Panel.Slots[g].Picker) Panel.Slots[g].Picker->Tick(Context);
        }
    }

    TEWLabel* AddHeaderLabel(TLBSWidget* Parent, const int16_t Width, const uint8_t Alignment, const wchar_t* Text) {
        TEWLabel* Label = AddLabel(Parent, 0, 0, Width, Alignment, Text);
        if (Label) Label->textColor = HeaderColor;
        return Label;
    }

    void CreateGroupSlot(CategoryPanel& Panel, GroupSlot& Slot, const int Index, const std::vector<std::wstring>& Names) {
        TLBSWidget* Parent = Panel.Container;
        Slot.Owner = &Panel;
        Slot.Index = Index;

        Slot.ParamLabelA = AddHeaderLabel(Parent, 32, 1, L"total");
        Slot.ParamA = WidgetKit::CreateEditBox(CachedHost, 0, 0, 28, 20);
        Attach(Parent, Slot.ParamA.Frame);
        Slot.ParamLabelB = AddHeaderLabel(Parent, 16, 3, L"to");
        Slot.ParamB = WidgetKit::CreateEditBox(CachedHost, 0, 0, 28, 20);
        Attach(Parent, Slot.ParamB.Frame);
        Slot.ValueHeader = AddHeaderLabel(Parent, ValueBoxWidth, 3, L"min");
        Slot.Remove = AddRemoveButton(Parent, &OnRemoveGroup, &Slot);
        Place(Slot.Remove, RemoveX, 0, RemoveSize, RemoveSize);

        for (int r = 0; r < MaxStatsPerGroup; r++) {
            StatRow& Row = Slot.Rows[r];
            Row.Name = AddLabel(Parent, Pad, 0, static_cast<int16_t>(ValueX - Pad - 4), 1, L"");
            Row.Value = WidgetKit::CreateEditBox(CachedHost, ValueX, 0, ValueBoxWidth, 20);
            Attach(Parent, Row.Value.Frame);
            Slot.Bindings[r] = {&Slot, r};
            Row.Remove = AddRemoveButton(Parent, &OnRemoveStat, &Slot.Bindings[r]);
            Place(Row.Remove, RemoveX, 0, RemoveSize, RemoveSize);
        }

        WidgetKit::Dropdown::Desc PickerDesc;
        PickerDesc.Width = static_cast<int16_t>(ContentWidth - 17);
        PickerDesc.VisibleRows = 12;
        PickerDesc.Placeholder = L"+ Add attribute";
        PickerDesc.OnPick = &OnStatPicked;
        PickerDesc.Argument = &Slot;
        Slot.Picker = WidgetKit::Dropdown::Create(CachedHost, PickerDesc);
        if (Slot.Picker) {
            Slot.Picker->SetItems(Names);
            Attach(Parent, Slot.Picker->Widget());
        }

        WidgetKit::Dropdown::Desc TypeDesc;
        TypeDesc.Width = 70;
        TypeDesc.VisibleRows = 3;
        TypeDesc.Filterable = false;
        TypeDesc.OnPick = &OnTypePicked;
        TypeDesc.Argument = &Slot;
        Slot.Type = WidgetKit::Dropdown::Create(CachedHost, TypeDesc);
        if (Slot.Type) {
            Slot.Type->SetItems({GroupTypeNames, GroupTypeNames + std::size(GroupTypeNames)});
            Slot.Type->SetSelected(0);
            Attach(Parent, Slot.Type->Widget());
        }
    }

    void CreateCategoryPanel(CategoryPanel& Panel, const Group Kind) {
        Panel.Kind = Kind;
        Panel.Attributes = ShellAttributes::OfGroup(Kind);
        Panel.Rules.Groups.assign(1, ShellFilter::Group{});
        Panel.Container = Widget::Create<TLBSWidget>(CachedHost);
        if (!Panel.Container) return;
        Panel.Container->isVisible = false;
        Attach(Window, Panel.Container);

        std::vector<std::wstring> Names;
        for (const auto* Attribute : Panel.Attributes) Names.push_back(Attribute->Name);

        Panel.AddGroup = Widget::Create<TEWButtonWidget>(CachedHost);
        if (Panel.AddGroup) {
            Panel.AddGroup->UseHeaderLook();
            Panel.AddGroup->SetWidth(ContentWidth);
            Panel.AddGroup->SetCaption(L"+ Add group");
            WidgetKit::SetOnClick(Panel.AddGroup, &OnAddGroup, &Panel);
            Attach(Panel.Container, Panel.AddGroup);
        }
        for (int g = MaxGroups - 1; g >= 0; g--) CreateGroupSlot(Panel, Panel.Slots[g], g, Names);
        Rebind(Panel);
    }

    void CreateFilterWindow(TLBSWidget* Root) {
        const WidgetKit::WindowDesc Desc{WidgetKit::WindowStyle::Plain, 0, 0, PanelWidth, 400, L"Shell Filter", false};
        Window = WidgetKit::CreateGameWindow(CachedHost, Desc);
        if (!Window) return;
        Window->isMoveable = false;
        Window->isVisible = false;

        Attach(Window, WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, OptionsTop, L"Tint matches", 80,
                                                        &Config.TintMatches, &SaveSettings));
        Attach(Window, WidgetKit::CreateLabeledCheckbox(CachedHost, Pad + 120, OptionsTop, L"Dim others", 80,
                                                        &Config.DimOthers, &SaveSettings));
        Attach(Window, WidgetKit::CreateLabeledCheckbox(CachedHost, Pad, OptionsTop + 22, L"Hide buy on others", 140,
                                                        &Config.HideOtherBuys, &SaveSettings));

        MatchCountLabel = AddLabel(Window, PanelWidth - 12 - 110, 15, 110, 2, L"");
        if (MatchCountLabel) MatchCountLabel->textColor = HeaderColor;
        ShownMatchCount.clear();

        CreateCategoryPanel(Panels[0], Group::Weapon);
        CreateCategoryPanel(Panels[1], Group::Armor);
        CreateCategoryPanel(Panels[2], Group::Fairy);

        Unavailable = Widget::Create<TLBSWidget>(CachedHost);
        if (Unavailable) {
            Unavailable->isVisible = false;
            Attach(Window, Unavailable);
            AddLabel(Unavailable, Pad, 4, PanelWidth - 2 * Pad, 1, L"No filter for this category yet.");
            AddLabel(Unavailable, Pad, 24, PanelWidth - 2 * Pad, 1, L"Pick Weapon or Armour.");
        }
        Attach(Root, Window);
    }

    CategoryPanel* PanelForCategory(const uint16_t Category) {
        switch (Category) {
            case CategoryWeapon:    return &Panels[0];
            case CategoryArmour:    return &Panels[1];
            default:                return nullptr;
        }
    }

    TNTConsignmentWidget* FindBazaar(const TLBSWidget* Root) {
        if (!BazaarVTable) BazaarVTable = CachedHost->ResolveVTable(TNTConsignmentWidget::ClassName);
        if (!BazaarVTable || !Root->childrenList) return nullptr;
        for (uint32_t i = 0; i < Root->childrenList->count; i++) {
            TLBSWidget* Child = Root->childrenList->list[i];
            if (Child && Child->vTable == BazaarVTable) return reinterpret_cast<TNTConsignmentWidget*>(Child);
        }
        return nullptr;
    }

    void FollowBazaar() {
        const Rect& Area = Bazaar->rect;
        int16_t X = Area.right;
        if (AttachedRoot && X + PanelWidth > AttachedRoot->rect.right) X = static_cast<int16_t>(Area.left - PanelWidth);
        const auto Height = static_cast<uint16_t>(Area.bottom - Area.top);
        if (Window->rect.left == X && Window->rect.top == Area.top && Window->rect.bottom - Window->rect.top == Height) return;
        Window->rect.left = X;
        Window->rect.right = static_cast<int16_t>(X + PanelWidth);
        Window->rect.top = Area.top;
        SetPanelHeight(Window, Height);
        for (TLBSWidget* Container : {Panels[0].Container, Panels[1].Container, Panels[2].Container, Unavailable}) {
            Place(Container, 0, ContentTop, PanelWidth, static_cast<int16_t>(Height - ContentTop));
        }
    }

    Color AccentTint() {
        const ImVec4 Accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        const float Brightest = std::max({Accent.x, Accent.y, Accent.z});
        const float Dimmest = std::min({Accent.x, Accent.y, Accent.z});
        if (Brightest <= 0.0f || Brightest - Dimmest < 0.05f) return FallbackTint;
        const auto Channel = [&](const float Value) { return static_cast<uint8_t>(Value / Brightest * 255.0f); };
        return Color(255, Channel(Accent.x), Channel(Accent.y), Channel(Accent.z));
    }

    bool SameColor(const Color& A, const Color& B) {
        return A.red == B.red && A.green == B.green && A.blue == B.blue && A.alpha == B.alpha;
    }

    void PaintRow(const int Slot, const RowLook Look) {
        TEWCustomPanelWidget* Bar = Bazaar->GetResultBar(Slot);
        TNTIconWidget* Icon = Bazaar->GetIconWidget(Slot);
        if (Bar) Bar->color = Look == RowLook::Tinted ? Tint : Look == RowLook::Dimmed ? DimmedColor : PlainColor;
        if (Icon) Icon->color = Look == RowLook::Dimmed ? DimmedColor : PlainColor;
    }

    void SetRowLook(const int Slot, const RowLook Look) {
        if (RowLooks[Slot] == Look || !Bazaar) return;
        PaintRow(Slot, Look);
        RowLooks[Slot] = Look;
    }

    void RefreshTint() {
        const Color Current = AccentTint();
        if (SameColor(Current, Tint)) return;
        Tint = Current;
        for (int i = 0; i < ResultsPerPage; i++) {
            if (RowLooks[i] == RowLook::Tinted) PaintRow(i, RowLook::Tinted);
        }
    }

    void SetBuyHidden(const int Slot, const bool Hide) {
        TEWGraphicButtonWidget* Button = Bazaar->buyButtons[Slot];
        TEWLabel* Label = Bazaar->buyButtonLabels[Slot];
        if (!Button) return;
        if (Hide) {
            if (!Button->isVisible) return;
            Button->isVisible = false;
            if (Label) Label->isVisible = false;
            BuyHiddenByUs[Slot] = true;
        } else if (BuyHiddenByUs[Slot]) {
            Button->isVisible = true;
            if (Label) Label->isVisible = true;
            BuyHiddenByUs[Slot] = false;
        }
    }

    void ClearRowLooks() {
        for (int i = 0; i < ResultsPerPage; i++) {
            SetRowLook(i, RowLook::Plain);
            if (Bazaar) SetBuyHidden(i, false);
        }
    }

    void SetMatchCount(const std::wstring& Text) {
        if (!MatchCountLabel || ShownMatchCount == Text) return;
        ShownMatchCount = Text;
        MatchCountLabel->SetText(Text.c_str());
    }

    void ApplyRowLooks(const CategoryPanel* Active) {
        const bool Filtering = Active && ShellFilter::IsActive(Active->Rules);
        const int First = (std::max<int>(1, Bazaar->currentPage) - 1) % PagesPerSearch * ResultsPerPage;
        int Listed = 0;
        int Matched = 0;
        for (int i = 0; i < ResultsPerPage; i++) {
            const int Index = First + i;
            if (Index >= static_cast<int>(Listings.size())) {
                SetRowLook(i, RowLook::Plain);
                BuyHiddenByUs[i] = false;
                continue;
            }
            if (!Filtering) {
                SetRowLook(i, RowLook::Plain);
                SetBuyHidden(i, false);
                continue;
            }
            Listed++;
            bool Match = false;
            Group Kind;
            const std::vector<Packet::RCBListShellOption>* Options = nullptr;
            if (ShellFilter::ShellOptionsOf(Listings[Index], Kind, Options) && Kind == Active->Kind) {
                Match = ShellFilter::Matches(Active->Rules, *Options);
            }
            if (Match) SetRowLook(i, Config.TintMatches ? RowLook::Tinted : RowLook::Plain);
            else SetRowLook(i, Config.DimOthers ? RowLook::Dimmed : RowLook::Plain);
            SetBuyHidden(i, !Match && Config.HideOtherBuys);
            Matched += Match;
        }
        SetMatchCount(Filtering && Listed > 0
                          ? std::to_wstring(Matched) + L" of " + std::to_wstring(Listed) + L" match"
                          : std::wstring{});
    }

    void RefreshFilterButtonLook() {
        if (FilterButton) FilterButton->color = Config.Enabled ? Color(255, 255, 255, 255) : Color(255, 110, 110, 110);
    }

    void ToggleFilter() {
        Config.Enabled = !Config.Enabled;
        SaveSettings(nullptr);
        RefreshFilterButtonLook();
    }

    void __cdecl OnFilterButton(void*) {
        ToggleFilter();
    }

    void CreateFilterButton() {
        TLBSWidget* BuyTab = Bazaar->buyTab;
        if (!BuyTab || !BuyTab->childrenList) return;
        FilterButton = Widget::Create<TEWGraphicButtonWidget>(CachedHost);
        if (!FilterButton) return;
        delete[] FilterButton->imageData.atlasFrames;
        FilterButton->imageData.imageName = 1593835585;
        FilterButton->imageData.imageWidth = 512;
        FilterButton->imageData.imageHeight = 512;
        FilterButton->imageData.frameCount = 9;
        FilterButton->imageData.atlasFrames = new AtlasFrame[9]{
            {419, 308, 5, 24}, {424, 308, 10, 24}, {409, 308, 10, 24},
            {445, 308, 5, 24}, {450, 308, 10, 24}, {435, 308, 10, 24},
            {471, 308, 5, 24}, {476, 308, 10, 24}, {461, 308, 10, 24},
        };
        constexpr wchar_t CaptionText[] = L"Filter";
        constexpr int16_t Cap = 10;
        constexpr int16_t Air = 8;
        const int Measured = WidgetKit::MeasureText(1, CaptionText);
        const auto Width = static_cast<int16_t>(std::max(40, (Measured > 0 ? Measured : 30) + 2 * (Cap + Air)));
        const auto Left = static_cast<int16_t>(13 + (92 - Width) / 2);
        FilterButton->nineSliceInfo = {static_cast<uint16_t>(Width - 2 * Cap), 25, static_cast<uint16_t>(Width - Cap), 25,
                                       Cap, 0, Cap, 0};
        FilterButton->sliceCount = 3;
        FilterButton->drawMode = 4;
        FilterButton->rect = {Left, 59, static_cast<int16_t>(Left + Width), 84};
        WidgetKit::SetOnClick(FilterButton, &OnFilterButton, nullptr);

        TEWLabel* Caption = AddLabel(FilterButton, 0, 6, Width, 3, CaptionText);
        if (Caption) {
            Caption->textColor = Color(255, 201, 227, 252);
            Caption->shadowColor = Color(255, 1, 61, 255);
        }
        Attach(BuyTab, FilterButton);
        RefreshFilterButtonLook();
    }

    bool OnSearchResults(const Packet::RCBListPacket& Packet) {
        Listings = Packet.entries;
        return true;
    }

    void Detach(TLBSWidget* Widget) {
        if (!Widget) return;
        Widget->isVisible = false;
        if (TLBSWidget* Parent = Widget->parent; Parent && Parent->childrenList
            && Parent->childrenList->index_of(Widget) >= 0) {
            Parent->childrenList->remove(Widget);
        }
    }
}

extern "C" {
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = std::size(Requirements);
        return Requirements;
    }

    __declspec(dllexport) void ModStartup(ImGuiContext* Context, const ImGuiMemAllocFunc AllocFunc,
                                          const ImGuiMemFreeFunc FreeFunc, void* AllocUserData,
                                          const ModHost* Host) {
        ImGui::SetCurrentContext(Context);
        ImGui::SetAllocatorFunctions(AllocFunc, FreeFunc, AllocUserData);
        CachedHost = Host;
        LoadSettings();
        Packet::SubscribePacket(Host, &OnSearchResults);
    }

    __declspec(dllexport) void ModShutdown() {
        ClearRowLooks();
        Detach(FilterButton);
        FilterButton = nullptr;
        Detach(Window);
        Window = nullptr;
        Bazaar = nullptr;
        AttachedRoot = nullptr;
        CachedHost = nullptr;
    }

    __declspec(dllexport) void ModEarlyTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        TLBSWidget* Root = const_cast<TLBSWidget*>(RootWidget);
        if (!CachedHost || !Root || !Root->childrenList) return;
        if (!tickContext.isPlayerLoaded) {
            Show(Window, false);
            return;
        }

        if (Root != AttachedRoot) {
            Window = nullptr;
            Bazaar = nullptr;
            FilterButton = nullptr;
            std::fill(std::begin(RowLooks), std::end(RowLooks), RowLook::Plain);
            std::fill(std::begin(BuyHiddenByUs), std::end(BuyHiddenByUs), false);
            AttachedRoot = Root;
            CreateFilterWindow(Root);
        }
        if (!Bazaar) Bazaar = FindBazaar(Root);
        if (Bazaar && !FilterButton) CreateFilterButton();

        if (!Window || !Bazaar) {
            CachedHost->ReportStatus(ModHealthLevel::Broken, !Window ? "Couldn't create the filter window."
                                                                     : "NosBazaar widget not found.");
            return;
        }
        if (ShellAttributes::All().empty()) {
            CachedHost->ReportStatus(ModHealthLevel::Warning, "Shell attribute names not found; the lists are empty.");
        } else {
            CachedHost->ReportStatus(ModHealthLevel::Ok, "");
        }

        const bool Shown = Config.Enabled && Bazaar->isVisible && Bazaar->currentTab == NBTab::Buy;
        if (Shown && !Window->isVisible) Window->BubbleUp();
        Show(Window, Shown);
        if (!Shown) {
            ClearRowLooks();
            return;
        }
        FollowBazaar();

        CategoryPanel* Active = PanelForCategory(Bazaar->categoryFilter);
        for (CategoryPanel& Panel : Panels) Show(Panel.Container, &Panel == Active);
        Show(Unavailable, Active == nullptr);

        if (Active) {
            TickPanel(*Active, tickContext);
        }
        RefreshTint();
        ApplyRowLooks(Active);
    }

    __declspec(dllexport) void ModTick(const TLBSWidget*, TickContext) {}

    __declspec(dllexport) void ModToggleMainWindow() {
        ToggleFilter();
    }
}

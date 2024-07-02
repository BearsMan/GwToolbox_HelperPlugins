#pragma once

#include <ToolboxUIPlugin.h>

class SmartVigSpirit : public ToolboxUIPlugin
{
public:
    SmartVigSpirit() {};
    ~SmartVigSpirit() {};

    const char *Name() const override
    {
        return "SmartVigSpirit";
    }

    const char *Icon() const override
    {
        return ICON_FA_CROWN;
    }

    void Initialize(ImGuiContext *, ImGuiAllocFns, HMODULE) override;
    void SignalTerminate() override;
    bool CanTerminate() override;
    void Terminate() override;
    bool HasSettings() const override
    {
        return false;
    }
    void LoadSettings(const wchar_t *) {};
    void SaveSettings(const wchar_t *) {};

    void Draw(IDirect3DDevice9 *) override {};
    void Update(float delta) override;

    /* MAIN CYCLE FUNCTIONS */

    bool HeroSmarterSkills_Main();
};

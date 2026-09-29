#pragma once

#include <Mod/CppUserModBase.hpp>

namespace RC::PakSync
{
    class PakSyncRuntime;

    class PakSyncMod final : public CppUserModBase
    {
    public:
        PakSyncMod();
        ~PakSyncMod() override;

        auto on_unreal_init() -> void override;
        auto on_update() -> void override;
        auto on_program_start() -> void override;

    private:
        PakSyncRuntime* m_impl{};
    };
}

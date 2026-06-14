#pragma once

#include <filesystem>
#include <vector>

#include <Compat/RustCoreFFI.hpp>
#include <Unreal/Core/Windows/MinimalWindowsApi.hpp>

#include <Mod/CppUserModBase.hpp>
#include <Mod/Mod.hpp>

#include <String/StringType.hpp>

namespace RC
{
    class CppMod : public Mod
    {
      private:
        std::filesystem::path m_dlls_path;
        RC::Compat::RustCore::CppModHandle* m_handle = nullptr;

      public:
        CppMod(UE4SSProgram&, StringType&& mod_name, StringType&& mod_path);
        CppMod(UE4SSProgram&, StringType&& mod_name, StringType&& mod_path, StringType&& dll_name);
        CppMod(CppMod&) = delete;
        CppMod(CppMod&&) = delete;
        ~CppMod() override;

      public:
        auto set_installable(bool) -> void override;
        auto is_installable() const -> bool override;
        auto set_installed(bool) -> void override;
        auto is_installed() const -> bool override;
        auto is_started() const -> bool override;
        auto set_updates_disabled(bool) -> void override;
        auto are_updates_disabled() const -> bool override;

        auto start_mod() -> void override;
        auto uninstall() -> void override;
        auto fire_unreal_init() -> void override;
        auto fire_ui_init() -> void override;
        auto fire_program_start() -> void override;
        auto fire_update() -> void override;
        auto fire_dll_load(StringViewType dll_name) -> void;
    };
} // namespace RC

#define NOMINMAX

#include <filesystem>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppMod.hpp>

namespace
{
    auto make_slice(RC::StringViewType value) -> RC::Compat::RustCore::SliceU16
    {
        return {
                reinterpret_cast<const uint16_t*>(value.data()),
                value.size(),
        };
    }

    auto make_slice(const RC::StringType& value) -> RC::Compat::RustCore::SliceU16
    {
        return make_slice(RC::StringViewType{value.data(), value.size()});
    }

    auto make_slice(const std::filesystem::path& value) -> RC::Compat::RustCore::SliceU16
    {
        const auto& native = value.native();
        return {
                reinterpret_cast<const uint16_t*>(native.data()),
                native.size(),
        };
    }

    enum class CppModFailureCode : uint32_t
    {
        None = 0,
        MissingDirectory = 1,
        LoadLibraryFailed = 2,
        MissingLifecycleExports = 3,
    };
}

namespace RC
{
    auto cppmod_status(Compat::RustCore::CppModHandle* handle) -> Compat::RustCore::CppModRuntimeStatus
    {
        return ue4ssl_core_cppmod_status(handle);
    }

    CppMod::CppMod(UE4SSProgram& program, StringType&& mod_name, StringType&& mod_path)
        : Mod(program, std::move(mod_name), std::move(mod_path))
    {
        m_dlls_path = m_mod_path;
        m_handle = ue4ssl_core_cppmod_create(make_slice(m_dlls_path), {});
        const auto status = cppmod_status(m_handle);
        if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::MissingDirectory)
            Output::send<LogLevel::Warning>(STR("Could not find the dlls folder for mod {}\n"), m_mod_name);
        else if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::LoadLibraryFailed)
            Output::send<LogLevel::Warning>(
                    STR("Failed to load dll <{}> for mod {}, error code: 0x{:x}\n"), ensure_str(m_dlls_path / STR("main.dll")), m_mod_name, status.last_error);
        else if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::MissingLifecycleExports)
            Output::send<LogLevel::Warning>(STR("Failed to find exported mod lifecycle functions for mod {}\n"), m_mod_name);
    }

    CppMod::CppMod(UE4SSProgram& program, StringType&& mod_name, StringType&& mod_path, StringType&& dll_name) 
        : Mod(program, std::move(mod_name), std::move(mod_path))
    {
        m_dlls_path = m_mod_path;
        m_handle = ue4ssl_core_cppmod_create(make_slice(m_dlls_path), make_slice(dll_name));
        const auto status = cppmod_status(m_handle);
        if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::MissingDirectory)
            Output::send<LogLevel::Warning>(STR("Could not find the dlls folder for mod {}\n"), m_mod_name);
        else if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::LoadLibraryFailed)
            Output::send<LogLevel::Warning>(
                    STR("Failed to load dll <{}> for mod {}, error code: 0x{:x}\n"), ensure_str(m_dlls_path / dll_name), m_mod_name, status.last_error);
        else if (static_cast<CppModFailureCode>(status.failure_code) == CppModFailureCode::MissingLifecycleExports)
            Output::send<LogLevel::Warning>(STR("Failed to find exported mod lifecycle functions for mod {}\n"), m_mod_name);
    }

    auto CppMod::set_installable(bool value) -> void
    {
        ue4ssl_core_cppmod_set_installable(m_handle, value ? 1 : 0);
    }

    auto CppMod::is_installable() const -> bool
    {
        return cppmod_status(m_handle).installable != 0;
    }

    auto CppMod::set_installed(bool value) -> void
    {
        ue4ssl_core_cppmod_set_installed(m_handle, value ? 1 : 0);
    }

    auto CppMod::is_installed() const -> bool
    {
        return cppmod_status(m_handle).installed != 0;
    }

    auto CppMod::is_started() const -> bool
    {
        return cppmod_status(m_handle).started != 0;
    }

    auto CppMod::set_updates_disabled(bool value) -> void
    {
        ue4ssl_core_cppmod_set_updates_disabled(m_handle, value ? 1 : 0);
    }

    auto CppMod::are_updates_disabled() const -> bool
    {
        return cppmod_status(m_handle).updates_disabled != 0;
    }

    auto CppMod::start_mod() -> void
    {
        try
        {
            ue4ssl_core_cppmod_start(m_handle);
        }
        catch (std::exception& e)
        {
            if (!Output::has_internal_error())
            {
                Output::send<LogLevel::Warning>(STR("Failed to start mod {}: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            else
            {
                printf_s("Internal Error: %s\n", e.what());
            }
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception starting mod '{}'\n"), m_mod_name);
        }
    }

    auto CppMod::uninstall() -> void
    {
        Output::send(STR("Stopping C++ mod '{}' for uninstall\n"), m_mod_name);
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_uninstall(m_handle);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception uninstalling mod '{}': {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception uninstalling mod '{}'\n"), m_mod_name);
            }
        }
    }

    auto CppMod::fire_unreal_init() -> void
    {
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_fire_unreal_init(m_handle);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception in mod '{}' on_unreal_init: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in mod '{}' on_unreal_init\n"), m_mod_name);
            }
        }
    }

    auto CppMod::fire_ui_init() -> void
    {
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_fire_ui_init(m_handle);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception in mod '{}' on_ui_init: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in mod '{}' on_ui_init\n"), m_mod_name);
            }
        }
    }

    auto CppMod::fire_program_start() -> void
    {
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_fire_program_start(m_handle);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception in mod '{}' on_program_start: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in mod '{}' on_program_start\n"), m_mod_name);
            }
        }
    }

    auto CppMod::fire_update() -> void
    {
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_fire_update(m_handle);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception in mod '{}' on_update: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in mod '{}' on_update\n"), m_mod_name);
            }
        }
    }

    auto CppMod::fire_dll_load(StringViewType dll_name) -> void
    {
        if (is_started())
        {
            try
            {
                ue4ssl_core_cppmod_fire_dll_load(m_handle, make_slice(dll_name));
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Exception in mod '{}' on_dll_load: {}\n"), m_mod_name, ensure_str(e.what()));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in mod '{}' on_dll_load\n"), m_mod_name);
            }
        }
    }

    CppMod::~CppMod()
    {
        ue4ssl_core_cppmod_destroy(m_handle);
        m_handle = nullptr;
    }
} // namespace RC

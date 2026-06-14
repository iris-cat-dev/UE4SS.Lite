#include <Compat/CppApiShim.hpp>

namespace
{
    using namespace RC;

    static_assert(sizeof(CharType) == sizeof(uint16_t));

    auto make_slice(RC::StringViewType value) -> RC::Compat::RustCore::SliceU16
    {
        return {
                reinterpret_cast<const uint16_t*>(value.data()),
                value.size(),
        };
    }

    auto make_slice(const std::filesystem::path& value) -> RC::Compat::RustCore::SliceU16
    {
        const auto& native = value.native();
        return {
                reinterpret_cast<const uint16_t*>(native.data()),
                native.size(),
        };
    }

    auto to_string(const RC::Compat::RustCore::OwnedString& value) -> RC::StringType
    {
        if (!value.data || value.len == 0)
        {
            return {};
        }

        return {
                reinterpret_cast<const CharType*>(value.data),
                value.len,
        };
    }

    auto to_path(const RC::Compat::RustCore::OwnedString& value) -> std::filesystem::path
    {
        return std::filesystem::path{to_string(value)};
    }
} // namespace

namespace RC::Compat::CppApi
{
    auto compute_base_paths(const std::filesystem::path& module_file_path, const std::filesystem::path& game_exe_path) -> PathSnapshot
    {
        auto snapshot = ue4ssl_core_compute_base_paths(make_slice(module_file_path), make_slice(game_exe_path));

        PathSnapshot result{
                .root_directory = to_path(snapshot.root_directory),
                .working_directory = to_path(snapshot.working_directory),
                .mods_directory = to_path(snapshot.mods_directory),
                .game_executable_directory = to_path(snapshot.game_executable_directory),
                .settings_path_and_file = to_path(snapshot.settings_path_and_file),
                .legacy_root_directory = to_path(snapshot.legacy_root_directory),
                .object_dumper_output_directory = to_path(snapshot.object_dumper_output_directory),
                .log_directory = to_path(snapshot.log_directory),
                .game_path_and_exe_name = to_path(snapshot.game_path_and_exe_name),
                .has_game_specific_config = snapshot.has_game_specific_config != 0,
        };

        ue4ssl_core_free_path_snapshot(snapshot);
        return result;
    }

    auto resolve_mods_directory(const std::filesystem::path& working_directory,
                                const std::filesystem::path& current_mods_directory,
                                StringViewType override_mods_directory) -> std::filesystem::path
    {
        auto resolved = ue4ssl_core_resolve_mods_directory(
                make_slice(working_directory), make_slice(current_mods_directory), make_slice(override_mods_directory));
        auto result = to_path(resolved);
        ue4ssl_core_free_string(resolved);
        return result;
    }

    auto discover_mods(const std::filesystem::path& working_directory, const std::filesystem::path& mods_directory) -> std::vector<DiscoveredModSpec>
    {
        auto discovery = ue4ssl_core_discover_mods(make_slice(working_directory), make_slice(mods_directory));
        std::vector<DiscoveredModSpec> mods{};
        mods.reserve(discovery.len);

        for (size_t index = 0; index < discovery.len; ++index)
        {
            const auto& mod = discovery.mods[index];
            DiscoveredModSpec spec{};
            spec.mod_name = to_string(mod.mod_name);
            spec.mod_path = to_path(mod.mod_path);
            spec.is_builtin = mod.is_builtin != 0;

            if (mod.has_custom_dll_name != 0)
            {
                spec.dll_name = to_string(mod.dll_name);
            }

            mods.emplace_back(std::move(spec));
        }

        ue4ssl_core_free_mod_discovery(discovery);
        return mods;
    }

    auto get_program_flags() -> RustCore::ProgramFlags
    {
        return ue4ssl_core_get_program_flags();
    }

    auto set_program_started(bool is_program_started) -> void
    {
        ue4ssl_core_set_program_started(is_program_started ? 1 : 0);
    }

    auto set_processing_state(bool processing_events, bool pause_events_processing) -> void
    {
        ue4ssl_core_set_processing_state(processing_events ? 1 : 0, pause_events_processing ? 1 : 0);
    }
} // namespace RC::Compat::CppApi

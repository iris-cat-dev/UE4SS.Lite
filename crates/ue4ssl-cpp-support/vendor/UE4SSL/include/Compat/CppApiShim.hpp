#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <Compat/RustCoreFFI.hpp>
#include <String/StringType.hpp>

namespace RC::Compat::CppApi
{
    struct PathSnapshot
    {
        std::filesystem::path root_directory;
        std::filesystem::path working_directory;
        std::filesystem::path mods_directory;
        std::filesystem::path game_executable_directory;
        std::filesystem::path settings_path_and_file;
        std::filesystem::path legacy_root_directory;
        std::filesystem::path object_dumper_output_directory;
        std::filesystem::path log_directory;
        std::filesystem::path game_path_and_exe_name;
        bool has_game_specific_config{};
    };

    struct DiscoveredModSpec
    {
        StringType mod_name{};
        std::filesystem::path mod_path{};
        std::optional<StringType> dll_name{};
        bool is_builtin{};
    };

    auto compute_base_paths(const std::filesystem::path& module_file_path, const std::filesystem::path& game_exe_path) -> PathSnapshot;
    auto resolve_mods_directory(const std::filesystem::path& working_directory,
                                const std::filesystem::path& current_mods_directory,
                                StringViewType override_mods_directory) -> std::filesystem::path;
    auto discover_mods(const std::filesystem::path& working_directory, const std::filesystem::path& mods_directory) -> std::vector<DiscoveredModSpec>;

    auto get_program_flags() -> RustCore::ProgramFlags;
    auto set_program_started(bool is_program_started) -> void;
    auto set_processing_state(bool processing_events, bool pause_events_processing) -> void;
} // namespace RC::Compat::CppApi

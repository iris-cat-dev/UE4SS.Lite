#pragma once

#include <string_view>

#include <Common.hpp>
#include <Mod/Mod.hpp>
#include <UE4SSProgram.hpp>

namespace RC
{
    class LuaMod;
}

namespace RC::LuaCompat
{
    inline constexpr CharType object_dumper_file_name[] = STR("UE4SSL_ObjectDump.txt");

    auto find_mod_by_name(StringViewType mod_name,
                          UE4SSProgram::IsInstalled installed_only = UE4SSProgram::IsInstalled::No,
                          UE4SSProgram::IsStarted started_only = UE4SSProgram::IsStarted::No) -> LuaMod*;
    auto find_mod_by_name(std::string_view mod_name,
                          UE4SSProgram::IsInstalled installed_only = UE4SSProgram::IsInstalled::No,
                          UE4SSProgram::IsStarted started_only = UE4SSProgram::IsStarted::No) -> LuaMod*;

    auto queue_reinstall_mod_by_name(std::string_view mod_name) -> void;
    auto queue_uninstall_mod_by_name(std::string_view mod_name) -> void;
    auto queue_reinstall_mod_by_name(StringViewType mod_name) -> void;
    auto queue_uninstall_mod_by_name(StringViewType mod_name) -> void;

    auto generate_cxx_headers(const RC::StringType& output_directory) -> void;
    auto generate_lua_types(const RC::StringType& output_directory) -> void;
    auto generate_uht_compatible_headers() -> void;
    auto dump_all_objects_and_properties(const RC::StringType& output_path_and_file_name) -> void;
} // namespace RC::LuaCompat

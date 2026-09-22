#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <Common.hpp>
#include <CrashDumper.hpp>
#include <Compat/RustRuntimeFFI.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/Handler.hpp>
#include <MProgram.hpp>
#include <Mod/CppMod.hpp>
#include <Mod/Mod.hpp>
#include <SettingsManager.hpp>
#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/UnrealVersion.hpp>

#include <String/StringType.hpp>

// Used to set up ImGui context and allocator in DLL mods
// #define UE4SS_ENABLE_IMGUI()                                                                                                                                   \
//     /* Wait for UE4SS to create the imgui context. */                                                                                                          \
//     /* Without this, we're setting the context to nullptr and eventually crashing when we use any imgui functions. */                                          \
//     {                                                                                                                                                          \
//         while ((UE4SSProgram::settings_manager.Debug.DebugConsoleVisible || UE4SSProgram::get_program().m_render_thread.get_id() != std::jthread::id{}) &&     \
//                !UE4SSProgram::get_program().get_current_imgui_context())                                                                                       \
//         {                                                                                                                                                      \
//         }                                                                                                                                                      \
//         ImGui::SetCurrentContext(UE4SSProgram::get_current_imgui_context());                                                                                   \
//         ImGuiMemAllocFunc alloc_func{};                                                                                                                        \
//         ImGuiMemFreeFunc free_func{};                                                                                                                          \
//         void* user_data{};                                                                                                                                     \
//         UE4SSProgram::get_current_imgui_allocator_functions(&alloc_func, &free_func, &user_data);                                                              \
//         ImGui::SetAllocatorFunctions(alloc_func, free_func, user_data);                                                                                        \
//     }

namespace RC
{
    namespace Unreal
    {
        class UObject;
        class UObjectBase;
        class UObjectBaseUtility;
        class UWorld;
    } // namespace Unreal

    namespace Output
    {
        class ConsoleDevice;
    }

    struct RecognizableStruct
    {
        // Sha1 hash with no salt: "RecognizableString"
        char recognizable_string[41]{"81acd41b7490f7b70ec6455657855733e21d7c0e"};

        // Ensure that we have zero-initialized memory at the end of the struct
        // This means that we can reliably check whether a function exists externally
        uint64_t safety_padding[8]{0};
    };


    class UE4SSProgram : public MProgram
    {
      public:
        friend class CppUserModBase; // m_input_handler

      public:
        constexpr static CharType m_settings_file_name[] = STR("UE4SS-settings.ini");
        constexpr static CharType m_log_file_name[] = STR("UE4SS.log");
        //constexpr static CharType m_object_dumper_file_name[] = STR("UE4SS_ObjectDump.txt");

      public:
        RC_UE4SS_API static SettingsManager settings_manager;
        static inline bool unreal_is_shutting_down{};

      protected:
        Input::Handler m_input_handler{L"ConsoleWindowClass", L"UnrealWindow"};

      public:
        std::jthread m_render_thread;

      private:
        CrashDumper m_crash_dumper{};

      private:
        std::filesystem::path m_game_path_and_exe_name;
        std::filesystem::path m_root_directory;
        std::filesystem::path m_module_file_path;
        std::filesystem::path m_working_directory;
        std::filesystem::path m_mods_directory;
        std::filesystem::path m_game_executable_directory;
        std::filesystem::path m_log_directory;
        std::filesystem::path m_object_dumper_output_directory;
        std::filesystem::path m_default_settings_path_and_file;
        std::filesystem::path m_settings_path_and_file;
        std::filesystem::path m_legacy_root_directory;
        Output::DebugConsoleDevice* m_debug_console_device{};
        Output::ConsoleDevice* m_console_device{};
        //GUI::DebuggingGUI m_debugging_gui{};

        using EventCallable = void (*)(void* data);

      public:

        RecognizableStruct m_shared_functions{};

        static inline UE4SSProgram* s_program{};

        bool m_has_game_specific_config{};

      public:
        enum class IsInstalled
        {
            Yes,
            No
        };

        enum class IsStarted
        {
            Yes,
            No
        };

      public:
        UE4SSProgram(const std::filesystem::path& ModuleFilePath, std::initializer_list<BinaryOptions> options);
        ~UE4SSProgram();
        UE4SSProgram(const UE4SSProgram&) = delete;
        UE4SSProgram(UE4SSProgram&&) = delete;

      private:
        auto setup_paths(const std::filesystem::path& moduleFilePath) -> void;
        enum class FunctionStatus
        {
            Success,
            Failure,
        };
        auto create_emergency_console_for_early_error(RC::StringViewType error_message) -> void;
        auto setup_mod_directory_path() -> void;
        auto create_simple_console() -> void;
        auto setup_unreal() -> void;
        auto load_unreal_offsets_from_file() -> void;
        auto on_program_start() -> void;

      public:
        auto init() -> void;
        auto is_program_started() -> bool;
        auto reinstall_mods() -> void;
        RC_UE4SS_API auto get_object_dumper_output_directory() -> const RC::StringType;
        RC_UE4SS_API auto get_module_directory() -> RC::StringType;
        RC_UE4SS_API auto get_game_executable_directory() -> RC::StringType;
        RC_UE4SS_API auto get_working_directory() -> RC::StringType;
        RC_UE4SS_API auto get_mods_directory() -> RC::StringType;
        RC_UE4SS_API auto get_legacy_root_directory() -> RC::StringType;
        RC_UE4SS_API auto queue_event(EventCallable callable, void* data) -> void;
        RC_UE4SS_API auto queue_event_owned(uintptr_t owner, EventCallable callable, void* data, EventCallable release) -> bool;
        RC_UE4SS_API auto is_queue_empty() -> bool;
        RC_UE4SS_API auto can_process_events() -> bool
        {
            return ue4ssl_core_get_program_flags().processing_events != 0;
        }

      public:
        // API pass-through for use outside the private scope of UE4SSProgram
        RC_UE4SS_API auto register_keydown_event(Input::Key, const Input::EventCallbackCallable&, uint8_t custom_data = 0, void* custom_data2 = nullptr) -> void;
        RC_UE4SS_API auto register_keydown_event(Input::Key,
                                                 const Input::Handler::ModifierKeyArray&,
                                                 const Input::EventCallbackCallable&,
                                                 uint8_t custom_data = 0,
                                                 void* custom_data2 = nullptr) -> void;
        RC_UE4SS_API auto register_keydown_event_owned(Input::Key, const Input::EventCallbackCallable&, uint8_t kind, uintptr_t owner) -> uint64_t;
        RC_UE4SS_API auto register_keydown_event_owned(Input::Key,
                                                     const Input::Handler::ModifierKeyArray&,
                                                     const Input::EventCallbackCallable&,
                                                     uint8_t kind,
                                                     uintptr_t owner) -> uint64_t;
        RC_UE4SS_API auto unregister_input_owner(uintptr_t owner) -> void;
        RC_UE4SS_API auto is_keydown_event_registered(Input::Key) -> bool;
        RC_UE4SS_API auto is_keydown_event_registered(Input::Key, const Input::Handler::ModifierKeyArray&) -> bool;

      private:
        using FMBNI_ExtraPredicate = std::function<bool(Mod*)>;
        static auto find_mod_by_name_internal(StringViewType mod_name,
                                              IsInstalled = IsInstalled::No,
                                              IsStarted = IsStarted::No,
                                              FMBNI_ExtraPredicate extra_predicate = {}) -> Mod*;

      public:
        template <typename T>
        static auto find_mod_by_name(StringViewType mod_name, IsInstalled = IsInstalled::No, IsStarted = IsStarted::No) -> T*
        {
            std::abort();
        };
        template <typename T>
        static auto find_mod_by_name(std::string_view mod_name, IsInstalled = IsInstalled::No, IsStarted = IsStarted::No) -> T*
        {
            std::abort();
        };
        template <>
        auto find_mod_by_name<CppMod>(StringViewType mod_name, IsInstalled is_installed, IsStarted is_started) -> CppMod*
        {
            return static_cast<CppMod*>(find_mod_by_name_internal(mod_name, is_installed, is_started, [](auto elem) -> bool {
                return dynamic_cast<CppMod*>(elem);
            }));
        }
        template <>
        auto find_mod_by_name<CppMod>(std::string_view mod_name, IsInstalled is_installed, IsStarted is_started) -> CppMod*
        {
            return find_mod_by_name<CppMod>(ensure_str(mod_name), is_installed, is_started);
        }

        RC_UE4SS_API static auto get_program() -> UE4SSProgram&
        {
            return *s_program;
        }

    };
} // namespace RC

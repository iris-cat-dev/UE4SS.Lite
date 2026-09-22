#define NOMINMAX

#include <Windows.h>

#ifdef TEXT
#undef TEXT
#endif

#include <algorithm>
#include <Compat/CompatibilityContract.hpp>
#include <Compat/CppApiShim.hpp>
#include <Compat/HostShim.hpp>
#include <Compat/RustCoreFFI.hpp>
#include <Compat/RustRuntimeFFI.hpp>
#include <Compat/UnrealBridge.hpp>
#include <cwctype>
#include <format>
#include <filesystem>
#include <new>
#include <limits>
#include <unordered_set>
#include <DynamicOutput/DynamicOutput.hpp>
#include <ExceptionHandling.hpp>
#include <Helpers/Format.hpp>
#include <Helpers/Integer.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppMod.hpp>
#include <Mod/Mod.hpp>
#include <SigScanner/SinglePassSigScanner.hpp>
#include <Signatures.hpp>
#include <UE4SSProgram.hpp>
#include <Compat/RustSupportFFI.hpp>
#include <Unreal/AGameMode.hpp>
#include <Unreal/AGameModeBase.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Searcher/ObjectSearcher.hpp>
#include <Unreal/Core/Templates/Tuple.hpp>
#include <Unreal/UEngine.hpp>
#include <Unreal/TypeChecker.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/UInterface.hpp>
#include <Unreal/UKismetSystemLibrary.hpp>
#include <Unreal/ULocalPlayer.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UPackage.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/World.hpp>
#include <Unreal/FWorldContext.hpp>
#include <UnrealDef.hpp>
//#include <MountPak.hpp>


#pragma comment(lib, "User32.lib")

namespace
{
    static_assert(sizeof(RC::CharType) == sizeof(uint16_t));


    auto make_slice(RC::StringViewType value) -> RC::Compat::RustCore::SliceU16
    {
        return {
                reinterpret_cast<const uint16_t*>(value.data()),
                value.size(),
        };
    }

    auto to_string(const RC::Compat::RustCore::OwnedString& value) -> RC::StringType
    {
        if (!value.data || value.len == 0)
        {
            return {};
        }

        return {
                reinterpret_cast<const RC::CharType*>(value.data),
                value.len,
        };
    }

    auto read_text_file(const std::filesystem::path& file_path) -> RC::StringType
    {
        static_assert(sizeof(std::filesystem::path::value_type) == sizeof(uint16_t));
        auto file_contents = ue4ssl_native_file_read_to_string(reinterpret_cast<const uint16_t*>(file_path.c_str()));
        if (!file_contents.data || file_contents.len == 0)
        {
            return {};
        }
        RC::StringType output{
                reinterpret_cast<const RC::CharType*>(file_contents.data),
                file_contents.len,
        };
        ue4ssl_native_file_free_string(file_contents);
        return output;
    }

    class RustIniList
    {
      public:
        RustIniList(Ue4sslCoreIniHandle* handle, RC::StringViewType section) : m_handle(handle), m_section(section)
        {
        }

        auto size() -> size_t
        {
            return ue4ssl_core_ini_ordered_list_len(m_handle, make_slice(m_section));
        }

        template <typename Callable>
        auto for_each(Callable callable) -> void
        {
            const auto item_count = size();
            for (size_t index = 0; index < item_count; ++index)
            {
                auto item_string = ue4ssl_core_ini_ordered_list_item(m_handle, make_slice(m_section), index);
                auto item = to_string(item_string);
                callable(static_cast<uint32_t>(index), item);
                ue4ssl_core_free_string(item_string);
            }
        }

      private:
        Ue4sslCoreIniHandle* m_handle{};
        RC::StringType m_section{};
    };

    class RustIniParser
    {
      public:
        explicit RustIniParser(RC::StringViewType contents) : m_handle(ue4ssl_core_ini_parse(make_slice(contents)))
        {
        }

        ~RustIniParser()
        {
            ue4ssl_core_ini_destroy(m_handle);
        }

        RustIniParser(const RustIniParser&) = delete;
        auto operator=(const RustIniParser&) -> RustIniParser& = delete;

        auto get_int64(RC::StringViewType section, RC::StringViewType key, int64_t default_value) -> int64_t
        {
            return ue4ssl_core_ini_get_i64(m_handle, make_slice(section), make_slice(key), default_value);
        }

        auto get_ordered_list(RC::StringViewType section) -> RustIniList
        {
            return RustIniList{m_handle, section};
        }

      private:
        Ue4sslCoreIniHandle* m_handle{};
    };
} // namespace

namespace RC
{

    template<typename Derived, typename Base>
    std::unique_ptr<Base> upcast_unique(std::unique_ptr<Derived>&& derived) {
        return std::unique_ptr<Base>(std::move(derived));
    }

    auto setup_builtin_scan_overrides(bool should_enable_guobjectarray_fallback, Unreal::UnrealInitializer::Config& config) -> void
    {
        if (config.ScanOverrides.guobjectarray)
        {
            return;
        }

        if (!should_enable_guobjectarray_fallback)
        {
            return;
        }

        config.ScanOverrides.guobjectarray = [](std::vector<SignatureContainer>& signature_containers, Unreal::Signatures::ScanResult& scan_result) {
            signature_containers.emplace_back(SignatureContainer{
                    {
                            SignatureData{"8B 05 ?? ?? ?? ?? 3B 05 ?? ?? ?? ?? 75 ?? 48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 05", 24},
                            SignatureData{"74 ?? 48 8D 0D ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01 E8 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01", 5},
                            SignatureData{"75 ?? 48 ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 C9 4C 89 74 24", 8},
                            SignatureData{"45 84 c0 48 c7 41 10 00 00 00 00 b8 ff ff ff ff 4c 8d 1d ?? ?? ?? ?? 89 41 08 4c 8b d1 4c 89 19 0f 45 05 ?? ?? ?? ?? ff c0 89 41 08 3b 05", 19},
                            SignatureData{"81 ce 00 00 00 02 83 e0 fb 89 47 08 48 8d 0d ?? ?? ?? ?? 48 89 fa 45 31 c0 e8 ?? ?? ?? ??", 15},
                            SignatureData{"8B 05 ?? ?? ?? ?? 2B 05 ?? ?? ?? ?? 2B 05 ?? ?? ?? ??", 14},
                            SignatureData{"E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 03 0D ?? ?? ?? ?? 29 C8 87 05", 19},
                    },
                    [](SignatureContainer& self) -> bool {
                        const auto signature_index = self.get_index_into_signatures();
                        const auto& signatures = self.get_signatures();
                        if (signature_index >= signatures.size())
                        {
                            return false;
                        }

                        auto* rip_disp32 = self.get_match_address() + signatures[signature_index].custom_data;
                        auto disp = *reinterpret_cast<int32_t*>(rip_disp32);
                        void* resolved_address = static_cast<void*>(rip_disp32 + 4 + disp);
                        if (!resolved_address)
                        {
                            return false;
                        }

                        Output::send(STR("GUObjectArray address: {} <- Builtin UE5.6 fallback\n"), resolved_address);
                        Unreal::UObjectArray::SetupGUObjectArrayAddress(resolved_address);
                        self.get_did_succeed() = true;
                        return true;
                    },
                    [&scan_result](SignatureContainer& self) {
                        if (!self.get_did_succeed())
                        {
                            scan_result.Errors.emplace_back("Was unable to find GUObjectArray via built-in UE5.6 fallback signatures");
                        }
                    }});
        };
    }

    SettingsManager UE4SSProgram::settings_manager{};

#define OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(StructName)                                                                                                           \
    for (const auto& [name, offset] : Unreal::StructName::MemberOffsets)                                                                                       \
    {                                                                                                                                                          \
        Output::send(STR(#StructName "::{} = 0x{:X}\n"), name, offset);                                                                                        \
    }

    auto output_all_member_offsets() -> void
    {
        Output::send(STR("\n##### MEMBER OFFSETS START #####\n\n"));
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UObjectBase);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UScriptStruct);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UScriptStruct::ICppStructOps);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FField);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FOutputDevice);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FEnumProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UStruct);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UFunction);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UField);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UWorld);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UClass);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(UEnum);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FObjectPropertyBase);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FDelegateProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FMulticastDelegateProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FSetProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FStructProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FArrayProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FMapProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FBoolProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FByteProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FClassProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FSoftClassProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FInterfaceProperty);
        OUTPUT_MEMBER_OFFSETS_FOR_STRUCT(FFieldPathProperty);
        Output::send(STR("\n##### MEMBER OFFSETS END #####\n\n"));
    }


    UE4SSProgram::UE4SSProgram(const std::filesystem::path& moduleFilePath, std::initializer_list<BinaryOptions> options) : MProgram(options)
    {
        s_program = this;

        try
        {
            setup_paths(moduleFilePath);

            try
            {
                settings_manager.deserialize(m_settings_path_and_file);
            }
            catch (std::exception& e)
            {
                create_emergency_console_for_early_error(std::format(STR("The settings parser failed: {}"), ensure_str(e.what())));
                return;
            }

            if (settings_manager.CrashDump.EnableDumping)
            {
                m_crash_dumper.enable();
            }

            m_crash_dumper.set_full_memory_dump(settings_manager.CrashDump.FullMemoryDump);

            // Setup the log file
            auto& file_device = Output::set_default_devices<Output::NewFileDevice>();
            file_device.set_file_name_and_path(ensure_str((m_log_directory / m_log_file_name)));

            create_simple_console();

            Output::send(STR("Console created\n"));
            const auto prerelease_suffix =
                    UE4SS_LIB_VERSION_PRERELEASE == 0 ? StringType{} : std::format(STR(" PreRelease #{}"), UE4SS_LIB_VERSION_PRERELEASE);
            const auto beta_suffix = UE4SS_LIB_BETA_STARTED == 0
                                             ? StringType{}
                                             : (UE4SS_LIB_IS_BETA == 0 ? StringType{STR(" Beta #?")} : std::format(STR(" Beta #{}"), UE4SS_LIB_VERSION_BETA));
            Output::send(STR("UE4SS - v{}.{}.{}{}{} - Git SHA #{}\n"),
                         UE4SS_LIB_VERSION_MAJOR,
                         UE4SS_LIB_VERSION_MINOR,
                         UE4SS_LIB_VERSION_HOTFIX,
                         prerelease_suffix,
                         beta_suffix,
                         ensure_str(UE4SS_LIB_BUILD_GITSHA));

#ifdef __clang__
#define UE4SS_COMPILER STR("Clang")
#else
#define UE4SS_COMPILER STR("MSVC")
#endif

            Output::send(STR("UE4SS Build Configuration: {} ({})\n"), ensure_str(UE4SS_CONFIGURATION), UE4SS_COMPILER);

            if (!ue4ssl_install_dll_notifications())
            {
                throw std::runtime_error{"Unable to install DLL notifications"};
            }

            Compat::UnrealBridge::setup_unreal_modules();

            setup_mod_directory_path();


            if (m_has_game_specific_config)
            {
                Output::send(STR("Found configuration for game: {}\n"), ensure_str(m_mods_directory.parent_path().filename()));
            }
            else
            {
                Output::send(STR("No specific game configuration found, using default configuration file\n"));
            }

            Output::send(STR("Config: {}\n\n"), ensure_str(m_settings_path_and_file));
            Output::send(STR("root directory: {}\n"), ensure_str(m_root_directory));
            Output::send(STR("working directory: {}\n"), ensure_str(m_working_directory));
            Output::send(STR("game executable directory: {}\n"), ensure_str(m_game_executable_directory));
            Output::send(STR("game executable: {} ({} bytes)\n\n\n"), ensure_str(m_game_path_and_exe_name), std::filesystem::file_size(m_game_path_and_exe_name));
            Output::send(STR("mods directory: {}\n"), ensure_str(m_mods_directory));
            Output::send(STR("log directory: {}\n"), ensure_str(m_log_directory));
            //Output::send(STR("object dumper directory: {}\n\n\n"), ensure_str(m_object_dumper_output_directory));
        }
        catch (std::runtime_error& e)
        {
            if (!m_error_object->has_error())
            {
                copy_error_into_message(e.what());
            }
            return;
        }
        catch (std::exception& e)
        {
            if (!m_error_object->has_error())
            {
                copy_error_into_message(e.what());
            }
            return;
        }
        catch (...)
        {
            if (!m_error_object->has_error())
            {
                copy_error_into_message("Unknown exception during UE4SSProgram construction");
            }
            return;
        }
    }

    UE4SSProgram::~UE4SSProgram()
    {
        ue4ssl_runtime_shutdown();
        ue4ssl_stop_dll_notifications();

        try
        {
            Output::close_all_default_devices();
        }
        catch (...) {}
    }

    auto UE4SSProgram::init() -> void
    {
        if (m_error_object->has_error())
        {
            return;
        }
        const Compat::RustCore::RuntimeConfig config{
                make_slice(m_working_directory.native()),
                make_slice(m_mods_directory.native()),
                this,
                [](void* context) -> uint8_t {
                    auto& program = *static_cast<UE4SSProgram*>(context);
                    try
                    {
                        program.setup_unreal();
                        Output::send(STR("Unreal Engine modules ({}):\n"), SigScannerStaticData::m_is_modular ? STR("modular") : STR("non-modular"));
                        const auto main_exe = SigScannerStaticData::m_modules_info.array[static_cast<size_t>(ScanTarget::MainExe)].lpBaseOfDll;
                        for (size_t index = 0; index < static_cast<size_t>(ScanTarget::Max); ++index)
                        {
                            auto& module = SigScannerStaticData::m_modules_info.array[index];
                            if (index == static_cast<size_t>(ScanTarget::MainExe) || main_exe != module.lpBaseOfDll)
                            {
                                Output::send(STR("{} @ {} size={:#x}\n"), ensure_str(ScanTargetToString(index)), module.lpBaseOfDll, module.SizeOfImage);
                            }
                        }
                        return 1;
                    }
                    catch (const std::exception& error)
                    {
                        program.copy_error_into_message(error.what());
                    }
                    catch (...)
                    {
                        program.copy_error_into_message("Unknown exception during Unreal initialization");
                    }
                    return 0;
                },
                [](void* context) -> uint8_t {
                    auto& program = *static_cast<UE4SSProgram*>(context);
                    try
                    {
                        Compat::UnrealBridge::set_max_asset_loading_memory(settings_manager.Memory.MaxMemoryUsageDuringAssetLoading);
                        program.on_program_start();
                        return 1;
                    }
                    catch (const std::exception& error)
                    {
                        program.copy_error_into_message(error.what());
                    }
                    catch (...)
                    {
                        program.copy_error_into_message("Unknown exception during program initialization");
                    }
                    return 0;
                },
                [](void* context) -> uint8_t {
                    if (unreal_is_shutting_down) return 0;
                    try
                    {
                        static_cast<UE4SSProgram*>(context)->m_input_handler.process_event();
                        return 1;
                    }
                    catch (...)
                    {
                        return 0;
                    }
                },
                [](void* context) {
                    auto& handler = static_cast<UE4SSProgram*>(context)->m_input_handler;
                    handler.unregister_kind(static_cast<uint8_t>(Compat::ScriptKeybindCustomData::Lua));
                    handler.unregister_kind(static_cast<uint8_t>(Compat::ScriptKeybindCustomData::JavaScript));
                },
                [](void* context, uintptr_t owner) {
                    static_cast<UE4SSProgram*>(context)->unregister_input_owner(owner);
                },
                static_cast<uint64_t>(std::max<int64_t>(0, settings_manager.General.SlowCppModUpdateThresholdMs)),
                static_cast<uint8_t>(settings_manager.General.EnableSlowCppModUpdateGuard),
        };
        if (!ue4ssl_runtime_start(&config) && !m_error_object->has_error())
        {
            copy_error_into_message("Rust runtime initialization failed");
        }
    }

    auto UE4SSProgram::setup_paths(const std::filesystem::path& moduleFilePath) -> void
    {
        wchar_t exe_path_buffer[1024]{};
        GetModuleFileNameW(GetModuleHandle(nullptr), exe_path_buffer, 1023);

        auto snapshot = Compat::CppApi::compute_base_paths(moduleFilePath, std::filesystem::path{exe_path_buffer});

        m_root_directory = std::move(snapshot.root_directory);
        m_module_file_path = moduleFilePath;
        m_working_directory = std::move(snapshot.working_directory);
        m_mods_directory = std::move(snapshot.mods_directory);
        m_game_executable_directory = std::move(snapshot.game_executable_directory);
        m_settings_path_and_file = std::move(snapshot.settings_path_and_file);
        m_legacy_root_directory = std::move(snapshot.legacy_root_directory);
        m_object_dumper_output_directory = std::move(snapshot.object_dumper_output_directory);
        m_log_directory = std::move(snapshot.log_directory);
        m_game_path_and_exe_name = std::move(snapshot.game_path_and_exe_name);
        m_has_game_specific_config = snapshot.has_game_specific_config;

        // Preserve the existing DLL search path behavior even though the value is the full exe path.
        AddDllDirectory(m_game_path_and_exe_name.c_str());
    }

    auto UE4SSProgram::create_emergency_console_for_early_error(RC::StringViewType error_message) -> void
    {
        settings_manager.Debug.SimpleConsoleEnabled = true;
        create_simple_console();
        printf_s("%S\n", FromCharTypePtr<wchar_t>(error_message.data()));
    }

    auto UE4SSProgram::setup_mod_directory_path() -> void
    {
        m_mods_directory =
                Compat::CppApi::resolve_mods_directory(m_working_directory, m_mods_directory, settings_manager.Overrides.ModsFolderPath);
    }

    auto UE4SSProgram::create_simple_console() -> void
    {
        if (settings_manager.Debug.SimpleConsoleEnabled)
        {
            m_debug_console_device = &Output::set_default_devices<Output::DebugConsoleDevice>();
            Output::set_default_log_level<LogLevel::Normal>();
            m_debug_console_device->set_formatter([](RC::StringViewType string) -> RC::StringType {
                return std::format(STR("[{}] {}"), std::format(STR("{:%X}"), std::chrono::system_clock::now()), string);
            });

            if (AllocConsole())
            {
                FILE* stdin_filename;
                FILE* stdout_filename;
                FILE* stderr_filename;
                freopen_s(&stdin_filename, "CONIN$", "r", stdin);
                freopen_s(&stdout_filename, "CONOUT$", "w", stdout);
                freopen_s(&stderr_filename, "CONOUT$", "w", stderr);
            }
        }
    }

    auto UE4SSProgram::load_unreal_offsets_from_file() -> void
    {
        std::filesystem::path file_path = m_working_directory / "MemberVariableLayout.ini";
        if (std::filesystem::exists(file_path))
        {
            if (auto file_contents = read_text_file(file_path); !file_contents.empty())
            {
                RustIniParser parser{file_contents};

                // The following code is auto-generated.
#include <MacroSetter.hpp>
            }
        }
    }

    auto UE4SSProgram::setup_unreal() -> void
    {
        
        // Retrieve offsets from the config file
        const StringType offset_overrides_section{STR("OffsetOverrides")};

        load_unreal_offsets_from_file();

        Unreal::UnrealInitializer::Config config;
        config.CachePath = m_root_directory / "cache";
        config.bInvalidateCacheIfSelfChanged = settings_manager.General.InvalidateCacheIfDLLDiffers;
        config.bEnableCache = settings_manager.General.UseCache;
        config.SecondsToScanBeforeGivingUp = settings_manager.General.SecondsToScanBeforeGivingUp;
        config.bUseUObjectArrayCache = settings_manager.General.UseUObjectArrayCache;
        config.bHookStaticConstructObjectObjectCache = settings_manager.Hooks.HookStaticConstructObjectObjectCache;
        config.bUseNativeStaticFindObjectFast = settings_manager.ObjectSearch.UseNativeStaticFindObjectFast;
        config.bUseNativeClassEnumeration = settings_manager.ObjectSearch.UseNativeClassEnumeration;
        config.bCompareNativeSearchResults = settings_manager.ObjectSearch.CompareNativeSearchResults;

        const auto unreal_config_plan = Compat::HostApi::plan_unreal_config(
                settings_manager.Threads.SigScannerNumThreads,
                settings_manager.Threads.SigScannerMultithreadingModuleSizeThreshold,
                settings_manager.EngineVersionOverride.MajorVersion,
                settings_manager.EngineVersionOverride.MinorVersion,
                settings_manager.Hooks.FExecVTableOffsetInLocalPlayer);

        if (unreal_config_plan.has_num_scan_threads)
        {
            config.NumScanThreads = unreal_config_plan.num_scan_threads;
        }

        if (unreal_config_plan.has_multithreading_module_size_threshold)
        {
            config.MultithreadingModuleSizeThreshold = unreal_config_plan.multithreading_module_size_threshold;
        }

        if (unreal_config_plan.engine_version_override_invalid)
        {
            throw std::runtime_error{
                    "Was unable to override engine version from ini file; The number in the ini file must be in range of a uint32"};
        }

        if (unreal_config_plan.has_engine_version_override)
        {
            Unreal::Version::Major = unreal_config_plan.engine_version_major;
            Unreal::Version::Minor = unreal_config_plan.engine_version_minor;
            config.ScanOverrides.version_finder = [&]([[maybe_unused]] auto&, Unreal::Signatures::ScanResult&) {};
        }

        setup_builtin_scan_overrides(unreal_config_plan.should_enable_builtin_guobjectarray_fallback != 0, config);

        // Virtual function offset overrides
        TRY([&]() {
            // ProfilerScope();Named("loading virtual function offset overrides");
            static RC::StringType virtual_function_offset_override_file{ensure_str((m_working_directory / STR("VTableLayout.ini")))};
            if (std::filesystem::exists(virtual_function_offset_override_file))
            {
                RustIniParser parser{read_text_file(virtual_function_offset_override_file)};

                Output::send<Color::Blue>(STR("Getting ordered lists from ini file\n"));

                auto calculate_virtual_function_offset = []<typename... BaseSizes>(uint32_t current_index, BaseSizes... base_sizes) -> uint32_t {
                    return current_index == 0 ? 0 : (current_index + (base_sizes + ...)) * 8;
                };

                auto retrieve_vtable_layout_from_ini = [&](const RC::StringType& section_name, auto callable) -> uint32_t {
                    auto list = parser.get_ordered_list(section_name);
                    uint32_t vtable_size = static_cast<uint32_t>(list.size() - 1);
                    list.for_each([&](uint32_t index, RC::StringType& item) {
                        callable(index, item);
                    });
                    return vtable_size;
                };

                Output::send<Color::Blue>(STR("UObjectBase\n"));
                uint32_t uobjectbase_size = retrieve_vtable_layout_from_ini(STR("UObjectBase"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, 0);
                    Output::send(STR("UObjectBase::{} = 0x{:X}\n"), item, offset);
                    Unreal::UObjectBase::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UObjectBaseUtility\n"));
                uint32_t uobjectbaseutility_size = retrieve_vtable_layout_from_ini(STR("UObjectBaseUtility"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size);
                    Output::send(STR("UObjectBaseUtility::{} = 0x{:X}\n"), item, offset);
                    Unreal::UObjectBaseUtility::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UObject\n"));
                uint32_t uobject_size = retrieve_vtable_layout_from_ini(STR("UObject"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size);
                    Output::send(STR("UObject::{} = 0x{:X}\n"), item, offset);
                    Unreal::UObject::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UField\n"));
                uint32_t ufield_size = retrieve_vtable_layout_from_ini(STR("UField"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size);
                    Output::send(STR("UField::{} = 0x{:X}\n"), item, offset);
                    Unreal::UField::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UEngine\n"));
                uint32_t uengine_size = retrieve_vtable_layout_from_ini(STR("UEngine"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size);
                    Output::send(STR("UEngine::{} = 0x{:X}\n"), item, offset);
                    Unreal::UEngine::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UScriptStruct::ICppStructOps\n"));
                retrieve_vtable_layout_from_ini(STR("UScriptStruct::ICppStructOps"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, 0);
                    Output::send(STR("UScriptStruct::ICppStructOps::{} = 0x{:X}\n"), item, offset);
                    Unreal::UScriptStruct::ICppStructOps::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FField\n"));
                uint32_t ffield_size = retrieve_vtable_layout_from_ini(STR("FField"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, 0);
                    Output::send(STR("FField::{} = 0x{:X}\n"), item, offset);
                    Unreal::FField::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FProperty\n"));
                uint32_t fproperty_size = retrieve_vtable_layout_from_ini(STR("FProperty"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset{};
                    if (Unreal::Version::IsBelow(4, 25))
                    {
                        offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size, ufield_size);
                    }
                    else
                    {
                        offset = calculate_virtual_function_offset(index, ffield_size);
                    }
                    Output::send(STR("FProperty::{} = 0x{:X}\n"), item, offset);
                    Unreal::FProperty::VTableLayoutMap.emplace(item, offset);
                });

                // If the engine version is <4.25 then the inheritance is different and we must take that into consideration.
                if (Unreal::Version::IsBelow(4, 25))
                {
                    fproperty_size = uobjectbase_size + uobjectbaseutility_size + uobject_size + ufield_size + fproperty_size;
                }
                else
                {
                    fproperty_size = ffield_size + fproperty_size;
                }

                Output::send<Color::Blue>(STR("FNumericProperty\n"));
                retrieve_vtable_layout_from_ini(STR("FNumericProperty"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, fproperty_size);
                    Output::send(STR("FNumericProperty::{} = 0x{:X}\n"), item, offset);
                    Unreal::FNumericProperty::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FMulticastDelegateProperty\n"));
                retrieve_vtable_layout_from_ini(STR("FMulticastDelegateProperty"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, fproperty_size);
                    Output::send(STR("FMulticastDelegateProperty::{} = 0x{:X}\n"), item, offset);
                    Unreal::FMulticastDelegateProperty::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FObjectPropertyBase\n"));
                retrieve_vtable_layout_from_ini(STR("FObjectPropertyBase"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, fproperty_size);
                    Output::send(STR("FObjectPropertyBase::{} = 0x{:X}\n"), item, offset);
                    Unreal::FObjectPropertyBase::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UStruct\n"));
                retrieve_vtable_layout_from_ini(STR("UStruct"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size, ufield_size);
                    Output::send(STR("UStruct::{} = 0x{:X}\n"), item, offset);
                    Unreal::UStruct::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FOutputDevice\n"));
                retrieve_vtable_layout_from_ini(STR("FOutputDevice"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, 0);
                    Output::send(STR("FOutputDevice::{} = 0x{:X}\n"), item, offset);
                    Unreal::FOutputDevice::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("FMalloc\n"));
                retrieve_vtable_layout_from_ini(STR("FMalloc"), [&](uint32_t index, RC::StringType& item) {
                    // We don't support FExec, so we're manually telling it the size.
                    static constexpr uint32_t fexec_size = 1;
                    uint32_t offset = calculate_virtual_function_offset(index, fexec_size);
                    Output::send(STR("FMalloc::{} = 0x{:X}\n"), item, offset);
                    Unreal::FMalloc::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("AActor\n"));
                uint32_t aactor_size = retrieve_vtable_layout_from_ini(STR("AActor"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size);
                    Output::send(STR("AActor::{} = 0x{:X}\n"), item, offset);
                    Unreal::AActor::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("AGameModeBase\n"));
                uint32_t agamemodebase_size = retrieve_vtable_layout_from_ini(STR("AGameModeBase"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size, aactor_size);
                    Output::send(STR("AGameModeBase::{} = 0x{:X}\n"), item, offset);
                    Unreal::AGameModeBase::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("AGameMode\n"));
                retrieve_vtable_layout_from_ini(STR("AGameMode"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index,
                                                                        Unreal::Version::IsAtLeast(4, 14)
                                                                        ? uobjectbase_size,
                                                                        uobjectbaseutility_size,
                                                                        uobject_size,
                                                                        aactor_size,
                                                                        agamemodebase_size
                                                                        : uobjectbase_size,
                                                                        uobjectbaseutility_size,
                                                                        uobject_size,
                                                                        aactor_size);
                    Output::send(STR("AGameMode::{} = 0x{:X}\n"), item, offset);
                    Unreal::AGameMode::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("UPlayer\n"));
                uint32_t uplayer_size = retrieve_vtable_layout_from_ini(STR("UPlayer"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size);
                    Output::send(STR("UPlayer::{} = 0x{:X}\n"), item, offset);
                    Unreal::UPlayer::VTableLayoutMap.emplace(item, offset);
                });

                Output::send<Color::Blue>(STR("ULocalPlayer\n"));
                retrieve_vtable_layout_from_ini(STR("ULocalPlayer"), [&](uint32_t index, RC::StringType& item) {
                    uint32_t offset = calculate_virtual_function_offset(index, uobjectbase_size, uobjectbaseutility_size, uobject_size, uplayer_size);
                    Output::send(STR("ULocalPlayer::{} = 0x{:X}\n"), item, offset);
                    Unreal::ULocalPlayer::VTableLayoutMap.emplace(item, offset);
                });
            }
        });

        config.bHookProcessInternal = settings_manager.Hooks.HookProcessInternal;
        config.bHookProcessLocalScriptFunction = settings_manager.Hooks.HookProcessLocalScriptFunction;
        config.bHookLoadMap = settings_manager.Hooks.HookLoadMap;
        config.bHookInitGameState = settings_manager.Hooks.HookInitGameState;
        config.bHookCallFunctionByNameWithArguments = settings_manager.Hooks.HookCallFunctionByNameWithArguments;
        config.bHookBeginPlay = settings_manager.Hooks.HookBeginPlay;
        config.bHookLocalPlayerExec = settings_manager.Hooks.HookLocalPlayerExec;
        config.bHookEngineTick = settings_manager.Hooks.HookEngineTick;
        config.bHookAActorTick = settings_manager.Hooks.HookAActorTick;
        config.bHookUObjectProcessEvent = settings_manager.Hooks.HookProcessEvent;
        config.bHookUFunctionBind = settings_manager.Hooks.HookUFunctionBind;
        config.FExecVTableOffsetInLocalPlayer = unreal_config_plan.fexec_vtable_offset_in_local_player;

        Unreal::UnrealInitializer::Initialize(config);
        //InstallPakMountHook();

        bool can_create_custom_events{true};
        if (!UObject::ProcessLocalScriptFunctionInternal.is_ready() && Unreal::Version::IsAtLeast(4, 22))
        {
            can_create_custom_events = false;
            Output::send<LogLevel::Warning>(STR("ProcessLocalScriptFunction is not available, the following features will be unavailable:\n"));
        }
        else if (!UObject::ProcessInternalInternal.is_ready() && Unreal::Version::IsBelow(4, 22))
        {
            can_create_custom_events = false;
            Output::send<LogLevel::Warning>(STR("ProcessInternal is not available, the following features will be unavailable:\n"));
        }
        if (!can_create_custom_events)
        {
            Output::send<LogLevel::Warning>(STR("<Put function here responsible for creating custom UFunctions or events for BPs>\n"));
        }
    }

    auto UE4SSProgram::on_program_start() -> void
    {
        if ((settings_manager.ObjectDumper.LoadAllAssetsBeforeDumpingObjects || settings_manager.CXXHeaderGenerator.LoadAllAssetsBeforeGeneratingCXXHeaders) &&
            Unreal::Version::IsBelow(4, 17))
        {
            Output::send<LogLevel::Warning>(
                    STR("FAssetData not available in <4.17, ignoring 'LoadAllAssetsBeforeDumpingObjects' & 'LoadAllAssetsBeforeGeneratingCXXHeaders'."));
        }
    }



    auto UE4SSProgram::is_program_started() -> bool
    {
        return ue4ssl_core_get_program_flags().is_program_started != 0;
    }

    auto UE4SSProgram::reinstall_mods() -> void
    {
        ue4ssl_runtime_reinstall();
    }

    auto UE4SSProgram::get_module_directory() -> RC::StringType
    {
        return ensure_str(m_module_file_path);
    }

    auto UE4SSProgram::get_game_executable_directory() -> RC::StringType
    {
        return ensure_str(m_game_executable_directory);
    }

    auto UE4SSProgram::get_working_directory() -> RC::StringType
    {
        return ensure_str(m_working_directory);
    }

    auto UE4SSProgram::get_mods_directory() -> RC::StringType
    {
        return ensure_str(m_mods_directory);
    }

    auto UE4SSProgram::get_legacy_root_directory() -> RC::StringType
    {
        return ensure_str(m_legacy_root_directory);
    }

    auto UE4SSProgram::queue_event(EventCallable callable, void* data) -> void
    {
        queue_event_owned(ue4ssl_runtime_current_owner(), callable, data, nullptr);
    }

    auto UE4SSProgram::queue_event_owned(uintptr_t owner, EventCallable callable, void* data, EventCallable release) -> bool
    {
        struct Context
        {
            EventCallable callable;
            void* data;
            EventCallable release;
        };
        auto* context = callable ? new (std::nothrow) Context{callable, data, release} : nullptr;
        if (!context)
        {
            try
            {
                if (release) release(data);
            }
            catch (...) {}
            return false;
        }
        return ue4ssl_runtime_queue_event({
                owner,
                [](void* opaque) {
                    auto* context = static_cast<Context*>(opaque);
                    try
                    {
                        if (context->callable) context->callable(context->data);
                    }
                    catch (const std::exception& error)
                    {
                        try
                        {
                            Output::send<LogLevel::Error>(STR("Exception in queued event: {}\n"), ensure_str(error.what()));
                        }
                        catch (...) {}
                    }
                    catch (...) {}
                },
                context,
                [](void* opaque) {
                    auto* context = static_cast<Context*>(opaque);
                    try
                    {
                        if (context->release) context->release(context->data);
                    }
                    catch (...) {}
                    delete context;
                },
        }) != 0;
    }

    auto UE4SSProgram::is_queue_empty() -> bool
    {
        return ue4ssl_runtime_queue_empty() != 0;
    }


    auto UE4SSProgram::register_keydown_event(Input::Key key, const Input::EventCallbackCallable& callback, uint8_t custom_data, void* custom_data2) -> void
    {
        m_input_handler.register_keydown_event(key, callback, reinterpret_cast<uintptr_t>(custom_data2), custom_data);
    }

    auto UE4SSProgram::register_keydown_event(Input::Key key,
                                              const Input::Handler::ModifierKeyArray& modifier_keys,
                                              const Input::EventCallbackCallable& callback,
                                              uint8_t custom_data,
                                              void* custom_data2) -> void
    {
        m_input_handler.register_keydown_event(key, modifier_keys, callback, reinterpret_cast<uintptr_t>(custom_data2), custom_data);
    }

    auto UE4SSProgram::is_keydown_event_registered(Input::Key key) -> bool
    {
        return m_input_handler.is_keydown_event_registered(key);
    }

    auto UE4SSProgram::is_keydown_event_registered(Input::Key key, const Input::Handler::ModifierKeyArray& modifier_keys) -> bool
    {
        return m_input_handler.is_keydown_event_registered(key, modifier_keys);
    }

    auto UE4SSProgram::unregister_input_owner(uintptr_t owner) -> void
    {
        m_input_handler.unregister_owner(owner);
        ue4ssl_runtime_cancel_owner(owner);
    }

    auto UE4SSProgram::register_keydown_event_owned(Input::Key key, const Input::EventCallbackCallable& callback, uint8_t kind, uintptr_t owner) -> uint64_t
    {
        return m_input_handler.register_keydown_event(key, callback, owner, kind);
    }

    auto UE4SSProgram::register_keydown_event_owned(Input::Key key,
                                                  const Input::Handler::ModifierKeyArray& modifier_keys,
                                                  const Input::EventCallbackCallable& callback,
                                                  uint8_t kind,
                                                  uintptr_t owner) -> uint64_t
    {
        return m_input_handler.register_keydown_event(key, modifier_keys, callback, owner, kind);
    }

    auto UE4SSProgram::find_mod_by_name_internal(StringViewType mod_name, IsInstalled is_installed, IsStarted is_started, FMBNI_ExtraPredicate extra_predicate)
            -> Mod*
    {
        auto* mod = static_cast<Mod*>(ue4ssl_runtime_find_mod(make_slice(mod_name),
                is_installed == IsInstalled::Yes, is_started == IsStarted::Yes));
        return mod && (!extra_predicate || extra_predicate(mod)) ? mod : nullptr;
    }

    auto UE4SSProgram::get_object_dumper_output_directory() -> const RC::StringType
    {
        return ensure_str(m_object_dumper_output_directory);
    }

} // namespace RC

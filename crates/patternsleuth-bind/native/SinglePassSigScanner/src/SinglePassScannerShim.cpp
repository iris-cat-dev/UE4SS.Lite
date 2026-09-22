#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>

#include <SigScanner/SinglePassSigScanner.hpp>

namespace
{
    using PsAobMatchCallback = uint8_t (*)(uintptr_t address, size_t pattern_len, void* user_data);

    extern "C"
    {
        auto ps_scan_aob(const uint8_t* base,
                         size_t size,
                         const uint8_t* pattern,
                         size_t pattern_len,
                         PsAobMatchCallback callback,
                         void* user_data) -> size_t;
        auto ps_scan_wide_string(const uint8_t* base, size_t size, const uint16_t* needle, size_t needle_len) -> uintptr_t;
    }

    constexpr size_t scan_error = static_cast<size_t>(-1);

    struct MatchCallbackState
    {
        RC::SignatureContainer* container{};
        size_t signature_index{};
    };

    static constexpr std::array k_scan_target_names{
            std::string_view{"MainExe"},
            std::string_view{"AIModule"},
            std::string_view{"Analytics"},
            std::string_view{"AnalyticsET"},
            std::string_view{"AnimationCore"},
            std::string_view{"AnimGraphRuntime"},
            std::string_view{"AppFramework"},
            std::string_view{"ApplicationCore"},
            std::string_view{"AssetRegistry"},
            std::string_view{"AudioCaptureCore"},
            std::string_view{"AudioCaptureRtAudio"},
            std::string_view{"AudioExtensions"},
            std::string_view{"AudioMixer"},
            std::string_view{"AudioMixerCore"},
            std::string_view{"AudioMixerXAudio2"},
            std::string_view{"AudioPlatformConfiguration"},
            std::string_view{"AugmentedReality"},
            std::string_view{"AVEncoder"},
            std::string_view{"AVIWriter"},
            std::string_view{"BuildPatchServices"},
            std::string_view{"BuildSettings"},
            std::string_view{"Cbor"},
            std::string_view{"CEF3Utils"},
            std::string_view{"Chaos"},
            std::string_view{"ChaosCore"},
            std::string_view{"ChaosSolverEngine"},
            std::string_view{"ChaosSolvers"},
            std::string_view{"CinematicCamera"},
            std::string_view{"ClothingSystemRuntimeCommon"},
            std::string_view{"ClothingSystemRuntimeInterface"},
            std::string_view{"ClothingSystemRuntimeNv"},
            std::string_view{"Core"},
            std::string_view{"CoreUObject"},
            std::string_view{"CrunchCompression"},
            std::string_view{"D3D11RHI"},
            std::string_view{"D3D12RHI"},
            std::string_view{"Engine"},
            std::string_view{"EngineMessages"},
            std::string_view{"EngineSettings"},
            std::string_view{"EyeTracker"},
            std::string_view{"FieldSystemCore"},
            std::string_view{"FieldSystemEngine"},
            std::string_view{"FieldSystemSimulationCore"},
            std::string_view{"Foliage"},
            std::string_view{"GameplayMediaEncoder"},
            std::string_view{"GameplayTags"},
            std::string_view{"GameplayTasks"},
            std::string_view{"GeometryCollectionCore"},
            std::string_view{"GeometryCollectionEngine"},
            std::string_view{"GeometryCollectionSimulationCore"},
            std::string_view{"HeadMountedDisplay"},
            std::string_view{"HTTP"},
            std::string_view{"HttpNetworkReplayStreaming"},
            std::string_view{"HTTPServer"},
            std::string_view{"Icmp"},
            std::string_view{"ImageCore"},
            std::string_view{"ImageWrapper"},
            std::string_view{"ImageWriteQueue"},
            std::string_view{"InputCore"},
            std::string_view{"InputDevice"},
            std::string_view{"InstallBundleManager"},
            std::string_view{"InstancedSplines"},
            std::string_view{"InteractiveToolsFramework"},
            std::string_view{"Json"},
            std::string_view{"JsonUtilities"},
            std::string_view{"Landscape"},
            std::string_view{"LauncherCheck"},
            std::string_view{"LauncherPlatform"},
            std::string_view{"LevelSequence"},
            std::string_view{"LocalFileNetworkReplayStreaming"},
            std::string_view{"MaterialShaderQualitySettings"},
            std::string_view{"Media"},
            std::string_view{"MediaAssets"},
            std::string_view{"MediaUtils"},
            std::string_view{"MeshDescription"},
            std::string_view{"MeshUtilitiesCommon"},
            std::string_view{"Messaging"},
            std::string_view{"MessagingCommon"},
            std::string_view{"MoviePlayer"},
            std::string_view{"MovieScene"},
            std::string_view{"MovieSceneCapture"},
            std::string_view{"MovieSceneTracks"},
            std::string_view{"MRMesh"},
            std::string_view{"NavigationSystem"},
            std::string_view{"Navmesh"},
            std::string_view{"NetCore"},
            std::string_view{"Networking"},
            std::string_view{"NetworkReplayStreaming"},
            std::string_view{"NonRealtimeAudioRenderer"},
            std::string_view{"NullDrv"},
            std::string_view{"NullNetworkReplayStreaming"},
            std::string_view{"OpenGLDrv"},
            std::string_view{"Overlay"},
            std::string_view{"PacketHandler"},
            std::string_view{"PakFile"},
            std::string_view{"PerfCounters"},
            std::string_view{"PhysicsCore"},
            std::string_view{"PhysicsSQ"},
            std::string_view{"PhysXCooking"},
            std::string_view{"PreLoadScreen"},
            std::string_view{"Projects"},
            std::string_view{"PropertyPath"},
            std::string_view{"RawMesh"},
            std::string_view{"ReliabilityHandlerComponent"},
            std::string_view{"RenderCore"},
            std::string_view{"Renderer"},
            std::string_view{"RHI"},
            std::string_view{"RSA"},
            std::string_view{"SandboxFile"},
            std::string_view{"Serialization"},
            std::string_view{"SessionMessages"},
            std::string_view{"SessionServices"},
            std::string_view{"SignalProcessing"},
            std::string_view{"Slate"},
            std::string_view{"SlateCore"},
            std::string_view{"SlateNullRenderer"},
            std::string_view{"SlateRHIRenderer"},
            std::string_view{"Sockets"},
            std::string_view{"SoundFieldRendering"},
            std::string_view{"SSL"},
            std::string_view{"StaticMeshDescription"},
            std::string_view{"StreamingPauseRendering"},
            std::string_view{"SynthBenchmark"},
            std::string_view{"TimeManagement"},
            std::string_view{"TraceLog"},
            std::string_view{"UELibSampleRate"},
            std::string_view{"UMG"},
            std::string_view{"VectorVM"},
            std::string_view{"Voice"},
            std::string_view{"Voronoi"},
            std::string_view{"VulkanRHI"},
            std::string_view{"WebBrowser"},
            std::string_view{"WindowsPlatformFeatures"},
            std::string_view{"XAudio2"},
    };

    static_assert(k_scan_target_names.size() == static_cast<size_t>(RC::ScanTarget::Max));

    auto format_aob_string(std::string& value) -> void
    {
        if (value.size() < 4 || value[3] != '/')
        {
            return;
        }

        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        std::replace(value.begin(), value.end(), '/', ' ');
    }

    auto convert_hex_char_to_int(char ch) -> int
    {
        if (ch >= '0' && ch <= '9')
        {
            return ch - '0';
        }
        if (ch >= 'A' && ch <= 'F')
        {
            return ch - 'A' + 10;
        }
        if (ch >= 'a' && ch <= 'f')
        {
            return ch - 'a' + 10;
        }
        return -1;
    }
} // namespace

namespace RC
{
    ScanTargetArray SigScannerStaticData::m_modules_info;
    bool SigScannerStaticData::m_is_modular;

    uint32_t SinglePassScanner::m_num_threads = 8;
    SinglePassScanner::ScanMethod SinglePassScanner::m_scan_method = ScanMethod::Scalar;
    uint32_t SinglePassScanner::m_multithreading_module_size_threshold = 0x1000000;
    std::mutex SinglePassScanner::m_scanner_mutex{};

    auto WIN_MODULEINFO::operator=(MODULEINFO other) -> WIN_MODULEINFO&
    {
        lpBaseOfDll = other.lpBaseOfDll;
        SizeOfImage = other.SizeOfImage;
        EntryPoint = other.EntryPoint;
        return *this;
    }

    auto ScanTargetArray::operator[](ScanTarget index) -> MODULEINFO&
    {
        return *std::bit_cast<MODULEINFO*>(&array[static_cast<size_t>(index)]);
    }

    auto ScanTargetToString(ScanTarget scan_target) -> std::string
    {
        return ScanTargetToString(static_cast<size_t>(scan_target));
    }

    auto ScanTargetToString(size_t scan_target) -> std::string
    {
        if (scan_target >= k_scan_target_names.size())
        {
            return {"Unknown"};
        }

        return std::string{k_scan_target_names[scan_target]};
    }

    auto SinglePassScanner::string_to_vector(std::string_view signature) -> std::vector<int>
    {
        std::vector<int> bytes;
        bytes.reserve(signature.size());

        for (char current : signature)
        {
            if (current == '?')
            {
                bytes.push_back(-1);
            }
            else if (std::isxdigit(static_cast<unsigned char>(current)))
            {
                bytes.push_back(convert_hex_char_to_int(current));
            }
        }

        return bytes;
    }

    auto SinglePassScanner::string_to_vector(const std::vector<SignatureData>& signatures) -> std::vector<std::vector<int>>
    {
        std::vector<std::vector<int>> vector_of_signatures;
        vector_of_signatures.reserve(signatures.size());

        for (const auto& signature_data : signatures)
        {
            vector_of_signatures.emplace_back(string_to_vector(signature_data.signature));
        }

        return vector_of_signatures;
    }

    auto SinglePassScanner::format_aob_strings(std::vector<SignatureContainer>& signature_containers) -> void
    {
        std::lock_guard<std::mutex> safe_scope(m_scanner_mutex);
        for (auto& signature_container : signature_containers)
        {
            for (auto& signature : signature_container.signatures)
            {
                format_aob_string(signature.signature);
            }
        }
    }

    auto SinglePassScanner::scanner_work_thread(uint8_t* start_address,
                                                uint8_t* end_address,
                                                SYSTEM_INFO&,
                                                std::vector<SignatureContainer>& signature_containers) -> void
    {
        if (!start_address || !end_address || start_address >= end_address)
        {
            return;
        }

        format_aob_strings(signature_containers);
        const auto scan_size = static_cast<size_t>(end_address - start_address);

        auto on_match = [](uintptr_t address, size_t pattern_len, void* user_data) -> uint8_t {
            auto* state = static_cast<MatchCallbackState*>(user_data);
            auto& container = *state->container;

            if (container.ignore)
            {
                return 1;
            }

            container.index_into_signatures = state->signature_index;
            container.match_address = reinterpret_cast<uint8_t*>(address);
            container.match_signature_size = pattern_len;

            const bool should_stop = container.on_match_found(container);
            container.ignore = should_stop;

            if (container.store_results)
            {
                container.result_store.emplace_back(SignatureContainerLight{
                        .index_into_signatures = state->signature_index,
                        .match_address = reinterpret_cast<uint8_t*>(address),
                });
            }

            return should_stop ? 1 : 0;
        };

        for (auto& container : signature_containers)
        {
            for (size_t signature_index = 0; signature_index < container.signatures.size(); ++signature_index)
            {
                if (container.ignore)
                {
                    break;
                }

                const auto& signature = container.signatures[signature_index].signature;
                MatchCallbackState state{.container = &container, .signature_index = signature_index};
                const auto match_count = ps_scan_aob(start_address,
                                                     scan_size,
                                                     reinterpret_cast<const uint8_t*>(signature.data()),
                                                     signature.size(),
                                                     on_match,
                                                     &state);
                if (match_count == scan_error || container.ignore)
                {
                    break;
                }
            }
        }
    }

    auto SinglePassScanner::scanner_work_thread_scalar(uint8_t* start_address,
                                                       uint8_t* end_address,
                                                       SYSTEM_INFO& info,
                                                       std::vector<SignatureContainer>& signature_containers) -> void
    {
        scanner_work_thread(start_address, end_address, info, signature_containers);
    }

    auto SinglePassScanner::scanner_work_thread_stdfind(uint8_t* start_address,
                                                        uint8_t* end_address,
                                                        SYSTEM_INFO& info,
                                                        std::vector<SignatureContainer>& signature_containers) -> void
    {
        scanner_work_thread(start_address, end_address, info, signature_containers);
    }

    auto SinglePassScanner::start_scan(SignatureContainerMap& signature_containers) -> void
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);

        if (!SigScannerStaticData::m_is_modular)
        {
            auto merged_module_info = SigScannerStaticData::m_modules_info[ScanTarget::MainExe];
            std::vector<SignatureContainer> merged_containers;

            for (const auto& [scan_target, outer_container] : signature_containers)
            {
                (void)scan_target;
                for (const auto& signature_container : outer_container)
                {
                    merged_containers.emplace_back(signature_container);
                }
            }

            auto* module_start_address = static_cast<uint8_t*>(merged_module_info.lpBaseOfDll);
            auto* module_end_address = module_start_address + merged_module_info.SizeOfImage;
            scanner_work_thread(module_start_address, module_end_address, info, merged_containers);

            for (auto& container : merged_containers)
            {
                container.on_scan_finished(container);
            }
            return;
        }

        for (auto& [scan_target, signature_container] : signature_containers)
        {
            auto* module_start_address = static_cast<uint8_t*>(SigScannerStaticData::m_modules_info[scan_target].lpBaseOfDll);
            auto* module_end_address = module_start_address + SigScannerStaticData::m_modules_info[scan_target].SizeOfImage;

            scanner_work_thread(module_start_address, module_end_address, info, signature_container);

            for (auto& container : signature_container)
            {
                container.on_scan_finished(container);
            }
        }
    }

    auto SinglePassScanner::string_scan(std::wstring_view string_to_scan_for, ScanTarget scan_target) -> void*
    {
        auto module = SigScannerStaticData::m_modules_info[scan_target];
        const auto address = ps_scan_wide_string(static_cast<const uint8_t*>(module.lpBaseOfDll),
                                                 module.SizeOfImage,
                                                 reinterpret_cast<const uint16_t*>(string_to_scan_for.data()),
                                                 string_to_scan_for.size());
        return reinterpret_cast<void*>(address);
    }
} // namespace RC

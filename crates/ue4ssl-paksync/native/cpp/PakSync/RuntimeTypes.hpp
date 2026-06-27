#pragma once

#include "PakSync/Config.hpp"
#include "PakSync/Protocol.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RC::PakSync
{
    struct PakManifest
    {
        std::filesystem::path path{};
        std::wstring name{};
        uint64_t size{};
        uint32_t chunk_size{DefaultChunkSize};
        uint32_t chunk_count{};
        std::array<uint8_t, 32> sha256{};
    };

    struct QueuedFrame
    {
        FrameKind kind{};
        uint64_t session_id{};
        uint32_t seq{};
        std::vector<uint8_t> bytes{};
        std::wstring label{};
    };

    struct IncomingSession
    {
        uint64_t session_id{};
        std::array<uint8_t, 32> pak_hash{};
        std::wstring name{};
        uint64_t size{};
        uint32_t chunk_size{};
        uint32_t chunk_count{};
        std::filesystem::path temp_path{};
        std::filesystem::path final_path{};
        std::vector<uint8_t> received{};
        uint64_t last_progress_ms{};
        uint64_t next_resume_retry_ms{};
        uint64_t next_retry_limit_log_ms{};
        uint32_t resume_retries{};
        uint32_t requested_until_seq{};
        bool retry_limit_logged{};
    };

    struct ControlConnection
    {
        void* connection{};
        void* control_channel{};
    };

    enum class SyncPhase : uint8_t
    {
        WaitingForConnection,
        AwaitingHostManifest,
        ControlChannelReady,
        ManifestAnnounced,
        TransferPending,
        Receiving,
        MountPending,
        AssetRegistryPending,
        ReadyToTravel,
        NoPakWork,
        MountedPartial,
    };

    struct ModuleSections
    {
        uint8_t* base{};
        size_t image_size{};
        uint8_t* text{};
        size_t text_size{};
    };

    struct PatternSpec
    {
        const char* key{};
        uint32_t pdb_rva{};
        std::vector<int> signature{};
    };

    struct ResolveResult
    {
        const PatternSpec* spec{};
        void* address{};
        void* second_address{};
        const char* method{"missing"};
        int confidence{};
        uint32_t match_count{};
    };

    struct FunctionTable
    {
        ResolveResult f_control_channel_out_bunch_ctor{};
        ResolveResult f_out_bunch_ctor{};
        ResolveResult f_in_bunch_ctor{};
        ResolveResult u_control_channel_received_bunch{};
        ResolveResult u_channel_received_raw_bunch{};
        ResolveResult u_channel_send_bunch{};
        ResolveResult u_control_channel_send_bunch{};
        ResolveResult u_net_connection_send_raw_bunch{};
        ResolveResult u_net_connection_create_channel_by_name{};
        ResolveResult f_pak_platform_file_mount{};
        ResolveResult f_pak_platform_file_mount_all_pak_files{};
        ResolveResult f_package_name_register_mount_point{};
        ResolveResult f_platform_file_manager_find_platform_file{};
        ResolveResult f_platform_file_manager_get{};
    };

    struct PakUgcResourceCache
    {
        std::vector<std::wstring> package_names{};
        std::vector<std::wstring> class_object_paths{};
        std::vector<std::wstring> visible_class_object_paths{};
        std::vector<std::wstring> map_names{};
    };
}

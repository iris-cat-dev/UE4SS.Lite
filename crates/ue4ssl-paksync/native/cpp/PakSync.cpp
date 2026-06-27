#define NOMINMAX

#include <PakSync/PakSync.hpp>

#include "PakSync/Config.hpp"
#include "PakSync/LegacyHotRefresh.hpp"
#include "PakSync/Protocol.hpp"
#include "PakSync/Resolver.hpp"
#include "PakSync/RuntimeTypes.hpp"
#include "PakSync/RuntimeUtils.hpp"
#include "PakSync/SyncStatusWindow.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Windows.h>
#include <shellapi.h>
#include <winnt.h>
#include <bcrypt.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <SehFramework.hpp>
#include <Unreal\CoreUObject\UObject\Class.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <polyhook2/Detour/x64Detour.hpp>

namespace fs = std::filesystem;

namespace RC::PakSync
{
    class PakSyncRuntime;

    namespace
    {
        constexpr size_t PendingNetGameReadyOffset = 0xA8;
        constexpr size_t PendingNetGameTickVtableIndex = 78;
        constexpr size_t PendingNetGameShadowVtableEntries = 160;
        constexpr uintptr_t PendingNetGameTickExpectedRva = 0x3942CA0;
        constexpr uint64_t PostTravelQuietMs = 30000;
        constexpr size_t UugcPackageWindowsSize = 288;
        constexpr size_t UugcPackageWindowsIdPtrOffset = 280;
        constexpr uint64_t ResumeRetryIntervalMs = 3000;
        constexpr uint64_t ResumeRetryAfterLimitMs = 10000;
        constexpr uint64_t SendFailureRetryMs = 500;
        constexpr uint32_t MaxResumeRetriesPerSession = 60;
        constexpr size_t LargeControlFrameWarningBytes = 60000;

        using ControlChannelReceivedBunchFn = void(__fastcall*)(void* channel, void* bunch);
        using ReceivedRawBunchFn = void(__fastcall*)(void* channel, void* bunch, bool* out_skip_ack, void* packet_id_range);
        using ControlChannelOutBunchCtorFn = void*(__fastcall*)(void* out_bunch, void* channel, bool close);
        using ControlChannelSendBunchFn = void*(__fastcall*)(void* channel, void* out_packet_id_range, void* bunch, bool merge);
        using PakMountFn = bool(__fastcall*)(void* pak_platform_file, const wchar_t* pak_path, uint32_t pak_order, const wchar_t* mount_point, bool notify);
        using PakMountAllPakFilesFn = int32_t(__fastcall*)(void* pak_platform_file, void* pak_folders, void* wildcard);
        using FindPlatformFileFn = void*(__fastcall*)(void* platform_file_manager, const wchar_t* name);
        using GetPlatformFileManagerFn = void*(__fastcall*)();
        using PendingNetGameTickFn = void(__fastcall*)(void* pending_net_game, float delta_seconds);

        struct DetourState
        {
            std::unique_ptr<PLH::x64Detour> control_channel_received_bunch{};
            uint64_t control_channel_received_bunch_trampoline{};
            std::unique_ptr<PLH::x64Detour> received_raw_bunch{};
            uint64_t received_raw_bunch_trampoline{};
            std::unique_ptr<PLH::x64Detour> pak_mount{};
            uint64_t pak_mount_trampoline{};
            std::unique_ptr<PLH::x64Detour> pak_mount_all{};
            uint64_t pak_mount_all_trampoline{};
        };

        static std::atomic<PakSyncRuntime*> GRuntime{};
        static ControlChannelReceivedBunchFn GOriginalControlChannelReceivedBunch{};
        static ReceivedRawBunchFn GOriginalReceivedRawBunch{};
        static PakMountFn GOriginalPakMount{};
        static PakMountAllPakFilesFn GOriginalPakMountAllPakFiles{};
        static PendingNetGameTickFn GOriginalPendingNetGameTick{};
        static std::atomic<uint64_t> GControlChannelReceivedBunchHitCount{};
        static std::atomic<uint64_t> GReceivedRawBunchHitCount{};
        static std::atomic<uint64_t> GPendingNetGameTickHitCount{};

        void __fastcall control_channel_received_bunch_detour(void* channel, void* bunch);
        void __fastcall received_raw_bunch_detour(void* channel, void* bunch, bool* out_skip_ack, void* packet_id_range);
        bool __fastcall pak_mount_detour(void* pak_platform_file, const wchar_t* pak_path, uint32_t pak_order, const wchar_t* mount_point, bool notify);
        int32_t __fastcall pak_mount_all_pak_files_detour(void* pak_platform_file, void* pak_folders, void* wildcard);
        void __fastcall pending_net_game_tick_detour(void* pending_net_game, float delta_seconds);

        struct PendingDownloadApproval
        {
            uint64_t session_id{};
            std::array<uint8_t, 32> pak_hash{};
            std::wstring name{};
            uint64_t size{};
            uint32_t chunk_size{};
            uint32_t chunk_count{};
            fs::path final_path{};
        };

    }

    class PakSyncRuntime
    {
    public:
        explicit PakSyncRuntime(PakSyncMod& owner) : m_owner(owner)
        {
            m_module_path = get_current_module_path();
            m_mod_dir = m_module_path.parent_path();
            m_config = load_config(m_mod_dir);
            m_launch_room_id = parse_launch_room_id();
            m_sync_window.set_restart_callback([this]() {
                const auto room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
                restart_with_room(room_id);
            });
            m_sync_window.set_approval_callback([this](bool approved) {
                handle_download_approval(approved);
            });
            install_early_pak_mount_hook_and_mount_active();
        }

        ~PakSyncRuntime()
        {
            m_sync_window.close();
            uninstall_detours();
        }

        void on_program_start()
        {
            Output::send<LogLevel::Normal>(STR("[UE4SSL.PakSync] program start\n"));
        }

        void install_early_pak_mount_hook_and_mount_active()
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] early pak mount init mod_dir={} launch_room={} room_dir={} pak_order={}\n"),
                    m_mod_dir.wstring(),
                    m_launch_room_id.empty() ? L"<none>" : m_launch_room_id,
                    launch_room_pak_dir().wstring(),
                    m_config.pak_order);
            if (m_config.stage_synced_paks_for_restart && m_launch_room_id.empty())
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] early pak mount disabled: missing -PakSyncRoom=<room_id>; normal launch will not load synced room pak files\n"));
            }
            m_functions = resolve_functions();
            if (!m_functions.f_pak_platform_file_mount.address)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] early pak mount skipped: FPakPlatformFile::Mount unresolved\n"));
                return;
            }
            GRuntime.store(this, std::memory_order_release);
            install_pak_mount_detour();
            install_pak_mount_all_detour();
            mount_active_paks_early();
        }

        void on_unreal_init()
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] unreal init mod_dir={} dry_run={} enable_detours={} auto_mount={} stage_restart={} prompt_restart={} pak_order={} legacy_hot_refresh={} ugc_refresh={} ugc_mount_package={} game_cache_refresh={} asset_full_scan={} host_manifest_timeout_ms={}\n"),
                    m_mod_dir.wstring(),
                    m_config.dry_run,
                    m_config.enable_detours,
                    m_config.auto_mount_verified_paks,
                    m_config.stage_synced_paks_for_restart,
                    m_config.prompt_restart_after_stage,
                    m_config.pak_order,
                    m_config.enable_legacy_hot_refresh,
                    m_config.auto_ugc_refresh,
                    m_config.auto_ugc_mount_package,
                    m_config.auto_game_cache_refresh,
                    m_config.asset_registry_full_game_scan,
                    m_config.host_manifest_timeout_ms);

            log_module_sections();
            merge_resolved_functions(resolve_functions());
            log_resolvers();
            m_resolver_ready = all_required_resolved(m_functions);
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] resolver summary ready={} control_recv=0x{:016X} control_send=0x{:016X} channel_send=0x{:016X} send_raw=0x{:016X} pak_mount=0x{:016X} pak_mount_all=0x{:016X}\n"),
                    m_resolver_ready,
                    reinterpret_cast<uintptr_t>(m_functions.u_control_channel_received_bunch.address),
                    reinterpret_cast<uintptr_t>(m_functions.u_control_channel_send_bunch.address),
                    reinterpret_cast<uintptr_t>(m_functions.u_channel_send_bunch.address),
                    reinterpret_cast<uintptr_t>(m_functions.u_net_connection_send_raw_bunch.address),
                    reinterpret_cast<uintptr_t>(m_functions.f_pak_platform_file_mount.address),
                    reinterpret_cast<uintptr_t>(m_functions.f_pak_platform_file_mount_all_pak_files.address));
            if (m_config.dump_vtables)
            {
                dump_channel_vtables();
            }

            scan_local_paks();
            update_sync_phase(discover_connections());
            register_travel_gate();

            if (m_resolver_ready && m_config.enable_detours)
            {
                install_detours();
            }
            else
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] detours not enabled: resolver_ready={} enable_detours={}\n"),
                        m_resolver_ready,
                        m_config.enable_detours);
            }

            if (!m_config.dry_run)
            {
                const auto queued_existing = m_config.stage_synced_paks_for_restart
                                                     ? enqueue_active_staged_paks_for_mount()
                                                     : (m_config.auto_mount_verified_paks ? enqueue_existing_incoming_paks_for_mount() : 0);
                if (!m_config.stage_synced_paks_for_restart && queued_existing > 0 && !m_pending_mounts.empty())
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] mounting startup staged/incoming pak(s) during Unreal init queued={} pending={}\n"),
                            queued_existing,
                            m_pending_mounts.size());
                    mount_pending_verified_paks();
                }
                else if (m_config.stage_synced_paks_for_restart && queued_existing > 0)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] room pak(s) were mounted early from launch room dir\n"));
                }
            }
        }

        void on_update()
        {
            if (should_skip_update_during_post_travel())
            {
                return;
            }

            const auto connection_count = discover_connections_for_update();
            if (connection_count > 0 && !is_ready_for_travel())
            {
                log_pending_net_game_tick_candidate_if_needed();
            }
            update_sync_phase(connection_count);
            if (connection_count > 0 && !m_queued_frames.empty() && m_resolver_ready && m_config.enable_detours)
            {
                flush_queued_frames();
            }
            if (connection_count > 0 && !m_incoming_sessions.empty())
            {
                retry_stalled_incoming_sessions();
                if (!m_queued_frames.empty() && m_resolver_ready && m_config.enable_detours)
                {
                    flush_queued_frames();
                }
            }

            if (m_config.auto_mount_verified_paks && !m_config.dry_run && !m_pending_mounts.empty())
            {
                mount_pending_verified_paks();
                update_sync_phase(discover_connections_for_update());
            }

            run_legacy_hot_refresh_update();
            update_sync_phase(discover_connections_for_update());

            if (!m_first_update_logged)
            {
                m_first_update_logged = true;
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] update ready phase={} connections={} manifests={} queued_frames={} resolver_ready={} detours={}\n"),
                        sync_phase_name(m_sync_phase),
                        connection_count,
                        m_manifests.size(),
                        m_queued_frames.size(),
                        m_resolver_ready,
                        m_detours.control_channel_received_bunch && m_detours.control_channel_received_bunch->isHooked());
                if (!m_queued_frames.empty())
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] {} frame(s) are queued for pre-travel PakSync flushing\n"),
                            m_queued_frames.size());
                }
            }
        }

        uint32_t configured_pak_order() const
        {
            return m_config.pak_order;
        }

        bool legacy_hot_refresh_enabled() const
        {
            return m_legacy_refresh.enabled(m_config);
        }

        bool legacy_hot_refresh_idle() const
        {
            return m_legacy_refresh.idle(m_config);
        }

        void run_legacy_hot_refresh_update()
        {
            const bool had_pending_scans = m_legacy_refresh.has_pending_scans();
            m_legacy_refresh.run_update(m_config, m_functions, m_pending_mounts, m_mounted_paks);
            if (had_pending_scans)
            {
                update_sync_phase(discover_connections_for_update());
            }
        }

        void mount_active_paks_from_mount_all_hook(void* pak_file)
        {
            mount_active_paks_from_pak_platform_file(pak_file, L"MountAllPakFiles");
        }

        void note_observed_control_channel(void* channel)
        {
            constexpr size_t ChannelConnectionOffset = 0x28;
            constexpr size_t ChannelIndexOffset = 0x34;
            if (!channel || !is_readable_memory(channel, ChannelIndexOffset + sizeof(int32_t)))
            {
                return;
            }

            const auto channel_index = *reinterpret_cast<int32_t*>(
                    static_cast<uint8_t*>(channel) + ChannelIndexOffset);
            if (channel_index != 0)
            {
                return;
            }

            if (!is_readable_memory(channel, ChannelConnectionOffset + sizeof(void*)))
            {
                return;
            }

            auto* connection = *reinterpret_cast<void**>(
                    static_cast<uint8_t*>(channel) + ChannelConnectionOffset);
            if (!connection || !is_readable_memory(connection, sizeof(void*)))
            {
                return;
            }

            m_last_control_connection = ControlConnection{connection, channel};
            m_last_control_connection_observed_ms = GetTickCount64();
            probe_pending_net_game_tick_candidate(L"control_channel");
            if (!m_last_control_connection_logged)
            {
                m_last_control_connection_logged = true;
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] observed ControlChannel connection=0x{:016X} control_channel=0x{:016X}\n"),
                        reinterpret_cast<uintptr_t>(connection),
                        reinterpret_cast<uintptr_t>(channel));
            }
        }

        bool should_skip_update_during_post_travel()
        {
            if (m_post_travel_quiet_until_ms == 0)
            {
                return false;
            }
            if (!m_queued_frames.empty() ||
                !m_incoming_sessions.empty() ||
                !m_pending_mounts.empty() ||
                m_legacy_refresh.has_pending_scans() ||
                m_legacy_refresh.game_cache_refresh_pending())
            {
                m_post_travel_quiet_until_ms = 0;
                return false;
            }

            const auto now = GetTickCount64();
            if (now >= m_post_travel_quiet_until_ms)
            {
                m_post_travel_quiet_until_ms = 0;
                return false;
            }
            return true;
        }

        bool try_handle_bunch(void* channel, void* bunch)
        {
            auto bytes = copy_bunch_payload_bytes(bunch);
            if (!bytes)
            {
                return false;
            }

            auto frame = find_paksync_frame(*bytes);
            if (!frame)
            {
                return false;
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] PakSync frame detected in incoming bunch channel=0x{:016X} bunch=0x{:016X} bytes={}\n"),
                    reinterpret_cast<uintptr_t>(channel),
                    reinterpret_cast<uintptr_t>(bunch),
                    frame->size());

            const bool handled = handle_frame_bytes(*frame);
            if (handled)
            {
                update_sync_phase(discover_connections());
            }
            return handled;
        }

        bool should_gate_pending_net_game_tick(void* pending_net_game) const
        {
            return m_config.block_travel_until_ready &&
                   !m_config.dry_run &&
                   m_tracked_pending_net_game != nullptr &&
                   pending_net_game == m_tracked_pending_net_game &&
                   m_pending_net_game_tick_hooked;
        }

        void after_pending_net_game_tick(void* pending_net_game)
        {
            if (!should_gate_pending_net_game_tick(pending_net_game))
            {
                return;
            }
            if (!is_readable_memory(pending_net_game, PendingNetGameReadyOffset + sizeof(uint8_t)))
            {
                return;
            }

            const bool ready = *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset) != 0;
            if (!ready)
            {
                return;
            }

            if (is_ready_for_travel())
            {
                if (m_pending_ready_allowed_logs < 8 || (m_pending_ready_allowed_logs % 120) == 0)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] PendingNetGame ready allowed phase={} mounted={} partial={}\n"),
                            sync_phase_name(m_sync_phase),
                            m_mounted_paks.size(),
                            m_legacy_refresh.partial_hot_reload_detected());
                }
                ++m_pending_ready_allowed_logs;
                m_post_travel_quiet_until_ms = GetTickCount64() + PostTravelQuietMs;
                restore_pending_net_game_tick_vtable();
                return;
            }

            if (!clear_pending_net_game_ready_flag(pending_net_game))
            {
                return;
            }
            m_pending_ready_was_suppressed = true;
            if (m_pending_ready_suppression_logs < 16 || (m_pending_ready_suppression_logs % 60) == 0)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] PendingNetGame ready suppressed phase={} queued={} incoming={} pending_mounts={} pending_scans={} mounted={} host_manifest_complete={}\n"),
                        sync_phase_name(m_sync_phase),
                        m_queued_frames.size(),
                        m_incoming_sessions.size(),
                        m_pending_mounts.size(),
                        m_legacy_refresh.pending_scan_count(),
                        m_mounted_paks.size(),
                        m_host_manifest_complete);
            }
            ++m_pending_ready_suppression_logs;
        }

    private:
        void flush_queued_frames()
        {
            const auto now = GetTickCount64();
            if (m_next_queued_frame_flush_ms != 0 && now < m_next_queued_frame_flush_ms)
            {
                return;
            }
            if (m_flushing_queued_frames)
            {
                if (m_queued_frame_flush_started_ms != 0 && now - m_queued_frame_flush_started_ms > 1000)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] queued frame flush was stuck for {}ms; resetting flush guard queued={}\n"),
                            now - m_queued_frame_flush_started_ms,
                            m_queued_frames.size());
                    m_flushing_queued_frames = false;
                    m_queued_frame_flush_started_ms = 0;
                }
                else
                {
                    return;
                }
            }
            if (!m_functions.f_control_channel_out_bunch_ctor.address ||
                !m_functions.u_control_channel_send_bunch.address)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] cannot flush frames: send resolver incomplete\n"));
                return;
            }

            void* connection = nullptr;
            void* channel = nullptr;
            if (auto control_connection = find_control_connection())
            {
                connection = control_connection->connection;
                channel = control_connection->control_channel;
            }

            if (!channel)
            {
                return;
            }

            m_flushing_queued_frames = true;
            m_queued_frame_flush_started_ms = now;
            struct FlushGuard
            {
                bool& flag;
                uint64_t& started_ms;
                ~FlushGuard()
                {
                    flag = false;
                    started_ms = 0;
                }
            } flush_guard{m_flushing_queued_frames, m_queued_frame_flush_started_ms};
            constexpr size_t OutBunchStorageSize = 0x200;
            constexpr uint8_t PakSyncControlMessage = 0xFD;
            constexpr size_t MaxFramesPerFlush = 2;
            auto construct_bunch = reinterpret_cast<ControlChannelOutBunchCtorFn>(m_functions.f_control_channel_out_bunch_ctor.address);
            auto send_bunch = reinterpret_cast<ControlChannelSendBunchFn>(m_functions.u_control_channel_send_bunch.address);

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] flushing {} queued frame(s) via connection=0x{:016X} control_channel=0x{:016X} max_per_flush={}\n"),
                    m_queued_frames.size(),
                    reinterpret_cast<uintptr_t>(connection),
                    reinterpret_cast<uintptr_t>(channel),
                    MaxFramesPerFlush);

            size_t sent = 0;
            while (!m_queued_frames.empty())
            {
                auto queued = std::move(m_queued_frames.front());
                m_queued_frames.erase(m_queued_frames.begin());
                if (should_drop_outbound_catalog_frame_for_join_client(queued))
                {
                    if (!m_join_client_outbound_catalog_drop_logged)
                    {
                        m_join_client_outbound_catalog_drop_logged = true;
                        Output::send<LogLevel::Warning>(
                                STR("[UE4SSL.PakSync] dropping outbound local catalog frame(s) while joining as client; waiting for host manifest instead\n"));
                    }
                    Output::send<LogLevel::Verbose>(
                            STR("[UE4SSL.PakSync] dropped outbound frame {} kind={} during client join\n"),
                            queued.label,
                            static_cast<uint32_t>(queued.kind));
                    continue;
                }

                alignas(16) std::array<uint8_t, OutBunchStorageSize> bunch_storage{};
                alignas(8) std::array<uint8_t, 16> packet_id_range{};

                void* bunch = construct_bunch(bunch_storage.data(), channel, false);
                if (!bunch)
                {
                    Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] failed to construct FControlChannelOutBunch\n"));
                    m_queued_frames.insert(m_queued_frames.begin(), std::move(queued));
                    m_next_queued_frame_flush_ms = GetTickCount64() + SendFailureRetryMs;
                    break;
                }

                const uint8_t message = PakSyncControlMessage;
                if (!append_bunch_bits(bunch, std::span<const uint8_t>{&message, 1}) ||
                    !append_bunch_bits(bunch, queued.bytes))
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] failed to append frame {} bytes={}\n"),
                            queued.label,
                            queued.bytes.size());
                    m_queued_frames.insert(m_queued_frames.begin(), std::move(queued));
                    m_next_queued_frame_flush_ms = GetTickCount64() + SendFailureRetryMs;
                    break;
                }

                send_bunch(channel, packet_id_range.data(), bunch, true);
                m_next_queued_frame_flush_ms = GetTickCount64() + 10;
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] flushed frame {} bytes={}\n"),
                        queued.label,
                        queued.bytes.size());
                ++sent;

                if (sent >= MaxFramesPerFlush)
                {
                    break;
                }
            }

        }

        bool should_drop_outbound_catalog_frame_for_join_client(const QueuedFrame& frame) const
        {
            if ((!m_tracked_pending_net_game && !m_join_client_observed_pending_net_game) ||
                !m_config.block_travel_until_ready)
            {
                return false;
            }

            switch (frame.kind)
            {
            case FrameKind::Manifest:
            case FrameKind::Chunk:
            case FrameKind::ManifestEnd:
            case FrameKind::NoWork:
                return true;
            default:
                return false;
            }
        }

        void log_resolvers() const
        {
            log_resolve_result(m_functions.f_control_channel_out_bunch_ctor);
            log_resolve_result(m_functions.f_out_bunch_ctor);
            log_resolve_result(m_functions.f_in_bunch_ctor);
            log_resolve_result(m_functions.u_control_channel_received_bunch);
            log_resolve_result(m_functions.u_channel_received_raw_bunch);
            log_resolve_result(m_functions.u_channel_send_bunch);
            log_resolve_result(m_functions.u_control_channel_send_bunch);
            log_resolve_result(m_functions.u_net_connection_send_raw_bunch);
            log_resolve_result(m_functions.u_net_connection_create_channel_by_name);
            log_resolve_result(m_functions.f_pak_platform_file_mount);
            log_resolve_result(m_functions.f_pak_platform_file_mount_all_pak_files);
            log_resolve_result(m_functions.f_package_name_register_mount_point);
            log_resolve_result(m_functions.f_platform_file_manager_find_platform_file);
            log_resolve_result(m_functions.f_platform_file_manager_get);
        }

        static void merge_resolve_result(ResolveResult& current, const ResolveResult& incoming)
        {
            if (!incoming.address)
            {
                return;
            }
            if (!current.address || incoming.confidence > current.confidence)
            {
                current = incoming;
            }
        }

        void merge_resolved_functions(const FunctionTable& incoming)
        {
            merge_resolve_result(m_functions.f_control_channel_out_bunch_ctor, incoming.f_control_channel_out_bunch_ctor);
            merge_resolve_result(m_functions.f_out_bunch_ctor, incoming.f_out_bunch_ctor);
            merge_resolve_result(m_functions.f_in_bunch_ctor, incoming.f_in_bunch_ctor);
            merge_resolve_result(m_functions.u_control_channel_received_bunch, incoming.u_control_channel_received_bunch);
            merge_resolve_result(m_functions.u_channel_received_raw_bunch, incoming.u_channel_received_raw_bunch);
            merge_resolve_result(m_functions.u_channel_send_bunch, incoming.u_channel_send_bunch);
            merge_resolve_result(m_functions.u_control_channel_send_bunch, incoming.u_control_channel_send_bunch);
            merge_resolve_result(m_functions.u_net_connection_send_raw_bunch, incoming.u_net_connection_send_raw_bunch);
            merge_resolve_result(m_functions.u_net_connection_create_channel_by_name, incoming.u_net_connection_create_channel_by_name);
            merge_resolve_result(m_functions.f_pak_platform_file_mount, incoming.f_pak_platform_file_mount);
            merge_resolve_result(m_functions.f_pak_platform_file_mount_all_pak_files, incoming.f_pak_platform_file_mount_all_pak_files);
            merge_resolve_result(m_functions.f_package_name_register_mount_point, incoming.f_package_name_register_mount_point);
            merge_resolve_result(m_functions.f_platform_file_manager_find_platform_file, incoming.f_platform_file_manager_find_platform_file);
            merge_resolve_result(m_functions.f_platform_file_manager_get, incoming.f_platform_file_manager_get);
        }

        auto find_control_connection() -> std::optional<ControlConnection>
        {
            constexpr uint64_t ObservedControlChannelTtlMs = 30000;
            const auto now = GetTickCount64();
            if (m_last_control_connection_observed_ms == 0 ||
                now - m_last_control_connection_observed_ms > ObservedControlChannelTtlMs)
            {
                return std::nullopt;
            }

            auto control_connection = m_last_control_connection;
            if (!control_connection.control_channel ||
                !control_connection.connection ||
                !is_readable_memory(control_connection.control_channel, sizeof(void*)) ||
                !is_readable_memory(control_connection.connection, sizeof(void*)))
            {
                m_last_control_connection = {};
                m_last_control_connection_observed_ms = 0;
                m_last_control_connection_logged = false;
                return std::nullopt;
            }
            return control_connection;
        }

        size_t discover_connections()
        {
            const size_t connection_count = find_control_connection() ? 1 : 0;
            if (connection_count != m_last_connection_count)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] discovered {} NetConnection object(s)\n"),
                        connection_count);
                m_last_connection_count = connection_count;
            }
            return connection_count;
        }

        size_t discover_connections_for_update()
        {
            const auto now = GetTickCount64();
            const bool join_in_progress = m_cached_connection_count > 0 && !is_ready_for_travel();
            const bool urgent = join_in_progress ||
                                !m_queued_frames.empty() ||
                                !m_incoming_sessions.empty() ||
                                !m_pending_mounts.empty() ||
                                m_legacy_refresh.has_pending_scans();
            const bool post_join_idle = m_cached_connection_count == 0 &&
                                        (m_sync_phase == SyncPhase::MountedPartial ||
                                         m_sync_phase == SyncPhase::ReadyToTravel ||
                                         (m_sync_phase == SyncPhase::WaitingForConnection && !m_mounted_paks.empty()));
            if (post_join_idle && !urgent)
            {
                return 0;
            }
            if (!urgent && now < m_next_connection_probe_ms)
            {
                return m_cached_connection_count;
            }

            m_next_connection_probe_ms = now + (urgent ? 50 : 500);
            m_cached_connection_count = discover_connections();
            return m_cached_connection_count;
        }

        void ensure_outbound_manifest_announced_for_connection()
        {
            if (m_outbound_manifest_queued_for_connection)
            {
                return;
            }

            if (m_queued_frames.empty())
            {
                queue_local_manifest_announcement();
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] queued local manifest announcement for new connection frames={} manifests={}\n"),
                        m_queued_frames.size(),
                        m_manifests.size());
            }
            m_outbound_manifest_queued_for_connection = true;
        }

        void apply_host_manifest_timeout(size_t connection_count)
        {
            if (connection_count == 0 || m_host_manifest_complete)
            {
                m_host_manifest_wait_start_ms = 0;
                m_host_manifest_timeout_logged = false;
                return;
            }
            if (m_config.host_manifest_timeout_ms == 0)
            {
                return;
            }

            const auto now = GetTickCount64();
            if (m_host_manifest_wait_start_ms == 0)
            {
                m_host_manifest_wait_start_ms = now;
                return;
            }
            if (now - m_host_manifest_wait_start_ms < m_config.host_manifest_timeout_ms)
            {
                return;
            }
            if (!m_incoming_sessions.empty())
            {
                return;
            }
            if (!m_pending_download_approvals.empty())
            {
                return;
            }

            m_host_manifest_complete = true;
            if (!m_host_manifest_timeout_logged)
            {
                m_host_manifest_timeout_logged = true;
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] host manifest timeout after {}ms; assuming legacy/no PakSync peer and allowing travel (had_work={} mounted={} pending_mounts={} pending_scans={})\n"),
                        now - m_host_manifest_wait_start_ms,
                        m_received_manifest_had_work,
                        m_mounted_paks.size(),
                        m_pending_mounts.size(),
                        m_legacy_refresh.pending_scan_count());
            }
        }

        void update_sync_phase(size_t connection_count)
        {
            if (connection_count == 0 && m_sync_phase != SyncPhase::WaitingForConnection)
            {
                const bool keep_post_mount_refresh =
                        legacy_hot_refresh_enabled() &&
                        !m_mounted_paks.empty() &&
                        !m_legacy_refresh.game_cache_refresh_attempted() &&
                        m_legacy_refresh.game_cache_refresh_pending_or_waiting_or_post_seen();
                m_host_manifest_complete = false;
                m_received_manifest_had_work = false;
                m_incoming_sessions.clear();
                m_queued_frames.clear();
                m_legacy_refresh.reset_for_join(keep_post_mount_refresh);
                if (keep_post_mount_refresh)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] preserving post-mount generic cache refresh after connection object disappeared mounted={} pending={} waiting={} load_map_post_seen={}\n"),
                            m_mounted_paks.size(),
                            m_legacy_refresh.cache_pending(),
                            m_legacy_refresh.cache_waiting(),
                            m_legacy_refresh.game_cache_refresh_pending_or_waiting_or_post_seen());
                }
                m_join_client_observed_pending_net_game = false;
                m_pending_ready_was_suppressed = false;
                m_host_manifest_wait_start_ms = 0;
                m_host_manifest_timeout_logged = false;
                m_outbound_manifest_queued_for_connection = false;
                m_join_client_outbound_catalog_drop_logged = false;
                m_staged_manifest_seen_this_join = false;
                m_staged_room_id.clear();
                m_staging_manifest_hashes.clear();
                m_staged_active_paks.clear();
                m_restart_required_after_stage = false;
                m_restart_prompt_shown = false;
                m_missing_pak_prompt_shown = false;
                m_missing_host_paks.clear();
                restore_pending_net_game_tick_vtable();
            }
            if (connection_count > 0)
            {
                ensure_outbound_manifest_announced_for_connection();
                apply_host_manifest_timeout(connection_count);
            }
            else
            {
                apply_host_manifest_timeout(connection_count);
            }

            SyncPhase next = SyncPhase::WaitingForConnection;
            if (connection_count == 0)
            {
                next = SyncPhase::WaitingForConnection;
            }
            else if (!m_pending_mounts.empty())
            {
                next = SyncPhase::MountPending;
            }
            else if (legacy_hot_refresh_enabled() && m_legacy_refresh.has_pending_scans())
            {
                next = SyncPhase::AssetRegistryPending;
            }
            else if (legacy_hot_refresh_enabled() && m_legacy_refresh.post_mount_refresh_in_progress())
            {
                next = SyncPhase::AssetRegistryPending;
            }
            else if (legacy_hot_refresh_enabled() && m_legacy_refresh.game_cache_refresh_pending())
            {
                next = SyncPhase::AssetRegistryPending;
            }
            else if (!m_incoming_sessions.empty())
            {
                next = SyncPhase::Receiving;
            }
            else if (!m_pending_download_approvals.empty())
            {
                next = SyncPhase::Receiving;
            }
            else if (!m_host_manifest_complete)
            {
                next = SyncPhase::AwaitingHostManifest;
            }
            else if (m_config.stage_synced_paks_for_restart && m_restart_required_after_stage)
            {
                next = SyncPhase::ReadyToTravel;
            }
            else if (!m_received_manifest_had_work && m_mounted_paks.empty())
            {
                next = SyncPhase::NoPakWork;
            }
            else if (m_received_manifest_had_work && m_mounted_paks.empty())
            {
                next = SyncPhase::ReadyToTravel;
            }
            else
            {
                next = m_legacy_refresh.partial_hot_reload_detected() ? SyncPhase::MountedPartial : SyncPhase::ReadyToTravel;
            }

            if (next != m_sync_phase)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] sync phase {} -> {} (connections={} manifests={} queued={} incoming={} pending_mounts={} pending_scans={} mounted={} host_manifest_complete={})\n"),
                        sync_phase_name(m_sync_phase),
                        sync_phase_name(next),
                        connection_count,
                        m_manifests.size(),
                        m_queued_frames.size(),
                        m_incoming_sessions.size(),
                        m_pending_mounts.size(),
                        m_legacy_refresh.pending_scan_count(),
                        m_mounted_paks.size(),
                        m_host_manifest_complete);
                m_sync_phase = next;
            }
            restore_suppressed_pending_ready_if_complete();
        }

        bool is_ready_for_travel() const
        {
            return receiver_side_ready_for_travel();
        }

        bool receiver_side_ready_for_travel() const
        {
            return m_host_manifest_complete &&
                   m_pending_download_approvals.empty() &&
                   (m_config.stage_synced_paks_for_restart || m_incoming_sessions.empty()) &&
                   (m_config.stage_synced_paks_for_restart || m_pending_mounts.empty()) &&
                   legacy_hot_refresh_idle();
        }

        void restore_suppressed_pending_ready_if_complete()
        {
            if (!m_pending_ready_was_suppressed || !receiver_side_ready_for_travel() || !m_tracked_pending_net_game)
            {
                return;
            }
            if (!set_pending_net_game_ready_flag(m_tracked_pending_net_game))
            {
                return;
            }

            m_pending_ready_was_suppressed = false;
            if (m_pending_ready_restore_logs < 8 || (m_pending_ready_restore_logs % 120) == 0)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] PendingNetGame ready restored after PakSync completion phase={} mounted={} partial={}\n"),
                        sync_phase_name(m_sync_phase),
                        m_mounted_paks.size(),
                        m_legacy_refresh.partial_hot_reload_detected());
            }
            ++m_pending_ready_restore_logs;
        }

        void register_travel_gate()
        {
            if (m_load_map_pre_callback != Unreal::Hook::ERROR_ID &&
                m_load_map_post_callback != Unreal::Hook::ERROR_ID)
            {
                return;
            }

            Unreal::Hook::FCallbackOptions options{};
            options.OwnerModName = STR("UE4SSL.PakSync");
            options.HookName = STR("PakSyncLoadMapGate");
            if (m_load_map_pre_callback == Unreal::Hook::ERROR_ID)
            {
                m_load_map_pre_callback = Unreal::Hook::RegisterLoadMapPreCallback(
                        [this](auto& info,
                               Unreal::UEngine* engine,
                               Unreal::FWorldContext& world_context,
                               Unreal::FURL url,
                               Unreal::UPendingNetGame* pending_net_game,
                               Unreal::FString& error) {
                            handle_load_map_pre(info, engine, world_context, url, pending_net_game, error);
                        },
                        options);
            }
            options.HookName = STR("PakSyncLoadMapPost");
            if (m_load_map_post_callback == Unreal::Hook::ERROR_ID)
            {
                m_load_map_post_callback = Unreal::Hook::RegisterLoadMapPostCallback(
                        [this](auto& info,
                               Unreal::UEngine* engine,
                               Unreal::FWorldContext& world_context,
                               Unreal::FURL url,
                               Unreal::UPendingNetGame* pending_net_game,
                               Unreal::FString& error) {
                            handle_load_map_post(info, engine, world_context, url, pending_net_game, error);
                        },
                        options);
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] LoadMap callbacks registered pre_id=0x{:016X} post_id=0x{:016X} block_travel_until_ready={} dry_run={}\n"),
                    m_load_map_pre_callback,
                    m_load_map_post_callback,
                    m_config.block_travel_until_ready,
                    m_config.dry_run);
        }

        void handle_load_map_pre(Unreal::Hook::TCallbackIterationData<bool>& info,
                                 Unreal::UEngine* engine,
                                 Unreal::FWorldContext&,
                                 Unreal::FURL,
                                 Unreal::UPendingNetGame* pending_net_game,
                                 Unreal::FString& error)
        {
            log_actual_pending_net_game_candidate(pending_net_game);
            const bool ready = is_ready_for_travel();
            if (pending_net_game)
            {
                m_join_client_observed_pending_net_game = true;
                if (try_install_pending_net_game_tick_detour(pending_net_game, L"LoadMapPre") && !ready)
                {
                    const bool cleared = clear_pending_net_game_ready_flag(pending_net_game);
                    if (cleared)
                    {
                        m_pending_ready_was_suppressed = true;
                    }
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] LoadMapPre fallback installed PendingNetGame gate and clear_ready={} phase={}\n"),
                            cleared,
                            sync_phase_name(m_sync_phase));
                }
            }
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] LoadMapPre diagnostic engine=0x{:016X} pending_net_game=0x{:016X} phase={} ready={} dry_run={} block={} queued={} incoming={} pending_mounts={} pending_scans={} mounted={} host_manifest_complete={}\n"),
                    reinterpret_cast<uintptr_t>(engine),
                    reinterpret_cast<uintptr_t>(pending_net_game),
                    sync_phase_name(m_sync_phase),
                    ready,
                    m_config.dry_run,
                    m_config.block_travel_until_ready,
                    m_queued_frames.size(),
                    m_incoming_sessions.size(),
                    m_pending_mounts.size(),
                    m_legacy_refresh.pending_scan_count(),
                    m_mounted_paks.size(),
                    m_host_manifest_complete);

            if (!ready && m_config.block_travel_until_ready && !m_config.dry_run && pending_net_game)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] LoadMapPre reached before PakSync ready; this callback is diagnostic only (phase={} queued={} incoming={} pending_mounts={} pending_scans={} host_manifest_complete={})\n"),
                        sync_phase_name(m_sync_phase),
                        m_queued_frames.size(),
                        m_incoming_sessions.size(),
                        m_pending_mounts.size(),
                        m_legacy_refresh.pending_scan_count(),
                        m_host_manifest_complete);
                (void)info;
                (void)error;
            }
            else if (ready)
            {
                m_post_travel_quiet_until_ms = GetTickCount64() + PostTravelQuietMs;
                restore_pending_net_game_tick_vtable();
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] ReadyToTravel gate passed phase={} mounted={} partial={}\n"),
                        sync_phase_name(m_sync_phase),
                        m_mounted_paks.size(),
                        m_legacy_refresh.partial_hot_reload_detected());
            }
        }

        void handle_load_map_post(Unreal::Hook::TCallbackIterationData<bool>&,
                                  Unreal::UEngine* engine,
                                  Unreal::FWorldContext&,
                                  Unreal::FURL,
                                  Unreal::UPendingNetGame* pending_net_game,
                                  Unreal::FString&)
        {
            m_legacy_refresh.mark_load_map_post_seen(m_mounted_paks.size());
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] LoadMapPost diagnostic engine=0x{:016X} pending_net_game=0x{:016X} phase={} mounted={} partial={} cache_waiting={} cache_pending={}\n"),
                    reinterpret_cast<uintptr_t>(engine),
                    reinterpret_cast<uintptr_t>(pending_net_game),
                    sync_phase_name(m_sync_phase),
                    m_mounted_paks.size(),
                    m_legacy_refresh.partial_hot_reload_detected(),
                    m_legacy_refresh.cache_waiting(),
                    m_legacy_refresh.cache_pending());

            m_post_travel_quiet_until_ms = 0;
        }

        void log_actual_pending_net_game_candidate(void* pending_net_game)
        {
            if (!pending_net_game || m_actual_pending_net_game_candidate_logged)
            {
                return;
            }
            if (!is_readable_memory(pending_net_game, PendingNetGameReadyOffset + sizeof(uint8_t)))
            {
                return;
            }

            auto** vtable = *reinterpret_cast<void***>(pending_net_game);
            if (!vtable || !is_readable_memory(vtable, (PendingNetGameTickVtableIndex + 1) * sizeof(void*)))
            {
                return;
            }

            const auto sections = get_module_sections();
            auto* target = vtable[PendingNetGameTickVtableIndex];
            auto* target_bytes = reinterpret_cast<uint8_t*>(target);
            const bool in_text = sections.base && sections.text && target_bytes >= sections.text && target_bytes < sections.text + sections.text_size;
            const uintptr_t rva = in_text ? static_cast<uintptr_t>(target_bytes - sections.base) : 0;
            const auto ready = *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset);
            const auto seamless = is_readable_memory(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset + 1, sizeof(uint8_t))
                                      ? *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset + 1)
                                      : 0;

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] actual PendingNetGame candidate pending=0x{:016X} vtable=0x{:016X} slot={} target=0x{:016X} rva=0x{:X} ready={} seamless={} diagnostic only\n"),
                    reinterpret_cast<uintptr_t>(pending_net_game),
                    reinterpret_cast<uintptr_t>(vtable),
                    PendingNetGameTickVtableIndex,
                    reinterpret_cast<uintptr_t>(target),
                    rva,
                    ready,
                    seamless);
            m_actual_pending_net_game_candidate_logged = true;
        }

        bool clear_pending_net_game_ready_flag(void* pending_net_game)
        {
            if (!pending_net_game || !is_readable_memory(pending_net_game, PendingNetGameReadyOffset + sizeof(uint8_t)))
            {
                return false;
            }

            auto* ready_ptr = reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset);
            if (*ready_ptr == 0)
            {
                return false;
            }

            *ready_ptr = 0;
            return true;
        }

        bool set_pending_net_game_ready_flag(void* pending_net_game)
        {
            if (!pending_net_game || !is_readable_memory(pending_net_game, PendingNetGameReadyOffset + sizeof(uint8_t)))
            {
                return false;
            }

            auto* ready_ptr = reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(pending_net_game) + PendingNetGameReadyOffset);
            if (*ready_ptr != 0)
            {
                return true;
            }

            *ready_ptr = 1;
            return true;
        }

        bool try_install_pending_net_game_tick_detour(void* pending_net_game, const wchar_t* source)
        {
            if (!m_config.enable_detours || !pending_net_game)
            {
                return false;
            }
            if (m_pending_net_game_tick_hooked && pending_net_game == m_tracked_pending_net_game)
            {
                return true;
            }
            if (m_pending_net_game_tick_hooked)
            {
                restore_pending_net_game_tick_vtable();
            }
            if (!is_readable_memory(pending_net_game, PendingNetGameReadyOffset + sizeof(uint8_t)))
            {
                return false;
            }

            auto** vtable = *reinterpret_cast<void***>(pending_net_game);
            if (!vtable || !is_readable_memory(vtable, (PendingNetGameTickVtableIndex + 1) * sizeof(void*)))
            {
                return false;
            }

            auto* target = vtable[PendingNetGameTickVtableIndex];
            const auto sections = get_module_sections();
            auto* target_bytes = reinterpret_cast<uint8_t*>(target);
            const bool in_text = sections.base && sections.text && target_bytes >= sections.text && target_bytes < sections.text + sections.text_size;
            if (!target || !in_text)
            {
                return false;
            }

            const auto rva = static_cast<uintptr_t>(target_bytes - sections.base);
            if (rva != PendingNetGameTickExpectedRva)
            {
                if (!m_pending_net_game_rejection_logged)
                {
                    m_pending_net_game_rejection_logged = true;
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] PendingNetGame tick target rejected source={} rva=0x{:X} expected=0x{:X}; travel gate not installed\n"),
                            source ? source : L"<null>",
                            rva,
                            PendingNetGameTickExpectedRva);
                }
                return false;
            }
            if (!is_readable_memory(vtable, PendingNetGameShadowVtableEntries * sizeof(void*)))
            {
                if (!m_pending_net_game_rejection_logged)
                {
                    m_pending_net_game_rejection_logged = true;
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] PendingNetGame vtable rejected source={} entries={} vtable=0x{:016X}; travel gate not installed\n"),
                            source ? source : L"<null>",
                            PendingNetGameShadowVtableEntries,
                            reinterpret_cast<uintptr_t>(vtable));
                }
                return false;
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] installing PendingNetGame::Tick vtable detour source={} pending=0x{:016X} vtable=0x{:016X} target=0x{:016X} rva=0x{:X}\n"),
                    source ? source : L"<null>",
                    reinterpret_cast<uintptr_t>(pending_net_game),
                    reinterpret_cast<uintptr_t>(vtable),
                    reinterpret_cast<uintptr_t>(target),
                    rva);

            m_pending_net_game_vtable_shadow.assign(vtable, vtable + PendingNetGameShadowVtableEntries);
            m_pending_net_game_vtable_shadow[PendingNetGameTickVtableIndex] = reinterpret_cast<void*>(&pending_net_game_tick_detour);
            m_original_pending_net_game_vtable = vtable;
            GOriginalPendingNetGameTick = reinterpret_cast<PendingNetGameTickFn>(target);
            *reinterpret_cast<void***>(pending_net_game) = m_pending_net_game_vtable_shadow.data();
            m_tracked_pending_net_game = pending_net_game;
            m_pending_net_game_tick_hooked = true;

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] PendingNetGame::Tick vtable detour installed=true source={} original=0x{:016X} shadow=0x{:016X}\n"),
                    source ? source : L"<null>",
                    reinterpret_cast<uintptr_t>(target),
                    reinterpret_cast<uintptr_t>(m_pending_net_game_vtable_shadow.data()));
            return true;
        }

        void restore_pending_net_game_tick_vtable()
        {
            if (m_pending_net_game_tick_hooked &&
                m_tracked_pending_net_game &&
                m_original_pending_net_game_vtable &&
                !m_pending_net_game_vtable_shadow.empty() &&
                is_readable_memory(m_tracked_pending_net_game, sizeof(void*)))
            {
                auto*** object_vtable = reinterpret_cast<void***>(m_tracked_pending_net_game);
                if (*object_vtable == m_pending_net_game_vtable_shadow.data())
                {
                    *object_vtable = m_original_pending_net_game_vtable;
                    Output::send<LogLevel::Verbose>(
                            STR("[UE4SSL.PakSync] PendingNetGame::Tick vtable detour restored pending=0x{:016X} original_vtable=0x{:016X}\n"),
                            reinterpret_cast<uintptr_t>(m_tracked_pending_net_game),
                            reinterpret_cast<uintptr_t>(m_original_pending_net_game_vtable));
                }
            }

            m_pending_net_game_tick_hooked = false;
            m_tracked_pending_net_game = nullptr;
            m_original_pending_net_game_vtable = nullptr;
            m_pending_net_game_vtable_shadow.clear();
            GOriginalPendingNetGameTick = nullptr;
        }

        void dump_channel_vtables()
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] vtable diagnostics enabled entries={}\n"),
                    m_config.vtable_entries);

            dump_object_lookup(STR("/Script/Engine.Channel"));
            dump_object_lookup(STR("/Script/Engine.ControlChannel"));
            dump_object_lookup(STR("/Script/Engine.NetConnection"));
            dump_object_lookup(STR("/Script/Engine.Default__Channel"));
            dump_object_lookup(STR("/Script/Engine.Default__ControlChannel"));
            dump_object_lookup(STR("/Script/Engine.Default__NetConnection"));

            dump_first_instance_vtable(STR("Channel"));
            dump_first_instance_vtable(STR("ControlChannel"));
            dump_first_instance_vtable(STR("NetConnection"));
        }

        void dump_object_lookup(const CharType* path)
        {
            auto* object = Unreal::UObjectGlobals::StaticFindObject(nullptr, nullptr, path);
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] object lookup {} -> 0x{:016X}\n"),
                    path,
                    reinterpret_cast<uintptr_t>(object));
            if (object)
            {
                dump_vtable(object, object->GetFullName());
            }
        }

        void dump_first_instance_vtable(const CharType* class_name)
        {
            std::vector<Unreal::UObject*> objects{};
            Unreal::UObjectGlobals::FindAllInstancesOfClass(class_name, objects);
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] FindAllInstancesOfClass {} count={}\n"),
                    class_name,
                    objects.size());
            if (!objects.empty() && objects.front())
            {
                dump_vtable(objects.front(), objects.front()->GetFullName());
            }
        }

        void dump_vtable(Unreal::UObject* object, const StringType& label)
        {
            const auto sections = get_module_sections();
            auto** vtable = object ? *reinterpret_cast<void***>(object) : nullptr;
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] vtable {} object=0x{:016X} vtable=0x{:016X}\n"),
                    label,
                    reinterpret_cast<uintptr_t>(object),
                    reinterpret_cast<uintptr_t>(vtable));
            if (!vtable)
            {
                return;
            }

            for (uint32_t index = 0; index < m_config.vtable_entries; ++index)
            {
                void* fn = vtable[index];
                const auto* fn_bytes = reinterpret_cast<uint8_t*>(fn);
                const bool in_text = sections.text && fn_bytes >= sections.text && fn_bytes < sections.text + sections.text_size;
                if (!in_text)
                {
                    continue;
                }

                const auto rva = static_cast<uintptr_t>(fn_bytes - sections.base);
                Output::send<LogLevel::Verbose>(
                        STR("[UE4SSL.PakSync] vtable-slot {}[{}] fn=0x{:016X} rva=0x{:X}\n"),
                        label,
                        index,
                        reinterpret_cast<uintptr_t>(fn),
                        rva);
            }
        }

        bool probe_pending_net_game_tick_candidate(const wchar_t* source)
        {
            if (!m_resolver_ready || !m_config.enable_detours || !m_config.block_travel_until_ready || m_config.dry_run)
            {
                return false;
            }
            if (m_pending_net_game_tick_hooked && m_tracked_pending_net_game)
            {
                return true;
            }

            std::vector<Unreal::UObject*> pending_games{};
            Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("PendingNetGame"), pending_games);
            if (pending_games.empty())
            {
                Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("PendingNetGameBase"), pending_games);
            }
            if (pending_games.empty())
            {
                return false;
            }

            const auto sections = get_module_sections();
            for (auto* object : pending_games)
            {
                if (!object || !is_readable_memory(object, PendingNetGameReadyOffset + sizeof(uint8_t)))
                {
                    continue;
                }

                auto** vtable = *reinterpret_cast<void***>(object);
                if (!vtable || !is_readable_memory(vtable, (PendingNetGameTickVtableIndex + 1) * sizeof(void*)))
                {
                    continue;
                }

                auto* target = vtable[PendingNetGameTickVtableIndex];
                auto* target_bytes = reinterpret_cast<uint8_t*>(target);
                const bool in_text = sections.text && target_bytes >= sections.text && target_bytes < sections.text + sections.text_size;
                if (!target || !in_text)
                {
                    if (!m_pending_net_game_rejection_logged)
                    {
                        m_pending_net_game_rejection_logged = true;
                        Output::send<LogLevel::Warning>(
                                STR("[UE4SSL.PakSync] PendingNetGame::Tick candidate rejected object={} target=0x{:016X} in_text={}\n"),
                                object->GetFullName(),
                                reinterpret_cast<uintptr_t>(target),
                                in_text);
                    }
                    continue;
                }

                const auto rva = static_cast<uintptr_t>(target_bytes - sections.base);
                if (!m_pending_net_game_candidate_logged)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] PendingNetGame candidate from FindAllInstancesOfClass source={} object={} pending=0x{:016X} vtable_index={} target=0x{:016X} rva=0x{:X}; attempting pre-LoadMap Tick vtable detour\n"),
                            source ? source : L"<null>",
                            object->GetFullName(),
                            reinterpret_cast<uintptr_t>(object),
                            PendingNetGameTickVtableIndex,
                            reinterpret_cast<uintptr_t>(target),
                            rva);
                    m_pending_net_game_candidate_logged = true;
                }

                if (try_install_pending_net_game_tick_detour(object, source))
                {
                    return true;
                }
            }
            return false;
        }

        void log_pending_net_game_tick_candidate_if_needed()
        {
            (void)probe_pending_net_game_tick_candidate(L"on_update");
        }

        void scan_local_paks()
        {
            const auto pak_dir = m_mod_dir / L"paks";
            std::error_code ec{};
            if (!fs::is_directory(pak_dir, ec))
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] pak directory not present: {}\n"),
                        pak_dir.wstring());
                return;
            }

            for (const auto& entry : fs::directory_iterator{pak_dir, ec})
            {
                if (ec)
                {
                    break;
                }
                if (!entry.is_regular_file(ec) || entry.path().extension() != L".pak")
                {
                    continue;
                }

                auto manifest = build_manifest(entry.path(), m_config.chunk_size);
                if (!manifest)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] failed to build manifest for {}\n"),
                            entry.path().wstring());
                    continue;
                }

                const auto hash = bytes_to_hex(manifest->sha256);
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] pak manifest {} size={} chunks={} sha256={}\n"),
                        manifest->name,
                        manifest->size,
                        manifest->chunk_count,
                        hash);
                m_manifests.emplace_back(std::move(*manifest));
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] local pak scan complete manifests={}; announcement will be queued after a NetConnection is discovered\n"),
                    m_manifests.size());
        }

        void queue_local_manifest_announcement()
        {
            if (m_manifests.empty())
            {
                queue_manifest_completion(0);
                return;
            }

            for (const auto& manifest : m_manifests)
            {
                prepare_transfer_frames(manifest);
            }
            queue_manifest_completion(m_manifests.size());
        }

        void queue_manifest_completion(size_t manifest_count)
        {
            std::array<uint8_t, 32> zero_hash{};
            if (manifest_count == 0)
            {
                queue_control_frame(FrameKind::NoWork, 0, zero_hash, 0, 0, L"manifest:no-work");
                Output::send<LogLevel::Normal>(STR("[UE4SSL.PakSync] prepared NoWork manifest completion frame\n"));
                return;
            }

            queue_control_frame(
                    FrameKind::ManifestEnd,
                    0,
                    zero_hash,
                    0,
                    static_cast<uint32_t>(std::min<size_t>(manifest_count, UINT32_MAX)),
                    L"manifest:end");
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] prepared ManifestEnd frame manifest_count={}\n"),
                    manifest_count);
        }

        void prepare_transfer_frames(const PakManifest& manifest)
        {
            const uint64_t session_id = session_id_for_hash(manifest.sha256);
            auto manifest_bytes = manifest_payload(manifest);
            auto frame = encode_frame(FrameKind::Manifest, session_id, manifest.sha256, 0, manifest.chunk_count, manifest_bytes);
            if (!decode_frame(frame))
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] manifest frame self-check failed for {}\n"),
                        manifest.name);
                return;
            }
            m_queued_frames.push_back(QueuedFrame{FrameKind::Manifest, session_id, 0, std::move(frame), manifest.name + L":manifest"});

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] prepared manifest for {} session={} chunks={} waiting for Resume before sending chunks\n"),
                    manifest.name,
                    session_id,
                    manifest.chunk_count);
        }

        void queue_chunk_frames_for_manifest(const PakManifest& manifest, uint32_t first_seq)
        {
            const uint64_t session_id = session_id_for_hash(manifest.sha256);
            if (first_seq >= manifest.chunk_count)
            {
                queue_control_frame(FrameKind::Done, session_id, manifest.sha256, manifest.chunk_count, manifest.chunk_count, manifest.name + L":done");
                return;
            }

            size_t queued = 0;
            const auto end_seq = std::min<uint32_t>(
                    manifest.chunk_count,
                    first_seq + std::max<uint32_t>(m_config.send_window, 1));
            for (uint32_t seq = first_seq; seq < end_seq; ++seq)
            {
                auto payload = chunk_payload(manifest, seq);
                if (!payload)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] failed to read chunk {} for {}\n"),
                            seq,
                            manifest.name);
                    continue;
                }

                auto chunk_frame = encode_frame(FrameKind::Chunk, session_id, manifest.sha256, seq, manifest.chunk_count, *payload);
                if (!decode_frame(chunk_frame))
                {
                    Output::send<LogLevel::Error>(
                            STR("[UE4SSL.PakSync] chunk frame self-check failed seq={} for {}\n"),
                            seq,
                            manifest.name);
                    continue;
                }

                const uint64_t chunk_offset = static_cast<uint64_t>(seq) * manifest.chunk_size;
                if (chunk_frame.size() > LargeControlFrameWarningBytes)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] chunk frame may be too large for ControlChannel name={} session={} seq={} payload={} frame_bytes={} offset={}\n"),
                            manifest.name,
                            session_id,
                            seq,
                            payload->size(),
                            chunk_frame.size(),
                            chunk_offset);
                }
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] queued chunk name={} session={} seq={}/{} payload={} frame_bytes={} offset={}\n"),
                        manifest.name,
                        session_id,
                        seq,
                        manifest.chunk_count,
                        payload->size(),
                        chunk_frame.size(),
                        chunk_offset);
                m_queued_frames.push_back(QueuedFrame{FrameKind::Chunk, session_id, seq, std::move(chunk_frame), manifest.name + L":chunk"});
                ++queued;
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] queued {} chunk frame(s) for {} from seq={} session={} send_window={}\n"),
                    queued,
                    manifest.name,
                    first_seq,
                    session_id,
                    m_config.send_window);
        }

        void queue_control_frame(FrameKind kind,
                                 uint64_t session_id,
                                 const std::array<uint8_t, 32>& pak_hash,
                                 uint32_t seq,
                                 uint32_t total,
                                 std::wstring label)
        {
            auto frame = encode_frame(kind, session_id, pak_hash, seq, total, {});
            m_queued_frames.push_back(QueuedFrame{kind, session_id, seq, std::move(frame), std::move(label)});
        }

        bool handle_frame_bytes(std::span<const uint8_t> bytes)
        {
            auto decoded = decode_frame(bytes);
            if (!decoded)
            {
                return false;
            }

            const auto& [header, payload] = *decoded;
            switch (static_cast<FrameKind>(header.kind))
            {
            case FrameKind::Manifest:
                return handle_manifest_frame(header, payload);
            case FrameKind::Chunk:
                return handle_chunk_frame(header, payload);
            case FrameKind::Ack:
                Output::send<LogLevel::Verbose>(
                        STR("[UE4SSL.PakSync] Ack received session={} seq={} total={}\n"),
                        header.session_id,
                        header.seq,
                        header.total);
                return true;
            case FrameKind::Nak:
                return handle_nak_frame(header);
            case FrameKind::Resume:
                return handle_resume_frame(header);
            case FrameKind::Done:
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] Done received session={} seq={} total={}\n"),
                        header.session_id,
                        header.seq,
                        header.total);
                return true;
            case FrameKind::ManifestEnd:
                return handle_manifest_complete_frame(header, false);
            case FrameKind::NoWork:
                return handle_manifest_complete_frame(header, true);
            default:
                return false;
            }
        }

        PakManifest* find_manifest_by_session(uint64_t session_id)
        {
            auto it = std::find_if(m_manifests.begin(), m_manifests.end(), [session_id](const PakManifest& manifest) {
                return session_id_for_hash(manifest.sha256) == session_id;
            });
            return it == m_manifests.end() ? nullptr : &*it;
        }

        bool handle_resume_frame(const FrameHeader& header)
        {
            auto* manifest = find_manifest_by_session(header.session_id);
            if (!manifest)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] Resume for unknown session={} seq={}\n"),
                        header.session_id,
                        header.seq);
                return true;
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] Resume received for {} session={} first_seq={}\n"),
                    manifest->name,
                    header.session_id,
                    header.seq);
            queue_chunk_frames_for_manifest(*manifest, header.seq);
            return true;
        }

        bool handle_nak_frame(const FrameHeader& header)
        {
            auto* manifest = find_manifest_by_session(header.session_id);
            if (!manifest)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] Nak for unknown session={} seq={}\n"),
                        header.session_id,
                        header.seq);
                return true;
            }

            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] Nak received for {} session={} seq={} requeueing from seq\n"),
                    manifest->name,
                    header.session_id,
                    header.seq);
            queue_chunk_frames_for_manifest(*manifest, header.seq);
            return true;
        }

        bool handle_manifest_frame(const FrameHeader& header, std::span<const uint8_t> payload)
        {
            m_received_manifest_had_work = true;
            size_t offset = 0;
            auto name_len = read_le_value<uint16_t>(payload, offset);
            if (!name_len || payload.size() - offset < *name_len)
            {
                return false;
            }

            auto name = wide_from_utf8(payload.subspan(offset, *name_len));
            offset += *name_len;
            auto size = read_le_value<uint64_t>(payload, offset);
            auto chunk_size = read_le_value<uint32_t>(payload, offset);
            auto chunk_count = read_le_value<uint32_t>(payload, offset);
            if (!size || !chunk_size || !chunk_count || *chunk_size == 0 || *chunk_count == 0)
            {
                return false;
            }

            std::error_code ec{};
            const auto sanitized_name = sanitize_filename(std::move(name));
            const auto final_path = incoming_pak_path_for_hash(header.pak_hash, sanitized_name);
            fs::create_directories(final_path.parent_path(), ec);
            if (m_config.stage_synced_paks_for_restart && !m_staged_manifest_seen_this_join)
            {
                m_staged_manifest_seen_this_join = true;
                m_restart_required_after_stage = false;
                m_restart_prompt_shown = false;
                m_staged_active_paks.clear();
                m_staging_manifest_hashes.clear();
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] started host manifest staging set for this join\n"));
            }
            if (m_config.stage_synced_paks_for_restart)
            {
                record_staging_manifest_hash(header.pak_hash);
            }
            if (fs::is_regular_file(final_path, ec))
            {
                auto existing_hash = sha256_file(final_path);
                if (existing_hash && *existing_hash == header.pak_hash)
                {
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] manifest already satisfied by {} session={} sending Done\n"),
                            final_path.wstring(),
                            header.session_id);
                    if (m_config.stage_synced_paks_for_restart)
                    {
                        stage_verified_pak_for_restart(final_path, header.pak_hash, sanitized_name);
                        update_sync_window_for_existing_pak(
                                sanitized_name,
                                *size,
                                *chunk_count,
                                header.pak_hash,
                                L"此房间需要的 pak 已在本地缓存。请重启游戏后重新加入同一个房间。",
                                m_restart_required_after_stage);
                        maybe_prompt_restart_after_stage();
                    }
                    else
                    {
                        enqueue_verified_pak_for_mount(final_path);
                    }
                    queue_control_frame(FrameKind::Done, header.session_id, header.pak_hash, *chunk_count, *chunk_count, sanitized_name + L":done");
                    return true;
                }
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] manifest accepted; waiting for user approval before requesting pak chunks session={} chunks={} chunk_size={} size={} name={} sha256={} final={}\n"),
                    header.session_id,
                    *chunk_count,
                    *chunk_size,
                    *size,
                    sanitized_name,
                    bytes_to_hex(header.pak_hash),
                    final_path.wstring());
            m_missing_host_paks.push_back(sanitized_name);
            m_pending_download_approvals.push_back(PendingDownloadApproval{
                    header.session_id,
                    header.pak_hash,
                    sanitized_name,
                    *size,
                    *chunk_size,
                    *chunk_count,
                    final_path});
            show_download_approval();
#if 0
            update_sync_window_for_session(it->second, L"已收到房主的 pak 清单，正在请求传输分片。", false, false);
#endif
            return true;
        }

        bool handle_manifest_complete_frame(const FrameHeader& header, bool no_work)
        {
            m_host_manifest_complete = true;
            m_host_manifest_wait_start_ms = 0;
            m_host_manifest_timeout_logged = false;
            if (no_work)
            {
                m_received_manifest_had_work = false;
                if (m_config.stage_synced_paks_for_restart)
                {
                    const auto previous_launch_room = m_launch_room_id;
                    m_staged_room_id.clear();
                    m_staging_manifest_hashes.clear();
                    m_staged_active_paks.clear();
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] host reported NoWork; no room pak set will be staged previous_launch_room={}\n"),
                            previous_launch_room.empty() ? L"<none>" : previous_launch_room);
                }
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] host manifest complete kind={} sessions={} had_work={}\n"),
                    no_work ? STR("NoWork") : STR("ManifestEnd"),
                    header.total,
                    m_received_manifest_had_work);
            maybe_prompt_restart_after_stage();
            return true;
        }

        uint32_t first_missing_chunk_seq(const IncomingSession& session) const
        {
            for (uint32_t seq = 0; seq < session.received.size(); ++seq)
            {
                if (session.received[seq] == 0)
                {
                    return seq;
                }
            }
            return session.chunk_count;
        }

        fs::path incoming_session_map_path(const IncomingSession& session) const
        {
            return fs::path{session.temp_path.wstring() + L".map"};
        }

        bool load_incoming_session_map(IncomingSession& session)
        {
            std::ifstream file{incoming_session_map_path(session), std::ios::binary};
            if (!file)
            {
                return false;
            }

            std::array<char, 8> magic{};
            uint64_t session_id{};
            uint64_t size{};
            uint32_t chunk_size{};
            uint32_t chunk_count{};
            std::array<uint8_t, 32> pak_hash{};
            file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            file.read(reinterpret_cast<char*>(&session_id), sizeof(session_id));
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            file.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
            file.read(reinterpret_cast<char*>(&chunk_count), sizeof(chunk_count));
            file.read(reinterpret_cast<char*>(pak_hash.data()), static_cast<std::streamsize>(pak_hash.size()));
            if (!file ||
                std::string_view{magic.data(), magic.size()} != std::string_view{"PSMAP1\0\0", 8} ||
                session_id != session.session_id ||
                size != session.size ||
                chunk_size != session.chunk_size ||
                chunk_count != session.chunk_count ||
                pak_hash != session.pak_hash)
            {
                return false;
            }

            std::vector<uint8_t> received(session.chunk_count, 0);
            file.read(reinterpret_cast<char*>(received.data()), static_cast<std::streamsize>(received.size()));
            if (!file)
            {
                return false;
            }
            session.received = std::move(received);
            return true;
        }

        void save_incoming_session_map(const IncomingSession& session)
        {
            std::ofstream file{incoming_session_map_path(session), std::ios::binary | std::ios::trunc};
            if (!file)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] failed to write temp progress map session={} path={}\n"),
                        session.session_id,
                        incoming_session_map_path(session).wstring());
                return;
            }

            const std::array<char, 8> magic{'P', 'S', 'M', 'A', 'P', '1', '\0', '\0'};
            file.write(magic.data(), static_cast<std::streamsize>(magic.size()));
            file.write(reinterpret_cast<const char*>(&session.session_id), sizeof(session.session_id));
            file.write(reinterpret_cast<const char*>(&session.size), sizeof(session.size));
            file.write(reinterpret_cast<const char*>(&session.chunk_size), sizeof(session.chunk_size));
            file.write(reinterpret_cast<const char*>(&session.chunk_count), sizeof(session.chunk_count));
            file.write(reinterpret_cast<const char*>(session.pak_hash.data()), static_cast<std::streamsize>(session.pak_hash.size()));
            file.write(reinterpret_cast<const char*>(session.received.data()), static_cast<std::streamsize>(session.received.size()));
        }

        void queue_resume_for_session(IncomingSession& session, uint32_t first_seq, const wchar_t* reason, bool force_retry = false)
        {
            if (!force_retry)
            {
                first_seq = first_missing_chunk_seq(session);
            }
            if (first_seq >= session.chunk_count)
            {
                return;
            }
            const auto already_queued = std::any_of(m_queued_frames.begin(), m_queued_frames.end(), [&](const QueuedFrame& frame) {
                return frame.kind == FrameKind::Resume &&
                       frame.session_id == session.session_id &&
                       frame.seq == first_seq;
            });
            if (already_queued)
            {
                session.next_resume_retry_ms = GetTickCount64() + ResumeRetryIntervalMs;
                Output::send<LogLevel::Verbose>(
                        STR("[UE4SSL.PakSync] Resume already queued reason={} session={} first_seq={}\n"),
                        reason,
                        session.session_id,
                        first_seq);
                return;
            }

            const auto label = session.name + L":resume:" + reason;
            queue_control_frame(FrameKind::Resume, session.session_id, session.pak_hash, first_seq, session.chunk_count, label);
            session.requested_until_seq = std::max<uint32_t>(
                    session.requested_until_seq,
                    std::min<uint32_t>(
                            session.chunk_count,
                            first_seq + std::max<uint32_t>(m_config.send_window, 1)));
            session.next_resume_retry_ms = GetTickCount64() + ResumeRetryIntervalMs;
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] queued Resume reason={} session={} first_seq={} requested_until={} retry={}/{}\n"),
                    reason,
                    session.session_id,
                    first_seq,
                    session.requested_until_seq,
                    session.resume_retries,
                    MaxResumeRetriesPerSession);
        }

        void retry_stalled_incoming_sessions()
        {
            const auto now = GetTickCount64();
            for (auto& [session_id, session] : m_incoming_sessions)
            {
                if (session.next_resume_retry_ms == 0 || now < session.next_resume_retry_ms)
                {
                    continue;
                }
                if (session.resume_retries >= MaxResumeRetriesPerSession)
                {
                    const auto first_missing = first_missing_chunk_seq(session);
                    if (first_missing >= session.chunk_count)
                    {
                        continue;
                    }
                    if (!session.retry_limit_logged || now >= session.next_retry_limit_log_ms)
                    {
                        session.retry_limit_logged = true;
                        session.next_retry_limit_log_ms = now + ResumeRetryAfterLimitMs;
                        Output::send<LogLevel::Warning>(
                                STR("[UE4SSL.PakSync] incoming session still waiting after retry budget session={} name={} first_missing={} received={}/{} temp={}; continuing with slow resume retries\n"),
                                session_id,
                                session.name,
                                first_missing,
                                std::count(session.received.begin(), session.received.end(), uint8_t{1}),
                                session.chunk_count,
                                session.temp_path.wstring());
                    }
                    queue_resume_for_session(session, first_missing, L"retry-slow", true);
                    session.next_resume_retry_ms = now + ResumeRetryAfterLimitMs;
                    continue;
                }

                const auto first_missing = first_missing_chunk_seq(session);
                if (first_missing >= session.chunk_count)
                {
                    continue;
                }

                ++session.resume_retries;
                queue_resume_for_session(session, first_missing, L"retry", true);
            }
        }

        auto incoming_dir() const -> fs::path
        {
            return m_mod_dir / L"incoming";
        }

        auto staged_rooms_dir() const -> fs::path
        {
            return m_mod_dir / L"rooms";
        }

        auto room_pak_dir(const std::wstring& room_id) const -> fs::path
        {
            return staged_rooms_dir() / room_id;
        }

        auto room_manifest_path(const std::wstring& room_id) const -> fs::path
        {
            return room_pak_dir(room_id) / L"manifest.txt";
        }

        auto launch_room_pak_dir() const -> fs::path
        {
            return m_launch_room_id.empty() ? fs::path{} : room_pak_dir(m_launch_room_id);
        }

        auto launch_room_manifest_path() const -> fs::path
        {
            return m_launch_room_id.empty() ? fs::path{} : room_manifest_path(m_launch_room_id);
        }

        auto hash_label(const std::array<uint8_t, 32>& hash) const -> std::wstring
        {
            return bytes_to_hex(hash);
        }

        auto parse_launch_room_id() const -> std::wstring
        {
            const wchar_t* command_line = GetCommandLineW();
            if (!command_line)
            {
                return {};
            }

            const std::wstring_view command{command_line};
            constexpr std::wstring_view key = L"-PakSyncRoom=";
            const auto pos = command.find(key);
            if (pos == std::wstring_view::npos)
            {
                return {};
            }

            auto start = pos + key.size();
            if (start < command.size() && command[start] == L'"')
            {
                ++start;
                const auto end = command.find(L'"', start);
                return sanitize_room_id(std::wstring{command.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start)});
            }

            const auto end = command.find_first_of(L" \t\r\n", start);
            return sanitize_room_id(std::wstring{command.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start)});
        }

        auto sanitize_room_id(std::wstring value) const -> std::wstring
        {
            value.erase(
                    std::remove_if(
                            value.begin(),
                            value.end(),
                            [](wchar_t ch) {
                                return !((ch >= L'0' && ch <= L'9') ||
                                         (ch >= L'a' && ch <= L'f') ||
                                         (ch >= L'A' && ch <= L'F') ||
                                         ch == L'_' ||
                                         ch == L'-');
                            }),
                    value.end());
            return value;
        }

        auto current_staging_room_id() const -> std::wstring
        {
            std::vector<uint8_t> bytes{};
            bytes.reserve(m_staging_manifest_hashes.size() * 32);
            auto hashes = m_staging_manifest_hashes;
            std::sort(hashes.begin(), hashes.end());
            for (const auto& hash : hashes)
            {
                bytes.insert(bytes.end(), hash.begin(), hash.end());
            }
            const auto crc = crc32(bytes);
            wchar_t buffer[16]{};
            swprintf_s(buffer, L"%08x", crc);
            return buffer;
        }

        auto current_staging_room_dir() const -> fs::path
        {
            const auto room_id = current_staging_room_id();
            return room_id.empty() ? fs::path{} : room_pak_dir(room_id);
        }

        void record_staging_manifest_hash(const std::array<uint8_t, 32>& pak_hash)
        {
            const auto exists = std::any_of(m_staging_manifest_hashes.begin(), m_staging_manifest_hashes.end(), [&](const auto& existing) {
                return existing == pak_hash;
            });
            if (!exists)
            {
                m_staging_manifest_hashes.push_back(pak_hash);
            }
        }

        auto incoming_pak_filename_for_hash(const std::array<uint8_t, 32>& hash, const std::wstring& name) const -> std::wstring
        {
            auto label = hash_label(hash);
            auto extension = fs::path{name}.extension().wstring();
            if (extension.empty())
            {
                extension = L".pak";
            }
            return label + L"_" + sanitize_filename(name);
        }

        auto incoming_pak_path_for_hash(const std::array<uint8_t, 32>& hash, const std::wstring& name) const -> fs::path
        {
            return incoming_dir() / incoming_pak_filename_for_hash(hash, name);
        }

        void write_room_manifest()
        {
            const auto room_dir = current_staging_room_dir();
            if (room_dir.empty())
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] failed to write room manifest: room id unavailable\n"));
                return;
            }
            std::error_code ec{};
            fs::create_directories(room_dir, ec);

            const auto manifest_path = room_manifest_path(current_staging_room_id());
            std::wofstream file{manifest_path, std::ios::trunc};
            if (!file)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] failed to write room pak manifest {}\n"),
                        manifest_path.wstring());
                return;
            }

            file << L"# UE4SSL.PakSync room pak set\n";
            file << L"# Launch with -PakSyncRoom=" << current_staging_room_id() << L" to mount this room.\n";
            for (const auto& pak_path : m_staged_active_paks)
            {
                file << pak_path.filename().wstring() << L"\n";
            }
        }

        auto room_manifest_paks(const std::wstring& room_id) const -> std::vector<fs::path>
        {
            std::vector<fs::path> paks{};
            const auto manifest_path = room_manifest_path(room_id);
            std::wifstream file{manifest_path};
            if (!file)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] room manifest unavailable room={} path={}\n"),
                        room_id,
                        manifest_path.wstring());
                return paks;
            }

            std::wstring line{};
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == L'#')
                {
                    continue;
                }
                const auto filename = sanitize_filename(line);
                const auto path = incoming_dir() / filename;
                std::error_code ec{};
                if (!fs::is_regular_file(path, ec))
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] room manifest pak missing room={} file={} path={}\n"),
                            room_id,
                            filename,
                            path.wstring());
                    continue;
                }
                paks.push_back(path);
            }
            return paks;
        }

        bool clear_pak_dir(const fs::path& dir, const wchar_t* label)
        {
            std::error_code ec{};
            fs::create_directories(dir, ec);
            bool removed_any = false;
            for (const auto& entry : fs::directory_iterator{dir, ec})
            {
                if (ec)
                {
                    break;
                }
                if (!entry.is_regular_file(ec))
                {
                    continue;
                }
                const auto filename = entry.path().filename().wstring();
                if (entry.path().extension() == L".pak" ||
                    filename == L"manifest.txt" ||
                    filename == L"restart_required.txt")
                {
                    ec.clear();
                    fs::remove(entry.path(), ec);
                    if (ec)
                    {
                        Output::send<LogLevel::Warning>(
                                STR("[UE4SSL.PakSync] failed to remove {} file {} ec={} {}\n"),
                                label,
                                entry.path().wstring(),
                                ec.value(),
                                widen(ec.message()));
                    }
                    else
                    {
                        removed_any = true;
                    }
                }
            }
            return removed_any;
        }

        bool file_hash_matches(const fs::path& path, const std::array<uint8_t, 32>& expected_hash) const
        {
            std::error_code ec{};
            if (!fs::is_regular_file(path, ec))
            {
                return false;
            }
            const auto hash = sha256_file(path);
            return hash && *hash == expected_hash;
        }

        bool stage_verified_pak_for_restart(
                const fs::path& pak_path,
                const std::array<uint8_t, 32>& pak_hash,
                const std::wstring& pak_name)
        {
            const auto incoming_path = incoming_pak_path_for_hash(pak_hash, pak_name);
            bool changed = pak_path != incoming_path;
            if (pak_path != incoming_path)
            {
                std::error_code ec{};
                fs::create_directories(incoming_path.parent_path(), ec);
                ec.clear();
                fs::copy_file(pak_path, incoming_path, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    Output::send<LogLevel::Error>(
                            STR("[UE4SSL.PakSync] failed to migrate pak into incoming cache {} -> {} ec={} {}\n"),
                            pak_path.wstring(),
                            incoming_path.wstring(),
                            ec.value(),
                            widen(ec.message()));
                    return false;
                }
            }
            if (!file_hash_matches(incoming_path, pak_hash))
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] cannot stage room pak: incoming cache missing or hash mismatch path={} expected={}\n"),
                        incoming_path.wstring(),
                        bytes_to_hex(pak_hash));
                return false;
            }

            const auto room_id = current_staging_room_id();
            if (room_id.empty())
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] failed to stage pak for restart: room id unavailable name={}\n"),
                        pak_name);
                return false;
            }
            m_staged_room_id = room_id;
            const auto already_active = std::any_of(m_staged_active_paks.begin(), m_staged_active_paks.end(), [&](const fs::path& active) {
                return active == incoming_path;
            });
            if (!already_active)
            {
                m_staged_active_paks.push_back(incoming_path);
            }
            write_room_manifest();
            const bool restart_needed_for_room = m_launch_room_id != room_id;
            if (changed || restart_needed_for_room)
            {
                m_restart_required_after_stage = true;
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] staged pak for restart source={} incoming={} room={} changed={} launch_room={} restart_required={}\n"),
                    pak_path.wstring(),
                    incoming_path.wstring(),
                    room_id,
                    changed,
                    m_launch_room_id.empty() ? L"<none>" : m_launch_room_id,
                    m_restart_required_after_stage);
            return true;
        }

        void mark_restart_required()
        {
            const auto room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            if (room_id.empty())
            {
                return;
            }
            std::error_code ec{};
            const auto room_dir = room_pak_dir(room_id);
            fs::create_directories(room_dir, ec);
            std::wofstream file{room_dir / L"restart_required.txt", std::ios::trunc};
            if (file)
            {
                file << L"PakSync synchronized pak files for the current host.\n";
                file << L"Restart Deep Rock Galactic with -PakSyncRoom=" << room_id << L" before joining again so staged pak files can load from a clean process.\n";
            }
        }

        void show_download_approval()
        {
            if (!m_config.prompt_restart_after_stage || m_pending_download_approvals.empty())
            {
                return;
            }

            uint64_t total_size = 0;
            uint32_t total_chunks = 0;
            std::wstring pak_names{};
            std::wstring first_hash{};
            for (const auto& pending : m_pending_download_approvals)
            {
                total_size += pending.size;
                total_chunks += pending.chunk_count;
                if (!pak_names.empty())
                {
                    pak_names += L"; ";
                }
                pak_names += pending.name;
                if (first_hash.empty())
                {
                    first_hash = bytes_to_hex(pending.pak_hash);
                }
            }

            SyncWindowState state{};
            state.title = L"PakSync \u9700\u8981\u4f60\u6279\u51c6\u4e0b\u8f7d";
            state.status = L"\u623f\u4e3b\u8981\u6c42\u540c\u6b65 " + std::to_wstring(m_pending_download_approvals.size()) +
                           L" \u4e2a pak\u3002\u6279\u51c6\u540e\u624d\u4f1a\u5f00\u59cb\u4ece\u623f\u4e3b\u4f20\u8f93\u3002";
            state.pak_name = pak_names;
            state.room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            state.hash = m_pending_download_approvals.size() == 1 ? first_hash : L"<multiple>";
            state.total_bytes = total_size;
            state.received_bytes = 0;
            state.total_chunks = total_chunks;
            state.received_chunks = 0;
            state.complete = false;
            state.restart_available = false;
            state.approval_available = true;
            m_sync_window.show_or_update(state);
        }

        void handle_download_approval(bool approved)
        {
            if (m_pending_download_approvals.empty())
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] download approval ignored: no pending manifest\n"));
                return;
            }

            auto pending = std::move(m_pending_download_approvals);
            m_pending_download_approvals.clear();

            if (!approved)
            {
                for (const auto& item : pending)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] user declined pak download session={} name={} sha256={}\n"),
                            item.session_id,
                            item.name,
                            bytes_to_hex(item.pak_hash));
                }
                return;
            }

            for (const auto& item : pending)
            {
                start_approved_download(item);
            }
        }

        bool start_approved_download(const PendingDownloadApproval& pending)
        {
            IncomingSession session{};
            session.session_id = pending.session_id;
            session.pak_hash = pending.pak_hash;
            session.name = pending.name;
            session.size = pending.size;
            session.chunk_size = pending.chunk_size;
            session.chunk_count = pending.chunk_count;
            session.temp_path = pending.final_path.wstring() + L".tmp";
            session.final_path = pending.final_path;
            session.received.assign(pending.chunk_count, 0);
            session.last_progress_ms = GetTickCount64();

            std::error_code ec{};
            uint32_t resume_from_seq = 0;
            bool create_temp = true;
            if (fs::exists(session.temp_path, ec) && !ec && load_incoming_session_map(session))
            {
                const auto received_chunks = static_cast<uint32_t>(
                        std::count(session.received.begin(), session.received.end(), uint8_t{1}));
                resume_from_seq = first_missing_chunk_seq(session);
                create_temp = false;
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] resuming existing temp pak session={} name={} received_chunks={}/{} first_missing={} temp={}\n"),
                        pending.session_id,
                        pending.name,
                        received_chunks,
                        pending.chunk_count,
                        resume_from_seq,
                        session.temp_path.wstring());
            }
            if (create_temp)
            {
                fs::remove(session.temp_path, ec);
                fs::remove(incoming_session_map_path(session), ec);
                ec.clear();
            }
            {
                std::ofstream temp{
                        session.temp_path,
                        std::ios::binary | (create_temp ? std::ios::trunc : std::ios::app)};
                if (!temp)
                {
                    Output::send<LogLevel::Error>(
                            STR("[UE4SSL.PakSync] failed to create temp pak after approval path={}\n"),
                            session.temp_path.wstring());
                    return false;
                }
                if (create_temp && pending.size > 0)
                {
                    temp.seekp(static_cast<std::streamoff>(pending.size - 1), std::ios::beg);
                    const char zero = 0;
                    temp.write(&zero, 1);
                }
                if (!temp)
                {
                    Output::send<LogLevel::Error>(
                            STR("[UE4SSL.PakSync] failed to size temp pak after approval path={} size={}\n"),
                            session.temp_path.wstring(),
                            pending.size);
                    return false;
                }
            }
            if (create_temp)
            {
                save_incoming_session_map(session);
            }

            auto [it, inserted] = m_incoming_sessions.insert_or_assign(pending.session_id, std::move(session));
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] user approved pak download; requesting chunks session={} chunks={} chunk_size={} size={} name={} sha256={} temp={}\n"),
                    pending.session_id,
                    pending.chunk_count,
                    pending.chunk_size,
                    pending.size,
                    pending.name,
                    bytes_to_hex(pending.pak_hash),
                    it->second.temp_path.wstring());
            queue_resume_for_session(it->second, resume_from_seq, resume_from_seq == 0 ? L"approved" : L"approved-resume");
            const auto connection_count = discover_connections();
            update_sync_window_for_session(
                    it->second,
                    connection_count > 0
                            ? L"\u5df2\u6279\u51c6\u4e0b\u8f7d\uff0c\u6b63\u5728\u4ece\u623f\u4e3b\u8bf7\u6c42 pak \u5206\u7247\u3002"
                            : L"\u5df2\u6279\u51c6\u4e0b\u8f7d\uff0c\u4f46\u5f53\u524d\u8fde\u63a5\u5df2\u65ad\u5f00\u3002\u8bf7\u91cd\u65b0\u52a0\u5165\u540c\u4e00\u4e2a\u623f\u95f4\u4ee5\u7ee7\u7eed\u4f20\u8f93\u3002",
                    false,
                    false);
            update_sync_phase(connection_count);
            return true;
        }

        void update_sync_window_for_session(const IncomingSession& session, const std::wstring& status, bool complete, bool restart_available)
        {
            if (!m_config.prompt_restart_after_stage)
            {
                return;
            }

            const auto received_chunks = static_cast<uint32_t>(std::count(session.received.begin(), session.received.end(), uint8_t{1}));
            const uint64_t received_bytes = session.chunk_count == 0
                                                    ? 0
                                                    : std::min<uint64_t>(
                                                              session.size,
                                                              static_cast<uint64_t>(received_chunks) * static_cast<uint64_t>(session.chunk_size));
            SyncWindowState state{};
            state.title = complete ? L"PakSync 已完成房间 Mod 同步" : L"PakSync 正在同步房间 Mod";
            state.status = status;
            state.pak_name = session.name;
            state.room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            state.hash = bytes_to_hex(session.pak_hash);
            state.total_bytes = session.size;
            state.received_bytes = complete ? session.size : received_bytes;
            state.total_chunks = session.chunk_count;
            state.received_chunks = complete ? session.chunk_count : received_chunks;
            state.complete = complete;
            state.restart_available = restart_available;
            m_sync_window.show_or_update(state);
        }

        void update_sync_window_for_existing_pak(
                const std::wstring& pak_name,
                uint64_t pak_size,
                uint32_t chunk_count,
                const std::array<uint8_t, 32>& pak_hash,
                const std::wstring& status,
                bool restart_available)
        {
            if (!m_config.prompt_restart_after_stage || !m_pending_download_approvals.empty())
            {
                return;
            }

            SyncWindowState state{};
            state.title = L"PakSync 房间 Mod 已在本地缓存";
            state.status = status;
            state.pak_name = pak_name;
            state.room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            state.hash = bytes_to_hex(pak_hash);
            state.total_bytes = pak_size;
            state.received_bytes = pak_size;
            state.total_chunks = chunk_count;
            state.received_chunks = chunk_count;
            state.complete = true;
            state.restart_available = restart_available;
            m_sync_window.show_or_update(state);
        }

        bool restart_with_room(const std::wstring& room_id)
        {
            if (room_id.empty())
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.PakSync] restart skipped: room id unavailable\n"));
                return false;
            }

            wchar_t exe_path[MAX_PATH]{};
            const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            if (len == 0 || len >= MAX_PATH)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.PakSync] restart failed: GetModuleFileNameW failed\n"));
                return false;
            }

            const auto current_dir = fs::path{exe_path}.parent_path().wstring();
            const auto args = L"-PakSyncRoom=" + room_id;
            auto* launched = ShellExecuteW(nullptr, L"open", exe_path, args.c_str(), current_dir.c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<intptr_t>(launched) <= 32)
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] restart failed: ShellExecuteW result={}\n"),
                        reinterpret_cast<intptr_t>(launched));
                return false;
            }

            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] restarting process after PakSync staging room={} args={}\n"),
                    room_id,
                    args);
            ExitProcess(0);
            return true;
        }

        void maybe_prompt_restart_after_stage()
        {
            if (!m_config.stage_synced_paks_for_restart ||
                !m_config.prompt_restart_after_stage ||
                !m_restart_required_after_stage ||
                !m_host_manifest_complete ||
                !m_pending_download_approvals.empty() ||
                !m_incoming_sessions.empty() ||
                m_restart_prompt_shown)
            {
                return;
            }

            m_restart_prompt_shown = true;
            mark_restart_required();
            const auto room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] restart required after PakSync staging room={} paks={}\n"),
                    room_id,
                    m_staged_active_paks.size());
            if (room_id.empty())
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.PakSync] restart prompt skipped: room id unavailable\n"));
                return;
            }

            SyncWindowState state{};
            state.title = L"PakSync 同步完成，需要重启";
            state.status = L"房间 Mod 已同步完成。请重启游戏后重新加入同一个房间。";
            state.room_id = room_id;
            state.total_bytes = 1;
            state.received_bytes = 1;
            state.total_chunks = static_cast<uint32_t>(m_staged_active_paks.size());
            state.received_chunks = static_cast<uint32_t>(m_staged_active_paks.size());
            state.complete = true;
            state.restart_available = true;
            if (!m_staged_active_paks.empty())
            {
                state.pak_name = m_staged_active_paks.back().filename().wstring();
            }
            m_sync_window.show_or_update(state);
        }

        void maybe_prompt_missing_pak(const std::wstring& pak_name, uint64_t pak_size, const std::array<uint8_t, 32>& pak_hash)
        {
            if (m_missing_pak_prompt_shown)
            {
                return;
            }

            m_missing_pak_prompt_shown = true;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] missing host pak; requesting transfer from host name={} size={} sha256={}\n"),
                    pak_name,
                    pak_size,
                    bytes_to_hex(pak_hash));

            if (!m_config.prompt_restart_after_stage)
            {
                return;
            }

            SyncWindowState state{};
            state.title = L"PakSync 正在同步房间 Mod";
            state.status = L"本地缺少此房间需要的 pak，正在从房主传输。完成后会提示重启。";
            state.pak_name = pak_name;
            state.room_id = m_staged_room_id.empty() ? current_staging_room_id() : m_staged_room_id;
            state.hash = bytes_to_hex(pak_hash);
            state.total_bytes = pak_size;
            state.received_bytes = 0;
            state.total_chunks = 0;
            state.received_chunks = 0;
            state.complete = false;
            state.restart_available = false;
            m_sync_window.show_or_update(state);
        }

        void enqueue_verified_pak_for_mount(const fs::path& pak_path)
        {
            const auto already_pending = std::any_of(m_pending_mounts.begin(), m_pending_mounts.end(), [&](const fs::path& pending) {
                return pending == pak_path;
            });
            const auto already_mounted = std::any_of(m_mounted_paks.begin(), m_mounted_paks.end(), [&](const fs::path& mounted) {
                return mounted == pak_path;
            });
            if (!already_pending && !already_mounted)
            {
                m_pending_mounts.push_back(pak_path);
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] queued verified pak for pre-travel mount: {}\n"),
                        pak_path.wstring());
            }
        }

        bool handle_chunk_frame(const FrameHeader& header, std::span<const uint8_t> payload)
        {
            auto it = m_incoming_sessions.find(header.session_id);
            if (it == m_incoming_sessions.end())
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] chunk before manifest session={} seq={}\n"),
                        header.session_id,
                        header.seq);
                return true;
            }

            auto& session = it->second;
            if (header.seq >= session.chunk_count)
            {
                return false;
            }

            const uint64_t offset = static_cast<uint64_t>(header.seq) * session.chunk_size;
            if (offset + payload.size() > session.size)
            {
                return false;
            }

            std::fstream file{session.temp_path, std::ios::binary | std::ios::in | std::ios::out};
            if (!file)
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] failed to open temp pak for chunk write {} session={} seq={}\n"),
                        session.temp_path.wstring(),
                        header.session_id,
                        header.seq);
                return false;
            }
            file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
            file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            if (!file)
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] failed to write chunk session={} seq={} bytes={} path={}\n"),
                        header.session_id,
                        header.seq,
                        payload.size(),
                        session.temp_path.wstring());
                return false;
            }
            file.flush();
            if (!file)
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] failed to flush chunk session={} seq={} path={}\n"),
                        header.session_id,
                        header.seq,
                        session.temp_path.wstring());
                return false;
            }
            file.close();

            session.received[header.seq] = 1;
            session.last_progress_ms = GetTickCount64();
            session.next_resume_retry_ms = session.last_progress_ms + ResumeRetryIntervalMs;
            session.resume_retries = 0;
            session.retry_limit_logged = false;
            save_incoming_session_map(session);
            const auto received_count = std::count(session.received.begin(), session.received.end(), uint8_t{1});
            const auto next_missing_after_write = first_missing_chunk_seq(session);
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] chunk received session={} name={} seq={}/{} bytes={} offset={} progress={}/{} next_missing={}\n"),
                    header.session_id,
                    session.name,
                    header.seq,
                    session.chunk_count,
                    payload.size(),
                    offset,
                    received_count,
                    session.chunk_count,
                    next_missing_after_write);
            update_sync_window_for_session(session, L"正在从房主接收 pak 数据。", false, false);
            if (finalize_session_if_complete(session))
            {
                const auto session_id = header.session_id;
                const auto final_path = session.final_path;
                const auto name = session.name;
                const auto chunk_count = session.chunk_count;
                const auto completed_session = session;
                m_incoming_sessions.erase(session_id);
                if (m_config.stage_synced_paks_for_restart)
                {
                    stage_verified_pak_for_restart(final_path, header.pak_hash, name);
                    update_sync_window_for_session(
                            completed_session,
                            L"pak 已接收并校验完成。请重启游戏后重新加入同一个房间。",
                            true,
                            m_restart_required_after_stage);
                }
                else
                {
                    enqueue_verified_pak_for_mount(final_path);
                    update_sync_window_for_session(
                            completed_session,
                            L"pak 已接收并校验完成，正在准备本次加入使用。",
                            true,
                            false);
                }
                queue_control_frame(FrameKind::Done, session_id, header.pak_hash, chunk_count, chunk_count, name + L":done");
                maybe_prompt_restart_after_stage();
                update_sync_phase(discover_connections());
            }
            else
            {
                const auto next_missing = first_missing_chunk_seq(session);
                if (next_missing < session.chunk_count)
                {
                    queue_resume_for_session(session, next_missing, L"next");
                }
            }
            return true;
        }

        bool finalize_session_if_complete(IncomingSession& session)
        {
            if (std::any_of(session.received.begin(), session.received.end(), [](uint8_t value) { return value == 0; }))
            {
                return false;
            }

            auto hash = sha256_file(session.temp_path);
            if (!hash || *hash != session.pak_hash)
            {
                Output::send<LogLevel::Error>(
                        STR("[UE4SSL.PakSync] hash mismatch for received pak {}\n"),
                        session.temp_path.wstring());
                return false;
            }

            std::error_code ec{};
            fs::rename(session.temp_path, session.final_path, ec);
            if (ec)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] rename temp pak failed {} -> {} ec={} {}; trying copy\n"),
                        session.temp_path.wstring(),
                        session.final_path.wstring(),
                        ec.value(),
                        widen(ec.message()));
                ec.clear();
                fs::copy_file(session.temp_path, session.final_path, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    Output::send<LogLevel::Error>(
                            STR("[UE4SSL.PakSync] copy temp pak failed {} -> {} ec={} {}\n"),
                            session.temp_path.wstring(),
                            session.final_path.wstring(),
                            ec.value(),
                            widen(ec.message()));
                    return false;
                }
                ec.clear();
                fs::remove(session.temp_path, ec);
                if (ec)
                {
                    Output::send<LogLevel::Warning>(
                            STR("[UE4SSL.PakSync] remove temp pak after copy failed {} ec={} {}\n"),
                            session.temp_path.wstring(),
                            ec.value(),
                            widen(ec.message()));
                }
            }

            ec.clear();
            fs::remove(incoming_session_map_path(session), ec);
            if (ec)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] remove temp progress map after verify failed {} ec={} {}\n"),
                        incoming_session_map_path(session).wstring(),
                        ec.value(),
                        widen(ec.message()));
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] received pak verified: {}\n"),
                    session.final_path.wstring());
            if (!m_host_manifest_complete)
            {
                m_host_manifest_complete = true;
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] host manifest completion inferred from verified session={} for legacy peer\n"),
                        session.session_id);
            }
            return true;
        }

        bool install_detours()
        {
            if (!m_functions.u_control_channel_received_bunch.address)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.PakSync] cannot install detour: ControlChannel::ReceivedBunch unresolved\n"));
                return false;
            }

            GRuntime.store(this, std::memory_order_release);
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] installing ControlChannel::ReceivedBunch detour target=0x{:016X} callback=0x{:016X}\n"),
                    reinterpret_cast<uintptr_t>(m_functions.u_control_channel_received_bunch.address),
                    reinterpret_cast<uintptr_t>(&control_channel_received_bunch_detour));
            m_detours.control_channel_received_bunch = std::make_unique<PLH::x64Detour>(
                    reinterpret_cast<uint64_t>(m_functions.u_control_channel_received_bunch.address),
                    reinterpret_cast<uint64_t>(&control_channel_received_bunch_detour),
                    &m_detours.control_channel_received_bunch_trampoline);
            m_detours.control_channel_received_bunch->setDetourScheme(PLH::x64Detour::RECOMMENDED);

            const bool ok = m_detours.control_channel_received_bunch->hook();
            if (ok)
            {
                GOriginalControlChannelReceivedBunch = reinterpret_cast<ControlChannelReceivedBunchFn>(m_detours.control_channel_received_bunch_trampoline);
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] ControlChannel::ReceivedBunch detour installed={} trampoline=0x{:016X}\n"),
                    ok,
                    m_detours.control_channel_received_bunch_trampoline);

            if (m_functions.u_channel_received_raw_bunch.address)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] installing UChannel::ReceivedRawBunch detour target=0x{:016X} callback=0x{:016X}\n"),
                        reinterpret_cast<uintptr_t>(m_functions.u_channel_received_raw_bunch.address),
                        reinterpret_cast<uintptr_t>(&received_raw_bunch_detour));
                m_detours.received_raw_bunch = std::make_unique<PLH::x64Detour>(
                        reinterpret_cast<uint64_t>(m_functions.u_channel_received_raw_bunch.address),
                        reinterpret_cast<uint64_t>(&received_raw_bunch_detour),
                        &m_detours.received_raw_bunch_trampoline);
                m_detours.received_raw_bunch->setDetourScheme(PLH::x64Detour::RECOMMENDED);

                const bool raw_ok = m_detours.received_raw_bunch->hook();
                if (raw_ok)
                {
                    GOriginalReceivedRawBunch = reinterpret_cast<ReceivedRawBunchFn>(m_detours.received_raw_bunch_trampoline);
                }

                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] UChannel::ReceivedRawBunch detour installed={} trampoline=0x{:016X}\n"),
                        raw_ok,
                        m_detours.received_raw_bunch_trampoline);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] UChannel::ReceivedRawBunch detour skipped: unresolved\n"));
            }

            install_pak_mount_detour();
            install_pak_mount_all_detour();

            return ok;
        }

        bool install_pak_mount_detour()
        {
            if (m_detours.pak_mount && m_detours.pak_mount->isHooked())
            {
                return true;
            }
            if (m_functions.f_pak_platform_file_mount.address)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] installing FPakPlatformFile::Mount detour target=0x{:016X} callback=0x{:016X}\n"),
                        reinterpret_cast<uintptr_t>(m_functions.f_pak_platform_file_mount.address),
                        reinterpret_cast<uintptr_t>(&pak_mount_detour));
                m_detours.pak_mount = std::make_unique<PLH::x64Detour>(
                        reinterpret_cast<uint64_t>(m_functions.f_pak_platform_file_mount.address),
                        reinterpret_cast<uint64_t>(&pak_mount_detour),
                        &m_detours.pak_mount_trampoline);
                m_detours.pak_mount->setDetourScheme(PLH::x64Detour::RECOMMENDED);

                const bool mount_ok = m_detours.pak_mount->hook();
                if (mount_ok)
                {
                    GOriginalPakMount = reinterpret_cast<PakMountFn>(m_detours.pak_mount_trampoline);
                }

                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] FPakPlatformFile::Mount detour installed={} trampoline=0x{:016X}\n"),
                        mount_ok,
                        m_detours.pak_mount_trampoline);
            }
            else
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] FPakPlatformFile::Mount detour skipped: unresolved\n"));
                return false;
            }

            return m_detours.pak_mount && m_detours.pak_mount->isHooked();
        }

        bool install_pak_mount_all_detour()
        {
            if (m_detours.pak_mount_all && m_detours.pak_mount_all->isHooked())
            {
                return true;
            }
            if (!m_functions.f_pak_platform_file_mount_all_pak_files.address)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] FPakPlatformFile::MountAllPakFiles detour skipped: unresolved\n"));
                return false;
            }

            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] installing FPakPlatformFile::MountAllPakFiles detour target=0x{:016X} callback=0x{:016X}\n"),
                    reinterpret_cast<uintptr_t>(m_functions.f_pak_platform_file_mount_all_pak_files.address),
                    reinterpret_cast<uintptr_t>(&pak_mount_all_pak_files_detour));
            m_detours.pak_mount_all = std::make_unique<PLH::x64Detour>(
                    reinterpret_cast<uint64_t>(m_functions.f_pak_platform_file_mount_all_pak_files.address),
                    reinterpret_cast<uint64_t>(&pak_mount_all_pak_files_detour),
                    &m_detours.pak_mount_all_trampoline);
            m_detours.pak_mount_all->setDetourScheme(PLH::x64Detour::RECOMMENDED);

            const bool ok = m_detours.pak_mount_all->hook();
            if (ok)
            {
                GOriginalPakMountAllPakFiles = reinterpret_cast<PakMountAllPakFilesFn>(m_detours.pak_mount_all_trampoline);
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] FPakPlatformFile::MountAllPakFiles detour installed={} trampoline=0x{:016X}\n"),
                    ok,
                    m_detours.pak_mount_all_trampoline);
            return ok;
        }

        void uninstall_detours()
        {
            if (m_load_map_pre_callback != Unreal::Hook::ERROR_ID)
            {
                Unreal::Hook::UnregisterCallback(m_load_map_pre_callback);
                m_load_map_pre_callback = Unreal::Hook::ERROR_ID;
            }
            if (m_detours.control_channel_received_bunch && m_detours.control_channel_received_bunch->isHooked())
            {
                m_detours.control_channel_received_bunch->unHook();
            }
            if (m_detours.received_raw_bunch && m_detours.received_raw_bunch->isHooked())
            {
                m_detours.received_raw_bunch->unHook();
            }
            if (m_detours.pak_mount && m_detours.pak_mount->isHooked())
            {
                m_detours.pak_mount->unHook();
            }
            if (m_detours.pak_mount_all && m_detours.pak_mount_all->isHooked())
            {
                m_detours.pak_mount_all->unHook();
            }
            restore_pending_net_game_tick_vtable();
            m_detours.control_channel_received_bunch.reset();
            m_detours.received_raw_bunch.reset();
            m_detours.pak_mount.reset();
            m_detours.pak_mount_all.reset();
            GOriginalControlChannelReceivedBunch = nullptr;
            GOriginalReceivedRawBunch = nullptr;
            GOriginalPakMount = nullptr;
            GOriginalPakMountAllPakFiles = nullptr;
            GRuntime.store(nullptr, std::memory_order_release);
        }

        auto enqueue_existing_incoming_paks_for_mount() -> size_t
        {
            if (m_config.stage_synced_paks_for_restart)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] existing incoming pak mount scan skipped: stage_synced_paks_for_restart=true\n"));
                return 0;
            }

            const auto incoming_dir = this->incoming_dir();
            std::error_code ec{};
            if (!fs::is_directory(incoming_dir, ec))
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] existing incoming pak scan skipped: directory not present {}\n"),
                        incoming_dir.wstring());
                return 0;
            }

            size_t queued_count = 0;
            for (const auto& entry : fs::directory_iterator{incoming_dir, ec})
            {
                if (ec)
                {
                    break;
                }
                if (!entry.is_regular_file(ec) || entry.path().extension() != L".pak")
                {
                    continue;
                }

                const auto before = m_pending_mounts.size();
                enqueue_verified_pak_for_mount(entry.path());
                if (m_pending_mounts.size() != before)
                {
                    ++queued_count;
                }
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] existing incoming pak scan completed dir={} queued={}\n"),
                    incoming_dir.wstring(),
                    queued_count);
            return queued_count;
        }

        void mount_active_paks_early()
        {
            if (m_config.dry_run || !m_config.stage_synced_paks_for_restart)
            {
                return;
            }
            if (m_launch_room_id.empty())
            {
                return;
            }
            if (!m_functions.f_platform_file_manager_get.address ||
                !m_functions.f_platform_file_manager_find_platform_file.address ||
                !m_functions.f_pak_platform_file_mount.address)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] early room pak mount skipped: resolver incomplete\n"));
                return;
            }

            const auto room_dir = launch_room_pak_dir();
            const auto room_paks = room_manifest_paks(m_launch_room_id);
            if (room_paks.empty())
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] early room pak mount skipped: no manifest pak entries room={} dir={}\n"),
                        m_launch_room_id,
                        room_dir.wstring());
                return;
            }

            const auto get_manager = reinterpret_cast<GetPlatformFileManagerFn>(m_functions.f_platform_file_manager_get.address);
            const auto find_platform_file = reinterpret_cast<FindPlatformFileFn>(m_functions.f_platform_file_manager_find_platform_file.address);
            const auto mount = reinterpret_cast<PakMountFn>(m_functions.f_pak_platform_file_mount.address);

            void* manager = get_manager();
            void* pak_file = manager ? find_platform_file(manager, L"PakFile") : nullptr;
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] early room pak mount lookup manager=0x{:016X} pak_file=0x{:016X} room={} dir={}\n"),
                    reinterpret_cast<uintptr_t>(manager),
                    reinterpret_cast<uintptr_t>(pak_file),
                    m_launch_room_id,
                    room_dir.wstring());
            if (!pak_file)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] early room pak mount skipped: PakFile platform file not found yet\n"));
                return;
            }

            size_t mounted_count = 0;
            for (const auto& pak : room_paks)
            {
                const auto pak_path = pak.wstring();
                const bool ok = mount(pak_file, pak_path.c_str(), m_config.pak_order, nullptr, true);
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] early room pak mount path={} order={} ok={}\n"),
                        pak_path,
                        m_config.pak_order,
                        ok);
                if (ok)
                {
                    m_mounted_paks.push_back(pak);
                    ++mounted_count;
                }
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] early room pak mount completed room={} dir={} mounted={}\n"),
                    m_launch_room_id,
                    room_dir.wstring(),
                    mounted_count);
        }

        void mount_active_paks_from_pak_platform_file(void* pak_file, const wchar_t* reason)
        {
            if (m_config.dry_run || !m_config.stage_synced_paks_for_restart || !pak_file)
            {
                return;
            }
            if (m_launch_room_id.empty())
            {
                return;
            }
            if (!m_functions.f_pak_platform_file_mount.address)
            {
                return;
            }

            const auto room_paks = room_manifest_paks(m_launch_room_id);
            if (room_paks.empty())
            {
                return;
            }

            const auto mount = GOriginalPakMount
                                       ? GOriginalPakMount
                                       : reinterpret_cast<PakMountFn>(m_functions.f_pak_platform_file_mount.address);
            size_t mounted_count = 0;
            for (const auto& pak : room_paks)
            {
                const auto already_mounted = std::any_of(m_mounted_paks.begin(), m_mounted_paks.end(), [&](const fs::path& mounted) {
                    return mounted == pak;
                });
                if (already_mounted)
                {
                    continue;
                }

                const auto pak_path = pak.wstring();
                const bool ok = mount(pak_file, pak_path.c_str(), m_config.pak_order, nullptr, true);
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] room pak mount from {} room={} path={} order={} ok={}\n"),
                        reason,
                        m_launch_room_id,
                        pak_path,
                        m_config.pak_order,
                        ok);
                if (ok)
                {
                    m_mounted_paks.push_back(pak);
                    ++mounted_count;
                }
            }
            if (mounted_count > 0)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] room pak mount from {} completed room={} mounted={}\n"),
                        reason,
                        m_launch_room_id,
                        mounted_count);
            }
        }

        auto enqueue_active_staged_paks_for_mount() -> size_t
        {
            if (m_launch_room_id.empty())
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] room pak scan skipped: missing -PakSyncRoom=<room_id>\n"));
                return 0;
            }
            const auto room_dir = launch_room_pak_dir();
            const auto room_paks = room_manifest_paks(m_launch_room_id);
            if (room_paks.empty())
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] room pak scan skipped: no manifest pak entries room={} dir={}\n"),
                        m_launch_room_id,
                        room_dir.wstring());
                return 0;
            }

            size_t queued_count = 0;
            for (const auto& pak : room_paks)
            {
                const auto already_listed = std::any_of(m_staged_active_paks.begin(), m_staged_active_paks.end(), [&](const fs::path& active) {
                    return active == pak;
                });
                if (!already_listed)
                {
                    m_staged_active_paks.push_back(pak);
                }

                const auto already_mounted = std::any_of(m_mounted_paks.begin(), m_mounted_paks.end(), [&](const fs::path& mounted) {
                    return mounted == pak;
                });
                if (!already_mounted)
                {
                    m_mounted_paks.push_back(pak);
                    ++queued_count;
                    Output::send<LogLevel::Normal>(
                            STR("[UE4SSL.PakSync] room pak recorded as mounted/early-mounted room={} path={}\n"),
                            m_launch_room_id,
                            pak.wstring());
                }
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] room pak scan completed room={} dir={} mounted={}\n"),
                    m_launch_room_id,
                    room_dir.wstring(),
                    queued_count);
            return queued_count;
        }

        void mount_pending_verified_paks()
        {
            if (m_config.stage_synced_paks_for_restart)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] hot mount skipped: stage_synced_paks_for_restart=true\n"));
                m_pending_mounts.clear();
                return;
            }
            if (!m_functions.f_platform_file_manager_get.address ||
                !m_functions.f_platform_file_manager_find_platform_file.address ||
                !m_functions.f_pak_platform_file_mount.address)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] pak mount skipped: resolver incomplete\n"));
                return;
            }

            const auto get_manager = reinterpret_cast<GetPlatformFileManagerFn>(m_functions.f_platform_file_manager_get.address);
            const auto find_platform_file = reinterpret_cast<FindPlatformFileFn>(m_functions.f_platform_file_manager_find_platform_file.address);
            const auto mount = reinterpret_cast<PakMountFn>(m_functions.f_pak_platform_file_mount.address);

            void* manager = get_manager();
            void* pak_file = manager ? find_platform_file(manager, L"PakFile") : nullptr;
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] pak mount lookup manager=0x{:016X} pak_file=0x{:016X}\n"),
                    reinterpret_cast<uintptr_t>(manager),
                    reinterpret_cast<uintptr_t>(pak_file));
            if (!pak_file)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.PakSync] pak mount skipped: PakFile platform file not found\n"));
                return;
            }

            auto pending = std::move(m_pending_mounts);
            m_pending_mounts.clear();
            for (const auto& pak : pending)
            {
                std::error_code ec{};
                if (!fs::is_regular_file(pak, ec))
                {
                    continue;
                }

                const auto pak_path = pak.wstring();
                auto before_packages = m_legacy_refresh.collect_before_mount(m_config);
                if (legacy_hot_refresh_enabled())
                {
                    m_legacy_refresh.detect_preexisting_packages_for_pak(m_config, pak, before_packages ? &*before_packages : nullptr);
                }
                const bool ok = mount(pak_file, pak_path.c_str(), m_config.pak_order, nullptr, true);
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync] pre-travel mount {} order={} ok={}\n"),
                        pak_path,
                        m_config.pak_order,
                        ok);
                if (ok)
                {
                    m_mounted_paks.push_back(pak);
                    if (!legacy_hot_refresh_enabled())
                    {
                        continue;
                    }
                    m_legacy_refresh.after_pak_mounted(
                            m_config,
                            m_functions,
                            pak,
                            before_packages ? &*before_packages : nullptr,
                            m_pending_mounts,
                            m_mounted_paks);
                }
                else
                {
                    m_pending_mounts.push_back(pak);
                }
            }
            if (legacy_hot_refresh_enabled())
            {
                m_legacy_refresh.schedule_game_cache_refresh_after_all_mounts(
                        m_config,
                        m_pending_mounts,
                        m_mounted_paks);
            }
        }

    private:
        PakSyncMod& m_owner;
        fs::path m_module_path{};
        fs::path m_mod_dir{};
        std::wstring m_launch_room_id{};
        RuntimeConfig m_config{};
        FunctionTable m_functions{};
        LegacyHotRefresh m_legacy_refresh{};
        SyncStatusWindow m_sync_window{};
        DetourState m_detours{};
        std::vector<PakManifest> m_manifests{};
        std::vector<QueuedFrame> m_queued_frames{};
        std::unordered_map<uint64_t, IncomingSession> m_incoming_sessions{};
        std::vector<fs::path> m_pending_mounts{};
        std::vector<fs::path> m_mounted_paks{};
        std::vector<fs::path> m_staged_active_paks{};
        std::vector<std::array<uint8_t, 32>> m_staging_manifest_hashes{};
        std::wstring m_staged_room_id{};
        std::vector<PendingDownloadApproval> m_pending_download_approvals{};
        std::vector<std::wstring> m_missing_host_paks{};
        std::vector<void*> m_pending_net_game_vtable_shadow{};
        Unreal::Hook::GlobalCallbackId m_load_map_pre_callback{Unreal::Hook::ERROR_ID};
        Unreal::Hook::GlobalCallbackId m_load_map_post_callback{Unreal::Hook::ERROR_ID};
        SyncPhase m_sync_phase{SyncPhase::WaitingForConnection};
        size_t m_last_connection_count{static_cast<size_t>(-1)};
        size_t m_cached_connection_count{};
        size_t m_pending_ready_suppression_logs{};
        size_t m_pending_ready_allowed_logs{};
        size_t m_pending_ready_restore_logs{};
        uint64_t m_next_connection_probe_ms{};
        uint64_t m_next_queued_frame_flush_ms{};
        uint64_t m_queued_frame_flush_started_ms{};
        uint64_t m_host_manifest_wait_start_ms{};
        uint64_t m_post_travel_quiet_until_ms{};
        uint64_t m_last_control_connection_observed_ms{};
        bool m_resolver_ready{};
        bool m_first_update_logged{};
        bool m_host_manifest_complete{};
        bool m_host_manifest_timeout_logged{};
        bool m_received_manifest_had_work{};
        bool m_outbound_manifest_queued_for_connection{};
        bool m_join_client_outbound_catalog_drop_logged{};
        bool m_join_client_observed_pending_net_game{};
        bool m_flushing_queued_frames{};
        bool m_pending_ready_was_suppressed{};
        bool m_pending_net_game_candidate_logged{};
        bool m_pending_net_game_rejection_logged{};
        bool m_actual_pending_net_game_candidate_logged{};
        bool m_last_control_connection_logged{};
        bool m_staged_manifest_seen_this_join{};
        bool m_restart_required_after_stage{};
        bool m_restart_prompt_shown{};
        bool m_missing_pak_prompt_shown{};
        ControlConnection m_last_control_connection{};
        void* m_tracked_pending_net_game{};
        void** m_original_pending_net_game_vtable{};
        bool m_pending_net_game_tick_hooked{};
    };

    namespace
    {
    void __fastcall control_channel_received_bunch_detour(void* channel, void* bunch)
    {
        const auto hit = GControlChannelReceivedBunchHitCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit <= 32 || hit % 256 == 0)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[UE4SSL.PakSync] ControlChannel::ReceivedBunch hit={} channel=0x{:016X} bunch=0x{:016X} trampoline=0x{:016X}\n"),
                    hit,
                    reinterpret_cast<uintptr_t>(channel),
                    reinterpret_cast<uintptr_t>(bunch),
                    reinterpret_cast<uintptr_t>(GOriginalControlChannelReceivedBunch));
        }

        auto* runtime = GRuntime.load(std::memory_order_acquire);
        if (runtime)
        {
            runtime->note_observed_control_channel(channel);
        }
        if (runtime && runtime->try_handle_bunch(channel, bunch))
        {
            return;
        }

        if (GOriginalControlChannelReceivedBunch)
        {
            GOriginalControlChannelReceivedBunch(channel, bunch);
        }
    }

    void __fastcall received_raw_bunch_detour(void* channel, void* bunch, bool* out_skip_ack, void* packet_id_range)
    {
        const auto hit = GReceivedRawBunchHitCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit <= 16 || hit % 1024 == 0)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[UE4SSL.PakSync] ReceivedRawBunch hit={} channel=0x{:016X} bunch=0x{:016X} out_skip_ack=0x{:016X} packet_id_range=0x{:016X} trampoline=0x{:016X}\n"),
                    hit,
                    reinterpret_cast<uintptr_t>(channel),
                    reinterpret_cast<uintptr_t>(bunch),
                    reinterpret_cast<uintptr_t>(out_skip_ack),
                    reinterpret_cast<uintptr_t>(packet_id_range),
                    reinterpret_cast<uintptr_t>(GOriginalReceivedRawBunch));
        }

        auto* runtime = GRuntime.load(std::memory_order_acquire);
        if (runtime)
        {
            runtime->note_observed_control_channel(channel);
        }

        if (GOriginalReceivedRawBunch)
        {
            GOriginalReceivedRawBunch(channel, bunch, out_skip_ack, packet_id_range);
        }
    }

    bool __fastcall pak_mount_detour(void* pak_platform_file, const wchar_t* pak_path, uint32_t pak_order, const wchar_t* mount_point, bool notify)
    {
        uint32_t effective_order = pak_order;
        const bool is_paksync_room_pak =
                pak_path &&
                std::wstring_view{pak_path}.find(L"UE4SSL.PakSync") != std::wstring_view::npos &&
                (std::wstring_view{pak_path}.find(L"rooms") != std::wstring_view::npos ||
                 std::wstring_view{pak_path}.find(L"incoming") != std::wstring_view::npos);

        auto* runtime = GRuntime.load(std::memory_order_acquire);
        if (is_paksync_room_pak && runtime)
        {
            effective_order = std::max<uint32_t>(pak_order, runtime->configured_pak_order());
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] FPakPlatformFile::Mount room pak order override path={} original_order={} effective_order={} mount_point={} notify={}\n"),
                    pak_path,
                    pak_order,
                    effective_order,
                    mount_point ? mount_point : L"<null>",
                    notify);
        }

        bool result = false;
        if (GOriginalPakMount)
        {
            result = GOriginalPakMount(pak_platform_file, pak_path, effective_order, mount_point, notify);
        }

        if (is_paksync_room_pak)
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] FPakPlatformFile::Mount room pak result={} path={} effective_order={}\n"),
                    result,
                    pak_path ? pak_path : L"<null>",
                    effective_order);
        }
        return result;
    }

    int32_t __fastcall pak_mount_all_pak_files_detour(void* pak_platform_file, void* pak_folders, void* wildcard)
    {
        int32_t result = 0;
        if (GOriginalPakMountAllPakFiles)
        {
            result = GOriginalPakMountAllPakFiles(pak_platform_file, pak_folders, wildcard);
        }

        auto* runtime = GRuntime.load(std::memory_order_acquire);
        if (runtime)
        {
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] FPakPlatformFile::MountAllPakFiles completed original_mounted={} pak_file=0x{:016X}; mounting PakSync active dir\n"),
                    result,
                    reinterpret_cast<uintptr_t>(pak_platform_file));
            runtime->mount_active_paks_from_mount_all_hook(pak_platform_file);
        }
        return result;
    }

    void __fastcall pending_net_game_tick_detour(void* pending_net_game, float delta_seconds)
    {
        const auto hit = GPendingNetGameTickHitCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (hit <= 8 || hit % 600 == 0)
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] PendingNetGame::Tick hit={} pending=0x{:016X} delta={}\n"),
                    hit,
                    reinterpret_cast<uintptr_t>(pending_net_game),
                    delta_seconds);
        }

        if (GOriginalPendingNetGameTick)
        {
            GOriginalPendingNetGameTick(pending_net_game, delta_seconds);
        }

        auto* runtime = GRuntime.load(std::memory_order_acquire);
        if (runtime)
        {
            runtime->after_pending_net_game_tick(pending_net_game);
        }
    }
    }

    PakSyncMod::PakSyncMod() : CppUserModBase()
    {
        ModName = STR("UE4SSL.PakSync");
        ModVersion = STR("0.1.0");
        ModDescription = STR("Pak synchronization over UE net channels");
        ModAuthors = STR("UE4SSL Community");
        m_impl = new PakSyncRuntime(*this);
        Output::send<LogLevel::Normal>(STR("[UE4SSL.PakSync] mod constructed\n"));
    }

    PakSyncMod::~PakSyncMod()
    {
        delete m_impl;
        m_impl = nullptr;
        Output::send<LogLevel::Normal>(STR("[UE4SSL.PakSync] mod destroyed\n"));
    }

    auto PakSyncMod::on_program_start() -> void
    {
        if (m_impl)
        {
            m_impl->on_program_start();
        }
    }

    auto PakSyncMod::on_unreal_init() -> void
    {
        if (m_impl)
        {
            m_impl->on_unreal_init();
        }
    }

    auto PakSyncMod::on_update() -> void
    {
        if (m_impl)
        {
            m_impl->on_update();
        }
    }
}

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod()
    {
        RC::Output::send<RC::LogLevel::Normal>(STR("[UE4SSL.PakSync] start_mod called\n"));
        return new RC::PakSync::PakSyncMod();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
    {
        RC::Output::send<RC::LogLevel::Normal>(STR("[UE4SSL.PakSync] uninstall_mod called\n"));
        delete mod;
    }
}

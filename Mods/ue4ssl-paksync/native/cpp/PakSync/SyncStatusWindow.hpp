#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace RC::PakSync
{
    struct SyncWindowState
    {
        std::wstring title{};
        std::wstring status{};
        std::wstring pak_name{};
        std::wstring room_id{};
        std::wstring hash{};
        uint64_t total_bytes{};
        uint64_t received_bytes{};
        uint32_t total_chunks{};
        uint32_t received_chunks{};
        bool complete{};
        bool restart_available{};
        bool approval_available{};
        bool download_declined{};
    };

    class SyncStatusWindow
    {
    public:
        using RestartCallback = std::function<void()>;
        using ApprovalCallback = std::function<void(bool approved)>;

        SyncStatusWindow();
        ~SyncStatusWindow();

        SyncStatusWindow(const SyncStatusWindow&) = delete;
        auto operator=(const SyncStatusWindow&) -> SyncStatusWindow& = delete;

        void set_restart_callback(RestartCallback callback);
        void set_approval_callback(ApprovalCallback callback);
        void show_or_update(const SyncWindowState& state);
        void close();

    private:
        struct Impl;
        Impl* m_impl{};
    };
}

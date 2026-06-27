#define NOMINMAX

#include "PakSync/SyncStatusWindow.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

#include <Windows.h>
#include <commctrl.h>

namespace RC::PakSync
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"UE4SSL.PakSync.SyncStatusWindow";
        constexpr UINT WM_PAKSYNC_UPDATE = WM_APP + 0x510;
        constexpr UINT WM_PAKSYNC_CLOSE = WM_APP + 0x511;
        constexpr int RestartButtonId = 1001;
        constexpr int LaterButtonId = 1002;
        constexpr int ApproveButtonId = 1003;
        constexpr int DeclineButtonId = 1004;

        auto format_bytes(uint64_t bytes) -> std::wstring
        {
            const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
            double value = static_cast<double>(bytes);
            size_t unit = 0;
            while (value >= 1024.0 && unit + 1 < std::size(units))
            {
                value /= 1024.0;
                ++unit;
            }

            std::wostringstream stream;
            stream.setf(std::ios::fixed);
            stream.precision(unit == 0 ? 0 : 1);
            stream << value << L" " << units[unit];
            return stream.str();
        }

        auto progress_percent(const SyncWindowState& state) -> int
        {
            if (state.total_bytes > 0)
            {
                return static_cast<int>(std::min<uint64_t>(100, (state.received_bytes * 100) / state.total_bytes));
            }
            if (state.total_chunks > 0)
            {
                return static_cast<int>(std::min<uint32_t>(100, (state.received_chunks * 100) / state.total_chunks));
            }
            return state.complete ? 100 : 0;
        }
    }

    struct SyncStatusWindow::Impl
    {
        std::mutex mutex{};
        SyncWindowState state{};
        RestartCallback restart_callback{};
        ApprovalCallback approval_callback{};
        std::thread thread{};
        std::atomic<DWORD> thread_id{};
        std::atomic<bool> ready{};
        HWND hwnd{};
        HWND title{};
        HWND status{};
        HWND pak{};
        HWND room{};
        HWND hash{};
        HWND bytes{};
        HWND chunks{};
        HWND progress{};
        HWND restart_button{};
        HWND later_button{};
        HWND approve_button{};
        HWND decline_button{};

        void set_restart_callback(RestartCallback callback)
        {
            std::scoped_lock lock{mutex};
            restart_callback = std::move(callback);
        }

        void set_approval_callback(ApprovalCallback callback)
        {
            std::scoped_lock lock{mutex};
            approval_callback = std::move(callback);
        }

        void show_or_update(const SyncWindowState& new_state)
        {
            {
                std::scoped_lock lock{mutex};
                state = new_state;
            }
            ensure_thread();
            post_update();
        }

        void close()
        {
            const DWORD id = thread_id.load();
            if (id != 0)
            {
                PostThreadMessageW(id, WM_PAKSYNC_CLOSE, 0, 0);
            }
            if (thread.joinable())
            {
                thread.join();
            }
            thread_id.store(0);
            ready.store(false);
        }

        void ensure_thread()
        {
            if (thread_id.load() != 0)
            {
                return;
            }

            if (thread.joinable())
            {
                thread.join();
            }

            ready.store(false);
            thread = std::thread([this]() {
                thread_main();
            });

            for (int i = 0; i < 100 && !ready.load(); ++i)
            {
                Sleep(10);
            }
        }

        void post_update()
        {
            const DWORD id = thread_id.load();
            if (id != 0)
            {
                PostThreadMessageW(id, WM_PAKSYNC_UPDATE, 0, 0);
            }
        }

        void thread_main()
        {
            thread_id.store(GetCurrentThreadId());
            INITCOMMONCONTROLSEX init{};
            init.dwSize = sizeof(init);
            init.dwICC = ICC_PROGRESS_CLASS;
            InitCommonControlsEx(&init);

            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &Impl::window_proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = WindowClassName;
            RegisterClassExW(&wc);

            hwnd = CreateWindowExW(
                    WS_EX_TOPMOST | WS_EX_APPWINDOW,
                    WindowClassName,
                    L"PakSync \u540c\u6b65\u72b6\u6001",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    580,
                    410,
                    nullptr,
                    nullptr,
                    wc.hInstance,
                    this);

            if (!hwnd)
            {
                ready.store(true);
                return;
            }

            create_controls();
            apply_state();
            ShowWindow(hwnd, SW_SHOWNORMAL);
            UpdateWindow(hwnd);
            ready.store(true);

            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                if (msg.message == WM_PAKSYNC_UPDATE)
                {
                    apply_state();
                    continue;
                }
                if (msg.message == WM_PAKSYNC_CLOSE)
                {
                    DestroyWindow(hwnd);
                    hwnd = nullptr;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        void create_controls()
        {
            const HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            title = make_static(20, 18, 530, 26);
            status = make_static(20, 52, 530, 24);
            progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 20, 86, 530, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            pak = make_static(20, 126, 530, 24);
            bytes = make_static(20, 154, 530, 24);
            chunks = make_static(20, 182, 530, 24);
            room = make_static(20, 210, 530, 24);
            hash = make_static(20, 238, 530, 24);
            approve_button = make_button(L"\u6279\u51c6\u4e0b\u8f7d", ApproveButtonId, 164, 292, 112, 32, true);
            decline_button = make_button(L"\u62d2\u7edd", DeclineButtonId, 288, 292, 88, 32, true);
            restart_button = make_button(L"\u7acb\u5373\u91cd\u542f\u6e38\u620f", RestartButtonId, 164, 332, 112, 32, false);
            later_button = make_button(L"\u7a0d\u540e", LaterButtonId, 288, 332, 88, 32, true);

            for (HWND control : {title, status, pak, bytes, chunks, room, hash, progress, restart_button, later_button, approve_button, decline_button})
            {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            SendMessageW(progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        }

        auto make_static(int x, int y, int w, int h) -> HWND
        {
            return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        }

        auto make_button(const wchar_t* text, int id, int x, int y, int w, int h, bool enabled) -> HWND
        {
            auto* control = CreateWindowExW(
                    0,
                    L"BUTTON",
                    text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x,
                    y,
                    w,
                    h,
                    hwnd,
                    reinterpret_cast<HMENU>(id),
                    GetModuleHandleW(nullptr),
                    nullptr);
            EnableWindow(control, enabled ? TRUE : FALSE);
            return control;
        }

        void apply_state()
        {
            if (!hwnd)
            {
                return;
            }

            SyncWindowState copy{};
            {
                std::scoped_lock lock{mutex};
                copy = state;
            }

            const int percent = progress_percent(copy);
            const auto title_text = copy.title.empty() ? L"PakSync \u6b63\u5728\u540c\u6b65\u623f\u95f4 Mod" : copy.title;
            const auto status_text = copy.status.empty() ? L"\u6b63\u5728\u7b49\u5f85\u540c\u6b65\u4fe1\u606f..." : copy.status;
            const auto pak_text = std::wstring{L"\u540c\u6b65\u5185\u5bb9\uff1a"} + (copy.pak_name.empty() ? std::wstring{L"<\u672a\u77e5 pak>"} : copy.pak_name);
            const auto bytes_text = std::wstring{L"\u6570\u636e\u8fdb\u5ea6\uff1a"} + format_bytes(copy.received_bytes) + L" / " + format_bytes(copy.total_bytes) +
                                    L" (" + std::to_wstring(percent) + L"%)";
            const auto chunks_text = std::wstring{L"\u5206\u7247\u8fdb\u5ea6\uff1a"} + std::to_wstring(copy.received_chunks) + L" / " + std::to_wstring(copy.total_chunks);
            const auto room_text = std::wstring{L"\u623f\u95f4\u6807\u8bc6\uff1a"} + (copy.room_id.empty() ? std::wstring{L"<\u540c\u6b65\u5b8c\u6210\u540e\u751f\u6210>"} : copy.room_id);
            const auto hash_text = std::wstring{L"SHA256\uff1a"} + (copy.hash.empty() ? std::wstring{L"<\u672a\u77e5>"} : copy.hash);

            SetWindowTextW(hwnd, copy.complete ? L"PakSync \u540c\u6b65\u5b8c\u6210" : L"PakSync \u540c\u6b65\u72b6\u6001");
            SetWindowTextW(title, title_text.c_str());
            SetWindowTextW(status, status_text.c_str());
            SetWindowTextW(pak, pak_text.c_str());
            SetWindowTextW(bytes, bytes_text.c_str());
            SetWindowTextW(chunks, chunks_text.c_str());
            SetWindowTextW(room, room_text.c_str());
            SetWindowTextW(hash, hash_text.c_str());
            SendMessageW(progress, PBM_SETPOS, percent, 0);
            EnableWindow(approve_button, copy.approval_available ? TRUE : FALSE);
            EnableWindow(decline_button, copy.approval_available ? TRUE : FALSE);
            EnableWindow(restart_button, copy.restart_available ? TRUE : FALSE);
        }

        void invoke_restart()
        {
            RestartCallback callback{};
            {
                std::scoped_lock lock{mutex};
                callback = restart_callback;
            }
            if (callback)
            {
                callback();
            }
        }

        void invoke_approval(bool approved)
        {
            ApprovalCallback callback{};
            {
                std::scoped_lock lock{mutex};
                state.approval_available = false;
                state.download_declined = !approved;
                state.status = approved ? L"\u5df2\u6279\u51c6\u4e0b\u8f7d\uff0c\u6b63\u5728\u8bf7\u6c42\u4f20\u8f93\u5206\u7247\u3002" : L"\u5df2\u62d2\u7edd\u4e0b\u8f7d\uff0cPakSync \u4e0d\u4f1a\u4ece\u623f\u4e3b\u63a5\u6536\u6b64 pak\u3002";
                callback = approval_callback;
            }
            apply_state();
            if (callback)
            {
                callback(approved);
            }
        }

        static auto CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> LRESULT
        {
            auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE)
            {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
                self = reinterpret_cast<Impl*>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }

            if (self)
            {
                switch (msg)
                {
                case WM_COMMAND:
                    if (LOWORD(wparam) == RestartButtonId)
                    {
                        self->invoke_restart();
                        return 0;
                    }
                    if (LOWORD(wparam) == ApproveButtonId)
                    {
                        self->invoke_approval(true);
                        return 0;
                    }
                    if (LOWORD(wparam) == DeclineButtonId)
                    {
                        self->invoke_approval(false);
                        return 0;
                    }
                    if (LOWORD(wparam) == LaterButtonId)
                    {
                        ShowWindow(hwnd, SW_HIDE);
                        return 0;
                    }
                    break;
                case WM_CLOSE:
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
                default:
                    break;
                }
            }

            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    };

    SyncStatusWindow::SyncStatusWindow() : m_impl(new Impl()) {}

    SyncStatusWindow::~SyncStatusWindow()
    {
        if (m_impl)
        {
            m_impl->close();
            delete m_impl;
            m_impl = nullptr;
        }
    }

    void SyncStatusWindow::set_restart_callback(RestartCallback callback)
    {
        m_impl->set_restart_callback(std::move(callback));
    }

    void SyncStatusWindow::set_approval_callback(ApprovalCallback callback)
    {
        m_impl->set_approval_callback(std::move(callback));
    }

    void SyncStatusWindow::show_or_update(const SyncWindowState& state)
    {
        m_impl->show_or_update(state);
    }

    void SyncStatusWindow::close()
    {
        m_impl->close();
    }
}

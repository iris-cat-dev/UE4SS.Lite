#include "JSInternal.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <mmsystem.h>
#include <winhttp.h>

#include <DynamicOutput/DynamicOutput.hpp>

namespace RC::JSScript
{
    namespace
    {
        struct DownloadOptions
        {
            int timeout_ms = 30000;
            std::string headers_str{};
        };

        struct DownloadResult
        {
            bool ok = false;
            int status = 0;
            int64_t bytes_written = 0;
            std::string error{};
        };

        auto js_value_to_utf8_string(JSContext* ctx, JSValueConst value) -> std::string
        {
            size_t len = 0;
            const char* cstr = JS_ToCStringLen(ctx, &len, value);
            if (!cstr)
            {
                return {};
            }

            std::string out(cstr, cstr + len);
            JS_FreeCString(ctx, cstr);
            return out;
        }

        auto get_bool_option(JSContext* ctx, JSValueConst options, const char* key, bool fallback) -> bool
        {
            if (!JS_IsObject(options))
            {
                return fallback;
            }

            JSValue value = JS_GetPropertyStr(ctx, options, key);
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                fallback = JS_ToBool(ctx, value) != 0;
            }
            JS_FreeValue(ctx, value);
            return fallback;
        }

        void parse_download_options(JSContext* ctx, int argc, JSValueConst* argv, DownloadOptions& out_options)
        {
            if (argc < 3 || JS_IsUndefined(argv[2]) || JS_IsNull(argv[2]) || !JS_IsObject(argv[2]))
            {
                return;
            }

            JSValue options = argv[2];

            JSValue timeout_value = JS_GetPropertyStr(ctx, options, "timeoutMs");
            if (!JS_IsUndefined(timeout_value) && !JS_IsNull(timeout_value))
            {
                int32_t timeout_ms = 0;
                if (JS_ToInt32(ctx, &timeout_ms, timeout_value) == 0 && timeout_ms > 0)
                {
                    out_options.timeout_ms = timeout_ms;
                }
            }
            JS_FreeValue(ctx, timeout_value);

            JSValue headers_value = JS_GetPropertyStr(ctx, options, "headers");
            if (JS_IsObject(headers_value))
            {
                JSPropertyEnum* names = nullptr;
                uint32_t len = 0;
                if (JS_GetOwnPropertyNames(ctx, &names, &len, headers_value, JS_GPN_STRING_MASK) == 0)
                {
                    for (uint32_t i = 0; i < len; i++)
                    {
                        const char* key = JS_AtomToCString(ctx, names[i].atom);
                        JSValue entry_value = JS_GetProperty(ctx, headers_value, names[i].atom);
                        const char* entry_cstr = JS_ToCString(ctx, entry_value);
                        if (key && entry_cstr)
                        {
                            out_options.headers_str += std::string(key) + ": " + entry_cstr + "\r\n";
                        }
                        if (key)
                        {
                            JS_FreeCString(ctx, key);
                        }
                        if (entry_cstr)
                        {
                            JS_FreeCString(ctx, entry_cstr);
                        }
                        JS_FreeValue(ctx, entry_value);
                    }
                    js_free(ctx, names);
                }
            }
            JS_FreeValue(ctx, headers_value);
        }

        auto make_download_result(JSContext* ctx, const std::string& path, const DownloadResult& result) -> JSValue
        {
            JSValue response = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, response, "ok", JS_NewBool(ctx, result.ok));
            JS_SetPropertyStr(ctx, response, "status", JS_NewInt32(ctx, result.status));
            JS_SetPropertyStr(ctx, response, "bytesWritten", JS_NewInt64(ctx, result.bytes_written));
            JS_SetPropertyStr(ctx, response, "path", JS_NewString(ctx, path.c_str()));
            JS_SetPropertyStr(ctx, response, "error", JS_NewString(ctx, result.error.c_str()));
            return response;
        }

        auto safe_download_callback_call(JSMod* mod, JSContext* ctx, JSValueConst callback,
                                         int argc, JSValueConst* argv, const wchar_t* operation) -> bool
        {
            JSValue call_result = JS_UNDEFINED;
            if (!safe_js_call(ctx, callback, JS_UNDEFINED, argc, argv, &call_result))
            {
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, operation, L"SEH exception");
                return false;
            }

            if (JS_IsException(call_result))
            {
                mod->log_exception(ctx, operation);
                JS_FreeValue(ctx, call_result);
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, operation, L"JS exception");
                return false;
            }

            JS_FreeValue(ctx, call_result);
            mod->report_subsystem_success(JSMod::GuardedSubsystem::Fetch);
            return true;
        }

        void download_file_to_path(const std::string& url, const std::filesystem::path& output_path,
                                   const DownloadOptions& options, DownloadResult& out_result)
        {
            std::wstring wurl = utf8_to_wide(url);
            if (wurl.empty())
            {
                out_result.error = "Invalid URL encoding";
                return;
            }

            URL_COMPONENTS components = {};
            components.dwStructSize = sizeof(components);
            wchar_t host_buf[256] = {};
            wchar_t path_buf[2048] = {};
            wchar_t extra_buf[4096] = {};
            components.lpszHostName = host_buf;
            components.dwHostNameLength = static_cast<DWORD>(std::size(host_buf));
            components.lpszUrlPath = path_buf;
            components.dwUrlPathLength = static_cast<DWORD>(std::size(path_buf));
            components.lpszExtraInfo = extra_buf;
            components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra_buf));
            if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &components))
            {
                out_result.error = "Failed to parse URL";
                return;
            }

            bool use_ssl = components.nScheme == INTERNET_SCHEME_HTTPS;
            int port = components.nPort == 0 ? (use_ssl ? 443 : 80) : components.nPort;
            std::wstring request_path = path_buf[0] ? path_buf : L"/";
            if (extra_buf[0])
            {
                request_path += extra_buf;
            }

            std::filesystem::path parent_path = output_path.parent_path();
            std::error_code filesystem_error;
            if (!parent_path.empty())
            {
                std::filesystem::create_directories(parent_path, filesystem_error);
                if (filesystem_error)
                {
                    out_result.error = "Failed to create output directory";
                    return;
                }
            }

            std::filesystem::path temp_path = output_path;
            temp_path += L".download";
            std::filesystem::remove(temp_path, filesystem_error);
            filesystem_error.clear();

            HINTERNET session = WinHttpOpen(L"UE4SS-JS/1.0", 0, nullptr, nullptr, 0);
            if (!session)
            {
                out_result.error = "WinHttpOpen failed";
                return;
            }

            HINTERNET connection = nullptr;
            HINTERNET request = nullptr;

            auto close_handles = [&]() {
                if (request)
                {
                    WinHttpCloseHandle(request);
                    request = nullptr;
                }
                if (connection)
                {
                    WinHttpCloseHandle(connection);
                    connection = nullptr;
                }
                if (session)
                {
                    WinHttpCloseHandle(session);
                    session = nullptr;
                }
            };

            auto cleanup_temp = [&]() {
                std::error_code cleanup_error;
                std::filesystem::remove(temp_path, cleanup_error);
            };

            int timeout_ms = std::max(options.timeout_ms, 1);
            WinHttpSetTimeouts(session, 5000, timeout_ms, timeout_ms, timeout_ms);

            connection = WinHttpConnect(session, host_buf, static_cast<INTERNET_PORT>(port), 0);
            if (!connection)
            {
                close_handles();
                out_result.error = "WinHttpConnect failed";
                return;
            }

            DWORD request_flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
            request = WinHttpOpenRequest(connection, L"GET", request_path.c_str(), nullptr, nullptr, nullptr, request_flags);
            if (!request)
            {
                close_handles();
                out_result.error = "WinHttpOpenRequest failed";
                return;
            }

            if (use_ssl)
            {
                DWORD secure_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &secure_flags, sizeof(secure_flags));
            }

            if (!options.headers_str.empty())
            {
                std::wstring headers = utf8_to_wide(options.headers_str);
                if (!headers.empty())
                {
                    WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
                }
            }

            if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                close_handles();
                out_result.error = "WinHttpSendRequest failed";
                return;
            }

            if (!WinHttpReceiveResponse(request, nullptr))
            {
                close_handles();
                out_result.error = "WinHttpReceiveResponse failed";
                return;
            }

            DWORD status_code = 0;
            DWORD status_code_size = sizeof(status_code);
            if (!WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    nullptr,
                    &status_code,
                    &status_code_size,
                    nullptr))
            {
                close_handles();
                out_result.error = "WinHttpQueryHeaders failed";
                return;
            }
            out_result.status = static_cast<int>(status_code);

            std::ofstream output_file(temp_path, std::ios::binary | std::ios::trunc);
            if (!output_file.is_open())
            {
                close_handles();
                out_result.error = "Failed to open output file";
                return;
            }

            std::vector<char> buffer(4096);
            DWORD read_len = 0;
            for (;;)
            {
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read_len))
                {
                    output_file.close();
                    close_handles();
                    cleanup_temp();
                    out_result.error = "WinHttpReadData failed";
                    return;
                }

                if (read_len == 0)
                {
                    break;
                }

                output_file.write(buffer.data(), static_cast<std::streamsize>(read_len));
                if (!output_file.good())
                {
                    output_file.close();
                    close_handles();
                    cleanup_temp();
                    out_result.error = "Failed to write downloaded data";
                    return;
                }

                out_result.bytes_written += static_cast<int64_t>(read_len);
            }

            output_file.close();
            close_handles();

            if (out_result.status < 200 || out_result.status >= 300)
            {
                cleanup_temp();
                out_result.error = "HTTP " + std::to_string(out_result.status);
                return;
            }

            if (out_result.bytes_written <= 0)
            {
                cleanup_temp();
                out_result.error = "Empty response body";
                return;
            }

            std::filesystem::remove(output_path, filesystem_error);
            filesystem_error.clear();
            std::filesystem::rename(temp_path, output_path, filesystem_error);
            if (filesystem_error)
            {
                filesystem_error.clear();
                std::filesystem::copy_file(temp_path, output_path, std::filesystem::copy_options::overwrite_existing,
                                           filesystem_error);
                if (filesystem_error)
                {
                    cleanup_temp();
                    out_result.error = "Failed to finalize downloaded file";
                    return;
                }
                std::filesystem::remove(temp_path, filesystem_error);
            }

            out_result.ok = true;
        }
    } // namespace

    void download_worker_run(JSMod* mod)
    {
        while (!mod->m_download_worker_stop.load())
        {
            try
            {
                JSMod::DownloadRequest request;
                bool has_work = false;
                {
                    std::unique_lock<std::mutex> lock(mod->m_download_mutex);
                    mod->m_download_request_cv.wait(lock, [mod]() {
                        return mod->m_download_worker_stop.load() || !mod->m_download_request_queue.empty();
                    });
                    if (mod->m_download_worker_stop.load())
                    {
                        break;
                    }
                    if (!mod->m_download_request_queue.empty())
                    {
                        request = std::move(mod->m_download_request_queue.front());
                        mod->m_download_request_queue.erase(mod->m_download_request_queue.begin());
                        has_work = true;
                    }
                }
                if (!has_work)
                {
                    continue;
                }

                DownloadOptions options;
                options.timeout_ms = request.timeout_ms;
                options.headers_str = request.headers_str;

                DownloadResult result;
                try
                {
                    download_file_to_path(request.url, std::filesystem::path(utf8_to_wide(request.output_path)), options, result);
                }
                catch (const std::exception& e)
                {
                    result.error = std::string("download exception: ") + e.what();
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] download_file_to_path exception: {}\n"),
                                                  std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    result.error = "download: unknown exception during HTTP request";
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] download_file_to_path: unknown exception\n"));
                }

                {
                    std::lock_guard<std::mutex> lock(mod->m_download_mutex);
                    mod->m_download_result_queue.push_back({
                        request.id,
                        result.ok,
                        result.status,
                        result.bytes_written,
                        std::move(request.output_path),
                        std::move(result.error),
                    });
                }
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] download worker loop exception: {}\n"),
                                              std::wstring(e.what(), e.what() + strlen(e.what())));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] download worker loop: unknown exception, continuing\n"));
            }
        }
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] download worker thread exiting normally\n"));
    }

    JSValue js_download_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;

        JSMod* mod = get_js_mod(ctx);
        if (!mod)
        {
            return JS_ThrowInternalError(ctx, "No JSMod");
        }
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Fetch))
        {
            return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");
        }

        if (argc < 2)
        {
            return JS_ThrowTypeError(ctx, "downloadFile requires 2 arguments: url, path");
        }

        std::string url = js_value_to_utf8_string(ctx, argv[0]);
        if (url.empty())
        {
            return JS_ThrowTypeError(ctx, "downloadFile: url must be a string");
        }

        std::string path = js_value_to_utf8_string(ctx, argv[1]);
        if (path.empty())
        {
            return JS_ThrowTypeError(ctx, "downloadFile: path must be a string");
        }

        DownloadOptions options;
        parse_download_options(ctx, argc, argv, options);

        JSValue resolving_funcs[2];
        JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(promise))
        {
            return promise;
        }

        JSValue resolve_func = JS_DupValue(ctx, resolving_funcs[0]);
        JSValue reject_func = JS_DupValue(ctx, resolving_funcs[1]);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);

        int64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(mod->m_download_mutex);
            id = mod->m_download_next_id++;
            mod->m_download_pending[id] = { resolve_func, reject_func };
            mod->m_download_request_queue.push_back({
                id,
                std::move(url),
                std::move(path),
                options.timeout_ms,
                std::move(options.headers_str),
            });
        }
        mod->m_download_request_cv.notify_one();
        return promise;
    }

    JSValue js_download_file_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;

        if (argc < 2)
        {
            return JS_ThrowTypeError(ctx, "downloadFileSync requires 2 arguments: url, path");
        }

        std::string url = js_value_to_utf8_string(ctx, argv[0]);
        if (url.empty())
        {
            return JS_ThrowTypeError(ctx, "downloadFileSync: url must be a string");
        }

        std::string path = js_value_to_utf8_string(ctx, argv[1]);
        if (path.empty())
        {
            return JS_ThrowTypeError(ctx, "downloadFileSync: path must be a string");
        }

        DownloadOptions options;
        parse_download_options(ctx, argc, argv, options);

        DownloadResult result;
        try
        {
            download_file_to_path(url, std::filesystem::path(utf8_to_wide(path)), options, result);
        }
        catch (const std::exception& e)
        {
            result.error = std::string("downloadFileSync exception: ") + e.what();
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] downloadFileSync exception: {}\n"),
                                          std::wstring(e.what(), e.what() + strlen(e.what())));
        }
        catch (...)
        {
            result.error = "downloadFileSync failed due to unknown exception";
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] downloadFileSync unknown exception\n"));
        }

        return make_download_result(ctx, path, result);
    }

    auto JSMod::process_download_results() -> void
    {
        std::vector<CompletedDownloadResult> results;
        {
            std::lock_guard<std::mutex> lock(m_download_mutex);
            results.swap(m_download_result_queue);
        }

        JSContext* ctx = m_main_ctx;
        if (!ctx)
        {
            return;
        }

        const bool fetch_disabled = is_subsystem_disabled(GuardedSubsystem::Fetch);
        for (auto& result : results)
        {
            PendingDownloadCallbacks callbacks;
            {
                std::lock_guard<std::mutex> lock(m_download_mutex);
                auto it = m_download_pending.find(result.id);
                if (it == m_download_pending.end())
                {
                    continue;
                }
                callbacks = it->second;
                m_download_pending.erase(it);
            }

            if (fetch_disabled)
            {
                JSValue err = JS_NewError(ctx);
                JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "Fetch subsystem disabled by circuit breaker"));
                JSValue args[1] = { err };
                safe_download_callback_call(this, ctx, callbacks.reject_func, 1, args, L"DownloadRejectCallback");
                JS_FreeValue(ctx, err);
                JS_FreeValue(ctx, callbacks.resolve_func);
                JS_FreeValue(ctx, callbacks.reject_func);
                continue;
            }

            try
            {
                DownloadResult payload;
                payload.ok = result.ok;
                payload.status = result.status;
                payload.bytes_written = result.bytes_written;
                payload.error = result.error_msg;

                JSValue response = make_download_result(ctx, result.path, payload);
                JSValue args[1] = { response };
                safe_download_callback_call(this, ctx, callbacks.resolve_func, 1, args, L"DownloadResolveCallback");
                JS_FreeValue(ctx, response);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_download_results exception for id {}: {}\n"),
                                              result.id, std::wstring(e.what(), e.what() + strlen(e.what())));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_download_results unknown exception for id {}\n"),
                                              result.id);
            }

            JS_FreeValue(ctx, callbacks.resolve_func);
            JS_FreeValue(ctx, callbacks.reject_func);
        }
    }

    JSValue js_play_sound_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;

        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "playSoundFile requires 1 argument: path");
        }

        std::string path = js_value_to_utf8_string(ctx, argv[0]);
        if (path.empty())
        {
            return JS_ThrowTypeError(ctx, "playSoundFile: path must be a string");
        }

        bool async = true;
        bool loop = false;
        bool no_stop = false;
        if (argc >= 2 && JS_IsObject(argv[1]))
        {
            async = get_bool_option(ctx, argv[1], "async", true);
            loop = get_bool_option(ctx, argv[1], "loop", false);
            no_stop = get_bool_option(ctx, argv[1], "noStop", false);
        }

        if (loop)
        {
            async = true;
        }

        std::wstring wide_path = utf8_to_wide(path);
        if (wide_path.empty())
        {
            return JS_ThrowTypeError(ctx, "playSoundFile: invalid path encoding");
        }

        DWORD flags = SND_FILENAME | SND_NODEFAULT;
        flags |= async ? SND_ASYNC : SND_SYNC;
        if (loop)
        {
            flags |= SND_LOOP;
        }
        if (no_stop)
        {
            flags |= SND_NOSTOP;
        }

        BOOL success = PlaySoundW(wide_path.c_str(), nullptr, flags);
        return JS_NewBool(ctx, success == TRUE);
    }

    JSValue js_stop_sound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        (void)argc;
        (void)argv;

        BOOL success = PlaySoundW(nullptr, nullptr, 0);
        return JS_NewBool(ctx, success == TRUE);
    }
} // namespace RC::JSScript
